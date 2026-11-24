#include "backend.h"
#include "inspector.h"

#include <QClipboard>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace {

const QStringList &supportedExtensions() {
    static const QStringList exts = {
        "png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff", "svg", "avif", "heic", "ico"
    };
    return exts;
}

CupsOption pickOption(const QHash<QString, CupsOption> &all, const QStringList &preferredKeys) {
    for (const QString &key : preferredKeys) {
        if (all.contains(key))
            return all.value(key);
    }
    return {};
}

QVariantList toOptionModel(const CupsOption &opt) {
    QVariantList list;
    for (const QString &v : opt.values) {
        QVariantMap row;
        row.insert("value", v);
        row.insert("label", Backend::friendlyCupsValue(v));
        list.append(row);
    }
    return list;
}

QString formatFilterLabel(const QString &format) {
    const QString norm = ImageOps::normalizedFormat(format);
    if (norm == "jpeg") return QObject::tr("JPEG images");
    if (norm == "webp") return QObject::tr("WebP images");
    if (norm == "tiff") return QObject::tr("TIFF images");
    if (norm == "bmp") return QObject::tr("BMP images");
    if (norm == "pdf") return QObject::tr("PDF documents");
    return QObject::tr("PNG images");
}

} // namespace

Backend::Backend(FilePicker *filePicker,
                 const QString &lpstatPath,
                 const QString &lpPath,
                 const QString &lpoptionsPath,
                 const QString &presetStorePath,
                 QObject *parent)
    : QObject(parent),
      m_filePicker(filePicker),
      m_lpstatPath(!lpstatPath.isEmpty() ? lpstatPath : QStandardPaths::findExecutable("lpstat")),
      m_lpPath(!lpPath.isEmpty() ? lpPath : QStandardPaths::findExecutable("lp")),
      m_lpoptionsPath(!lpoptionsPath.isEmpty() ? lpoptionsPath : QStandardPaths::findExecutable("lpoptions")),
      m_presetStore(presetStorePath) {
    // Restore Last Used Settings automatically on launch, resetting copies to 1
    // like macOS Preview so a previous multi-copy job isn't repeated by accident.
    if (m_presetStore.hasLastUsed()) {
        m_printSettings = m_presetStore.lastUsedSettings();
        m_printSettings.copies = 1;
        m_preferredPrinter = m_presetStore.lastUsedPrinter();
    }
    m_activePresetId = m_presetStore.activePresetId();
    PrintPreset checkPreset;
    if (!m_presetStore.findById(m_activePresetId, checkPreset))
        m_activePresetId = "last-used";

    if (!m_presetStore.exportFormat().isEmpty()) {
        m_exportFormat = m_presetStore.exportFormat();
        m_hasSavedExportFormat = true;
    }
    m_exportQuality = m_presetStore.exportQuality();

    connect(&m_lpstatProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString stdoutText = QString::fromUtf8(m_lpstatProcess.readAllStandardOutput());
        const QString stderrText = QString::fromUtf8(m_lpstatProcess.readAllStandardError()).trimmed();

        QStringList found;
        QString defPrinter;
        parseLpstatOutput(stdoutText, found, defPrinter);

        m_printers = found;
        m_defaultPrinter = defPrinter;

        const QString previousPrinter = m_selectedPrinter;
        if (m_printers.isEmpty()) {
            if (code != 0 || status != QProcess::NormalExit) {
                m_printersError = stderrText.isEmpty()
                                      ? tr("CUPS printing service is not responding.")
                                      : stderrText;
            } else {
                m_printersError = tr("No printers configured in CUPS.");
            }
            m_selectedPrinter.clear();
            clearPrinterOptions();
        } else {
            m_printersError.clear();
            if (m_selectedPrinter.isEmpty() || !m_printers.contains(m_selectedPrinter)) {
                if (!m_preferredPrinter.isEmpty() && m_printers.contains(m_preferredPrinter))
                    m_selectedPrinter = m_preferredPrinter;
                else if (!m_defaultPrinter.isEmpty() && m_printers.contains(m_defaultPrinter))
                    m_selectedPrinter = m_defaultPrinter;
                else
                    m_selectedPrinter = m_printers.first();
            }
            if (m_selectedPrinter != previousPrinter || !hasPrinterOptions())
                refreshPrinterOptions();
        }

        emit printersChanged();
        emit printSettingsChanged();
    });

    connect(&m_lpstatProcess, &QProcess::errorOccurred, this, [this] {
        m_printers.clear();
        m_defaultPrinter.clear();
        m_selectedPrinter.clear();
        clearPrinterOptions();
        m_printersError = tr("Could not run lpstat to list printers.");
        emit printersChanged();
        emit printSettingsChanged();
    });

    connect(&m_lpoptionsProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (code != 0 || status != QProcess::NormalExit) {
            clearPrinterOptions();
            return;
        }
        QHash<QString, CupsOption> parsed;
        parseLpoptionsOutput(QString::fromUtf8(m_lpoptionsProcess.readAllStandardOutput()), parsed);

        m_mediaTypeOpt = pickOption(parsed, {"MediaType", "cupsMediaType", "InputSlot"});
        m_qualityOpt = pickOption(parsed, {"PrintQuality", "cupsPrintQuality", "Quality", "OutputMode"});
        m_duplexOpt = pickOption(parsed, {"Duplex", "sides", "EFDuplex"});

        m_printSettings.mediaTypeKey = m_mediaTypeOpt.key;
        if (!m_mediaTypeOpt.values.contains(m_printSettings.mediaTypeValue))
            m_printSettings.mediaTypeValue = m_mediaTypeOpt.defaultValue;

        m_printSettings.qualityKey = m_qualityOpt.key;
        if (!m_qualityOpt.values.contains(m_printSettings.qualityValue))
            m_printSettings.qualityValue = m_qualityOpt.defaultValue;

        m_printSettings.duplexKey = m_duplexOpt.key;
        if (!m_duplexOpt.values.contains(m_printSettings.duplexValue))
            m_printSettings.duplexValue = m_duplexOpt.defaultValue;

        if (!m_activePresetModified)
            applyBuiltInCupsHints(m_activePresetId);

        emit printerOptionsChanged();
        emit printSettingsChanged();
    });

    connect(&m_lpoptionsProcess, &QProcess::errorOccurred, this, [this] {
        clearPrinterOptions();
    });

    connect(&m_lpProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString stdoutText = QString::fromUtf8(m_lpProcess.readAllStandardOutput()).trimmed();
        const QString stderrText = QString::fromUtf8(m_lpProcess.readAllStandardError()).trimmed();

        if (!m_activeTempPdf.isEmpty()) {
            QFile::remove(m_activeTempPdf);
            m_activeTempPdf.clear();
        }

        m_printBusy = false;
        emit printStateChanged();

        if (code == 0 && status == QProcess::NormalExit) {
            static const QRegularExpression jobRe("request id is (\\S+)");
            const auto match = jobRe.match(stdoutText);
            if (match.hasMatch())
                setStatus(tr("Sent to %1 (%2)").arg(m_selectedPrinter, match.captured(1)));
            else
                setStatus(tr("Sent to %1").arg(m_selectedPrinter));
        } else {
            setError(stderrText.isEmpty()
                         ? tr("Printing failed (exit code %1).").arg(code)
                         : tr("Printing failed: %1").arg(stderrText));
        }
    });

    connect(&m_lpProcess, &QProcess::errorOccurred, this, [this] {
        if (!m_activeTempPdf.isEmpty()) {
            QFile::remove(m_activeTempPdf);
            m_activeTempPdf.clear();
        }
        m_printBusy = false;
        emit printStateChanged();
        setError(tr("Could not start the lp print command."));
    });

    refreshPrinters();
}

Backend::~Backend() {
    auto stopProc = [](QProcess &proc) {
        if (proc.state() != QProcess::NotRunning) {
            proc.kill();
            proc.waitForFinished(1000);
        }
    };
    stopProc(m_lpstatProcess);
    stopProc(m_lpoptionsProcess);
    stopProc(m_lpProcess);
    if (!m_activeTempPdf.isEmpty())
        QFile::remove(m_activeTempPdf);
}

QString Backend::filePath() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_files.size())
        return {};
    return m_files.at(m_currentIndex);
}

QString Backend::fileName() const {
    const QString path = filePath();
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}

QVariantList Backend::fileItems() const {
    QVariantList items;
    items.reserve(m_files.size());
    for (int i = 0; i < m_files.size(); ++i) {
        QVariantMap row;
        row.insert("index", i);
        row.insert("filePath", m_files.at(i));
        row.insert("fileName", QFileInfo(m_files.at(i)).fileName());
        row.insert("selected", m_selectedIndices.contains(i));
        items.append(row);
    }
    return items;
}

void Backend::setExportFormat(const QString &format) {
    const QString norm = ImageOps::normalizedFormat(format);
    if (norm == m_exportFormat)
        return;
    m_exportFormat = norm;
    m_hasSavedExportFormat = true;
    m_presetStore.setExportDefaults(m_exportFormat, m_exportQuality);
    m_presetStore.save();
    refreshExportEstimate();
}

void Backend::setExportQuality(int quality) {
    const int clamped = std::clamp(quality, 10, 100);
    if (clamped == m_exportQuality)
        return;
    m_exportQuality = clamped;
    m_presetStore.setExportDefaults(m_exportFormat, m_exportQuality);
    m_presetStore.save();
    refreshExportEstimate();
}

int Backend::selectionPixelWidth() const {
    return selectionPixelRect().width();
}

int Backend::selectionPixelHeight() const {
    return selectionPixelRect().height();
}

void Backend::setPrintSelectionOnly(bool enabled) {
    const bool next = enabled && hasSelection();
    if (next == m_printSelectionOnly)
        return;
    m_printSelectionOnly = next;
    clampPreviewPage();
    emit printSettingsChanged();
}

QImage Backend::thumbnailForIndex(int index) const {
    if (index < 0 || index >= m_files.size())
        return {};
    if (m_thumbCache.contains(index))
        return m_thumbCache.value(index);

    const QString path = m_files.at(index);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize orig = reader.size();
    if (orig.isValid() && !orig.isEmpty()) {
        const QSize scaled = orig.scaled(220, 220, Qt::KeepAspectRatio);
        if (scaled.isValid() && !scaled.isEmpty())
            reader.setScaledSize(scaled);
    }
    QImage thumb = reader.read();
    if (!thumb.isNull() && (thumb.width() > 220 || thumb.height() > 220))
        thumb = thumb.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_thumbCache.insert(index, thumb);
    return thumb;
}

QImage Backend::printableImage() const {
    QImage img = m_displayImage;
    if (m_printSelectionOnly && hasSelection() && !img.isNull()) {
        const QRect crop = selectionPixelRect().intersected(img.rect());
        if (crop.isValid() && crop.width() >= 2 && crop.height() >= 2)
            img = img.copy(crop);
    }
    if (m_printSettings.grayscale && !img.isNull())
        img = img.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
    return img;
}

QVector<int> Backend::activeBatchIndices() const {
    QVector<int> indices;
    if (!m_selectedIndices.isEmpty()) {
        for (int idx : m_selectedIndices) {
            if (idx >= 0 && idx < m_files.size())
                indices.append(idx);
        }
        std::sort(indices.begin(), indices.end());
    } else {
        indices.reserve(m_files.size());
        for (int i = 0; i < m_files.size(); ++i)
            indices.append(i);
    }
    return indices;
}

QVector<QVector<QImage>> Backend::buildPrintPages() const {
    if (!hasImage())
        return {};

    if (m_multiMode == "selected" && m_files.size() > 1) {
        const QVector<int> indices = activeBatchIndices();
        QVector<QImage> batch;
        batch.reserve(indices.size());
        for (int idx : indices) {
            if (idx == m_currentIndex) {
                batch.append(printableImage());
                continue;
            }
            const QString path = m_files.at(idx);
            QImage raw;
            if (m_batchImageCache.contains(path)) {
                raw = m_batchImageCache.value(path);
            } else {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                raw = reader.read();
                if (!raw.isNull())
                    m_batchImageCache.insert(path, raw);
            }
            if (!raw.isNull()) {
                batch.append(m_printSettings.grayscale
                                 ? PrintRenderer::applyTransforms(raw, 0, false, false, true)
                                 : raw);
            }
        }
        if (!batch.isEmpty())
            return PrintRenderer::paginateImages(batch, m_printSettings.perPage, false);
    }

    return PrintRenderer::paginateImages({printableImage()}, m_printSettings.perPage, true);
}

QVector<QImage> Backend::previewPageImages() const {
    const QVector<QVector<QImage>> pages = buildPrintPages();
    if (pages.isEmpty())
        return {};
    const int idx = std::clamp(m_previewPageIndex, 0, static_cast<int>(pages.size()) - 1);
    return pages.at(idx);
}

void Backend::setPrinter(const QString &name) {
    if (name == m_selectedPrinter)
        return;
    m_selectedPrinter = name;
    m_preferredPrinter = name;
    clearPrinterOptions();
    refreshPrinterOptions();
    persistLastUsed(true);
    emit printSettingsChanged();
}

bool Backend::hasPrinterOptions() const {
    return !m_mediaTypeOpt.values.isEmpty() || !m_qualityOpt.values.isEmpty() || !m_duplexOpt.values.isEmpty();
}

QVariantList Backend::mediaTypeOptions() const {
    return toOptionModel(m_mediaTypeOpt);
}

void Backend::setMediaType(const QString &value) {
    if (value == m_printSettings.mediaTypeValue)
        return;
    m_printSettings.mediaTypeValue = value;
    persistLastUsed(true);
    emit printSettingsChanged();
}

QVariantList Backend::qualityOptions() const {
    return toOptionModel(m_qualityOpt);
}

void Backend::setPrintQuality(const QString &value) {
    if (value == m_printSettings.qualityValue)
        return;
    m_printSettings.qualityValue = value;
    persistLastUsed(true);
    emit printSettingsChanged();
}

QVariantList Backend::duplexOptions() const {
    return toOptionModel(m_duplexOpt);
}

void Backend::setDuplex(const QString &value) {
    if (value == m_printSettings.duplexValue)
        return;
    m_printSettings.duplexValue = value;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setCopies(int count) {
    const int clamped = std::clamp(count, 1, 99);
    if (clamped == m_printSettings.copies)
        return;
    m_printSettings.copies = clamped;
    persistLastUsed(false);
    emit printSettingsChanged();
}

void Backend::setPaperSize(const QString &id) {
    if (id.startsWith("custom:")) {
        CustomPaperSize custom;
        if (!m_presetStore.findCustomPaperById(id, custom))
            return;
        if (m_printSettings.paperSize == custom.id &&
            std::abs(m_printSettings.customPaperWidthMm - custom.widthMm) < 1e-4 &&
            std::abs(m_printSettings.customPaperHeightMm - custom.heightMm) < 1e-4)
            return;
        m_printSettings.paperSize = custom.id;
        m_printSettings.customPaperWidthMm = custom.widthMm;
        m_printSettings.customPaperHeightMm = custom.heightMm;
        persistLastUsed(true);
        emit printSettingsChanged();
        return;
    }

    const QString normalized = PrintRenderer::paperSpec(id).id;
    if (normalized == m_printSettings.paperSize &&
        m_printSettings.customPaperWidthMm == 0.0 &&
        m_printSettings.customPaperHeightMm == 0.0)
        return;
    m_printSettings.paperSize = normalized;
    m_printSettings.customPaperWidthMm = 0.0;
    m_printSettings.customPaperHeightMm = 0.0;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setOrientation(const QString &mode) {
    const QString normalized = (mode == "portrait" || mode == "landscape") ? mode : QStringLiteral("auto");
    if (normalized == m_printSettings.orientation)
        return;
    m_printSettings.orientation = normalized;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setScaleMode(const QString &mode) {
    const QString normalized = (mode == "fill" || mode == "scale") ? mode : QStringLiteral("fit");
    if (normalized == m_printSettings.scaleMode)
        return;
    m_printSettings.scaleMode = normalized;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setScalePercent(int percent) {
    const int clamped = std::clamp(percent, 10, 200);
    if (clamped == m_printSettings.scalePercent)
        return;
    m_printSettings.scalePercent = clamped;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setMarginMode(const QString &mode) {
    const QString normalized = (mode == "narrow" || mode == "borderless") ? mode : QStringLiteral("standard");
    if (normalized == m_printSettings.marginMode)
        return;
    m_printSettings.marginMode = normalized;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setGrayscale(bool enabled) {
    if (enabled == m_printSettings.grayscale)
        return;
    m_printSettings.grayscale = enabled;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setPerPage(int count) {
    const int normalized = PrintRenderer::normalizePerPage(count);
    if (normalized == m_printSettings.perPage)
        return;
    m_printSettings.perPage = normalized;
    clampPreviewPage();
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setMultiMode(const QString &mode) {
    const QString normalized = (mode == "selected") ? QStringLiteral("selected") : QStringLiteral("repeat");
    if (normalized == m_multiMode)
        return;
    m_multiMode = normalized;
    m_previewPageIndex = 0;
    clampPreviewPage();
    emit printSettingsChanged();
}

void Backend::setCutGuides(bool enabled) {
    if (enabled == m_printSettings.cutGuides)
        return;
    m_printSettings.cutGuides = enabled;
    persistLastUsed(true);
    emit printSettingsChanged();
}

void Backend::setPreviewPageIndex(int index) {
    const int total = std::max(1, previewPageCount());
    const int clamped = std::clamp(index, 0, total - 1);
    if (clamped == m_previewPageIndex)
        return;
    m_previewPageIndex = clamped;
    emit printSettingsChanged();
}

int Backend::previewPageCount() const {
    if (!hasImage())
        return 0;
    if (m_multiMode == "selected" && m_files.size() > 1) {
        const int itemCount = std::max(1, static_cast<int>(activeBatchIndices().size()));
        const int n = PrintRenderer::normalizePerPage(m_printSettings.perPage);
        return (itemCount + n - 1) / n;
    }
    return 1;
}

bool Backend::effectiveLandscape() const {
    const QVector<QImage> slotImages = previewPageImages();
    const QSize refSize = slotImages.isEmpty() ? m_displayImage.size() : slotImages.first().size();
    return PrintRenderer::resolveLandscape(m_printSettings, refSize);
}

double Backend::paperWidthMm() const {
    const QVector<QImage> slotImages = previewPageImages();
    const QSize refSize = slotImages.isEmpty() ? m_displayImage.size() : slotImages.first().size();
    return PrintRenderer::effectivePageSizeMm(m_printSettings, refSize).width();
}

double Backend::paperHeightMm() const {
    const QVector<QImage> slotImages = previewPageImages();
    const QSize refSize = slotImages.isEmpty() ? m_displayImage.size() : slotImages.first().size();
    return PrintRenderer::effectivePageSizeMm(m_printSettings, refSize).height();
}

bool Backend::isSupportedImageFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;
    return supportedExtensions().contains(info.suffix().toLower());
}

bool Backend::openFiles(const QStringList &paths) {
    clearMessages();

    QStringList resolved;
    for (const QString &raw : paths) {
        const QFileInfo info(raw);
        if (!info.exists()) {
            setError(tr("File not found: %1").arg(raw));
            continue;
        }
        if (info.isDir()) {
            QDir dir(info.absoluteFilePath());
            for (const QFileInfo &entry : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort)) {
                if (isSupportedImageFile(entry.absoluteFilePath()))
                    resolved.append(entry.absoluteFilePath());
            }
        } else {
            resolved.append(info.absoluteFilePath());
        }
    }

    if (resolved.isEmpty()) {
        if (m_errorMessage.isEmpty())
            setError(tr("No supported image files found."));
        return false;
    }

    int startIndex = 0;
    if (resolved.size() == 1) {
        const QFileInfo single(resolved.first());
        const QString targetPath = single.absoluteFilePath();
        QDir dir = single.dir();
        QStringList siblings;
        for (const QFileInfo &entry : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort)) {
            if (isSupportedImageFile(entry.absoluteFilePath()) || entry.absoluteFilePath() == targetPath)
                siblings.append(entry.absoluteFilePath());
        }

        QCollator collator;
        collator.setNumericMode(true);
        std::sort(siblings.begin(), siblings.end(),
                  [&collator](const QString &a, const QString &b) {
                      return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
                  });

        if (!siblings.contains(targetPath))
            siblings.prepend(targetPath);

        m_files = siblings;
        startIndex = m_files.indexOf(targetPath);
    } else {
        resolved.removeDuplicates();
        m_files = resolved;
    }

    m_selectedIndices.clear();
    m_thumbCache.clear();
    m_batchImageCache.clear();
    ++m_fileListVersion;
    emit filesChanged();

    return loadIndex(startIndex);
}

bool Backend::openUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls) {
        const QString local = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!local.isEmpty())
            paths.append(local);
    }
    return openFiles(paths);
}

void Backend::openFileDialog() {
    if (!m_filePicker) {
        setError(tr("File picker is unavailable."));
        return;
    }
    m_filePicker->openImages(
        tr("Open Image"),
        [this](const QStringList &paths) { openFiles(paths); },
        [this](const QString &error) { setError(error); });
}

void Backend::selectIndex(int index) {
    if (index < 0 || index >= m_files.size() || index == m_currentIndex)
        return;
    loadIndex(index);
}

void Backend::nextImage() {
    if (m_files.size() < 2)
        return;
    loadIndex((m_currentIndex + 1) % m_files.size());
}

void Backend::previousImage() {
    if (m_files.size() < 2)
        return;
    loadIndex((m_currentIndex - 1 + m_files.size()) % m_files.size());
}

void Backend::toggleFileSelected(int index) {
    if (index < 0 || index >= m_files.size())
        return;
    if (m_selectedIndices.contains(index))
        m_selectedIndices.remove(index);
    else
        m_selectedIndices.insert(index);
    clampPreviewPage();
    emit filesChanged();
    emit printSettingsChanged();
}

void Backend::selectRangeTo(int index) {
    if (index < 0 || index >= m_files.size())
        return;
    const int anchor = m_currentIndex >= 0 ? m_currentIndex : 0;
    const int lo = std::min(anchor, index);
    const int hi = std::max(anchor, index);
    for (int i = lo; i <= hi; ++i)
        m_selectedIndices.insert(i);
    clampPreviewPage();
    emit filesChanged();
    emit printSettingsChanged();
}

void Backend::selectAllFiles() {
    if (m_files.isEmpty())
        return;
    for (int i = 0; i < m_files.size(); ++i)
        m_selectedIndices.insert(i);
    clampPreviewPage();
    emit filesChanged();
    emit printSettingsChanged();
}

void Backend::clearFileSelection() {
    if (m_selectedIndices.isEmpty())
        return;
    m_selectedIndices.clear();
    clampPreviewPage();
    emit filesChanged();
    emit printSettingsChanged();
}

void Backend::setSelectionRect(double x, double y, double w, double h) {
    if (!hasImage())
        return;
    const double x1 = std::clamp(x, 0.0, 1.0);
    const double y1 = std::clamp(y, 0.0, 1.0);
    const double x2 = std::clamp(x + w, 0.0, 1.0);
    const double y2 = std::clamp(y + h, 0.0, 1.0);
    const QRectF norm(std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1), std::abs(y2 - y1));

    const int pxW = static_cast<int>(std::round(norm.width() * m_displayImage.width()));
    const int pxH = static_cast<int>(std::round(norm.height() * m_displayImage.height()));
    if (pxW < 8 || pxH < 8) {
        clearSelection();
        return;
    }

    m_selectionNorm = norm;
    m_printSelectionOnly = true;
    emit selectionChanged();
    emit printSettingsChanged();
}

void Backend::clearSelection() {
    if (m_selectionNorm.isEmpty() && !m_printSelectionOnly)
        return;
    m_selectionNorm = QRectF();
    m_printSelectionOnly = false;
    emit selectionChanged();
    emit printSettingsChanged();
}

void Backend::cropToSelection() {
    if (!hasImage() || !hasSelection())
        return;

    // Map normalized selection back into post-rotate/flip coordinates (before
    // any resize), composing with an existing cropRect if already cropped.
    const QRect baseRect = m_edit.cropRect.isEmpty()
                               ? QRect(QPoint(0, 0), m_preResizeSize)
                               : m_edit.cropRect;
    const int cx = baseRect.x() + static_cast<int>(std::round(m_selectionNorm.x() * baseRect.width()));
    const int cy = baseRect.y() + static_cast<int>(std::round(m_selectionNorm.y() * baseRect.height()));
    const int cw = std::max(8, static_cast<int>(std::round(m_selectionNorm.width() * baseRect.width())));
    const int ch = std::max(8, static_cast<int>(std::round(m_selectionNorm.height() * baseRect.height())));

    EditState next = m_edit;
    next.cropRect = QRect(cx, cy, cw, ch).intersected(baseRect);
    next.resizeTarget = QSize();
    clearSelection();
    applyEditState(next, true);
}

void Backend::copyToClipboard() {
    if (!hasImage())
        return;
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip) {
        setError(tr("Clipboard is unavailable."));
        return;
    }

    if (hasSelection()) {
        const QRect crop = selectionPixelRect().intersected(m_displayImage.rect());
        if (crop.isValid() && crop.width() >= 2 && crop.height() >= 2) {
            clip->setImage(m_displayImage.copy(crop));
            setStatus(tr("Copied selection to clipboard"));
            return;
        }
    }

    clip->setImage(m_displayImage);
    setStatus(tr("Copied image to clipboard"));
}

void Backend::applyResize(int width, int height) {
    if (!hasImage())
        return;
    const int w = std::clamp(width, 1, 16384);
    const int h = std::clamp(height, 1, 16384);

    EditState next = m_edit;
    if (QSize(w, h) == m_preResizeSize)
        next.resizeTarget = QSize();
    else
        next.resizeTarget = QSize(w, h);

    clearSelection();
    applyEditState(next, true);
}

void Backend::resetResize() {
    if (!hasImage() || m_edit.resizeTarget.isEmpty())
        return;
    EditState next = m_edit;
    next.resizeTarget = QSize();
    clearSelection();
    applyEditState(next, true);
}

void Backend::beginColorEdit() {
    if (!hasImage() || m_colorEditing)
        return;
    m_colorSnapshot = m_edit;
    m_colorEditing = true;
}

void Backend::setColorAdjustments(double exposure, double contrast, double saturation, double brightness) {
    if (!hasImage())
        return;
    if (!m_colorEditing)
        beginColorEdit();

    ColorAdjustments next;
    next.exposure = std::clamp(exposure, -1.0, 1.0);
    next.contrast = std::clamp(contrast, -1.0, 1.0);
    next.saturation = std::clamp(saturation, -1.0, 1.0);
    next.brightness = std::clamp(brightness, -1.0, 1.0);

    if (next == m_edit.color)
        return;

    m_edit.color = next;
    ++m_editVersion;
    rebuildDisplayImageFromPreColor();
    emit colorChanged();
    emit printSettingsChanged();
}

void Backend::commitColorEdit() {
    if (!m_colorEditing)
        return;
    m_colorEditing = false;
    if (!(m_edit == m_colorSnapshot)) {
        m_undoStack.push_back(m_colorSnapshot);
        m_redoStack.clear();
        emit imageChanged();
    }
}

void Backend::autoLevels() {
    if (!hasImage())
        return;
    commitColorEdit();
    EditState next = m_edit;
    next.color = ImageOps::computeAutoLevels(m_preColorImage);
    applyEditState(next, true);
}

void Backend::resetColorAdjustments() {
    if (!hasImage())
        return;
    commitColorEdit();
    if (m_edit.color.isIdentity())
        return;
    EditState next = m_edit;
    next.color = ColorAdjustments{};
    applyEditState(next, true);
}

void Backend::commitDraftAnnotation(QObject *draftObj) {
    auto *draft = qobject_cast<MarkupDraftItem *>(draftObj);
    if (!draft || !hasImage() || !draft->active())
        return;

    const Annotation ann = draft->currentAnnotation();
    draft->clear();

    // Ignore accidental tiny taps except for pen dots.
    if (ann.tool != "pen") {
        const double dx = std::abs(ann.p2.x() - ann.p1.x()) * m_displayImage.width();
        const double dy = std::abs(ann.p2.y() - ann.p1.y()) * m_displayImage.height();
        if (std::hypot(dx, dy) < 6.0)
            return;
    } else if (ann.points.isEmpty()) {
        return;
    }

    commitColorEdit();
    EditState next = m_edit;
    next.annotations.append(ann);
    applyEditState(next, true);
}

void Backend::addTextAnnotation(double nx, double ny, const QString &text, const QColor &color, const QString &strokeSize) {
    if (!hasImage() || text.trimmed().isEmpty())
        return;
    commitColorEdit();

    Annotation ann;
    ann.tool = "text";
    ann.p1 = QPointF(std::clamp(nx, 0.0, 1.0), std::clamp(ny, 0.0, 1.0));
    ann.p2 = ann.p1;
    ann.text = text.trimmed();
    ann.color = color.isValid() ? color : QColor("#ff453a");
    ann.strokeFactor = Markup::strokeFactorForPreset(strokeSize);

    EditState next = m_edit;
    next.annotations.append(ann);
    applyEditState(next, true);
}

void Backend::clearAnnotations() {
    if (!hasImage() || m_edit.annotations.isEmpty())
        return;
    commitColorEdit();
    EditState next = m_edit;
    next.annotations.clear();
    applyEditState(next, true);
}

void Backend::rotateLeft() {
    if (!hasImage())
        return;
    commitColorEdit();
    clearSelection();
    EditState next = m_edit;
    next.rotation = (next.rotation + 270) % 360;
    next.cropRect = QRect();
    next.resizeTarget = QSize();
    applyEditState(next, true);
}

void Backend::rotateRight() {
    if (!hasImage())
        return;
    commitColorEdit();
    clearSelection();
    EditState next = m_edit;
    next.rotation = (next.rotation + 90) % 360;
    next.cropRect = QRect();
    next.resizeTarget = QSize();
    applyEditState(next, true);
}

void Backend::toggleFlipHorizontal() {
    if (!hasImage())
        return;
    commitColorEdit();
    clearSelection();
    EditState next = m_edit;
    next.flipH = !next.flipH;
    next.cropRect = QRect();
    applyEditState(next, true);
}

void Backend::toggleFlipVertical() {
    if (!hasImage())
        return;
    commitColorEdit();
    clearSelection();
    EditState next = m_edit;
    next.flipV = !next.flipV;
    next.cropRect = QRect();
    applyEditState(next, true);
}

void Backend::resetTransforms() {
    if (!hasImage() || !isTransformed())
        return;
    commitColorEdit();
    clearSelection();
    applyEditState(EditState{}, true);
}

void Backend::undo() {
    commitColorEdit();
    if (m_undoStack.isEmpty())
        return;
    clearSelection();
    m_redoStack.push_back(m_edit);
    const EditState previous = m_undoStack.takeLast();
    applyEditState(previous, false);
}

void Backend::redo() {
    commitColorEdit();
    if (m_redoStack.isEmpty())
        return;
    clearSelection();
    m_undoStack.push_back(m_edit);
    const EditState next = m_redoStack.takeLast();
    applyEditState(next, false);
}

void Backend::refreshExportEstimate() {
    if (!hasImage()) {
        m_estimatedExportBytes = 0;
    } else {
        m_estimatedExportBytes = ImageOps::estimateEncodedSize(m_displayImage, m_exportFormat, m_exportQuality);
    }
    emit exportSettingsChanged();
}

void Backend::exportImageDialog() {
    if (!hasImage()) {
        setError(tr("Open an image before exporting."));
        return;
    }
    if (!m_filePicker) {
        setError(tr("File picker is unavailable."));
        return;
    }

    const QFileInfo info(filePath());
    const QString ext = ImageOps::extensionForFormat(m_exportFormat);
    const QString suffix = isTransformed() ? "-edited" : "-copy";
    const QString suggested = info.completeBaseName() + suffix + "." + ext;

    m_filePicker->saveFile(
        tr("Export Image"),
        suggested,
        info.absolutePath(),
        formatFilterLabel(m_exportFormat),
        ImageOps::mimeTypeForFormat(m_exportFormat),
        [this](const QString &path) { exportImageTo(path); },
        [this](const QString &error) { setError(error); });
}

bool Backend::exportImageTo(const QString &outputPath) {
    clearMessages();
    if (!hasImage()) {
        setError(tr("Open an image before exporting."));
        return false;
    }

    commitColorEdit();

    const QString norm = ImageOps::normalizedFormat(m_exportFormat);
    const QString ext = "." + ImageOps::extensionForFormat(norm);
    QString finalPath = outputPath;
    if (QFileInfo(finalPath).suffix().isEmpty())
        finalPath += ext;

    QString err;
    bool ok = false;
    if (norm == "pdf") {
        ok = PrintRenderer::exportPdf(finalPath, buildPrintPages(), m_printSettings, &err);
    } else {
        ok = ImageOps::exportImageAtomic(m_displayImage, finalPath, norm, m_exportQuality, &err);
    }

    if (!ok) {
        setError(err);
        return false;
    }

    m_exportedEditVersion = m_editVersion;
    emit imageChanged();
    setStatus(tr("Exported %1").arg(QFileInfo(finalPath).fileName()));
    return true;
}

void Backend::preparePrintSheet() {
    commitColorEdit();
    refreshPrinters();
    if (m_selectedIndices.size() > 1)
        m_multiMode = "selected";
    else if (m_selectedIndices.isEmpty() && m_multiMode == "selected" && m_files.size() <= 1)
        m_multiMode = "repeat";
    m_previewPageIndex = 0;
    clampPreviewPage();
    emit printSettingsChanged();
}

void Backend::nextPreviewPage() {
    setPreviewPageIndex(m_previewPageIndex + 1);
}

void Backend::previousPreviewPage() {
    setPreviewPageIndex(m_previewPageIndex - 1);
}

void Backend::refreshPrinters() {
    if (m_lpstatPath.isEmpty()) {
        m_printers.clear();
        m_defaultPrinter.clear();
        m_selectedPrinter.clear();
        clearPrinterOptions();
        m_printersError = tr("CUPS (lpstat) is not installed.");
        emit printersChanged();
        emit printSettingsChanged();
        return;
    }

    if (m_lpstatProcess.state() != QProcess::NotRunning)
        return;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    m_lpstatProcess.setProcessEnvironment(env);
    m_lpstatProcess.start(m_lpstatPath, {"-p", "-d"});
}

void Backend::printCurrent() {
    clearMessages();

    if (!hasImage()) {
        setError(tr("Open an image before printing."));
        return;
    }
    if (m_lpPath.isEmpty()) {
        setError(tr("CUPS (lp) is not installed, so Peek cannot send jobs to a printer."));
        return;
    }
    if (m_selectedPrinter.isEmpty()) {
        setError(tr("No printer selected. Check your CUPS printer setup."));
        return;
    }
    if (m_printBusy)
        return;

    const QVector<QVector<QImage>> pages = buildPrintPages();
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString tempPdf = QDir(tempDir).filePath(
        "peek-print-" + QUuid::createUuid().toString(QUuid::Id128) + ".pdf");

    QString exportError;
    if (!PrintRenderer::exportPdf(tempPdf, pages, m_printSettings, &exportError)) {
        setError(exportError);
        return;
    }

    m_activeTempPdf = tempPdf;
    m_printBusy = true;
    emit printStateChanged();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    m_lpProcess.setProcessEnvironment(env);
    m_lpProcess.start(m_lpPath, buildLpArguments(m_selectedPrinter, m_printSettings, tempPdf, fileName()));
}

void Backend::savePdfDialog() {
    if (!hasImage()) {
        setError(tr("Open an image before saving a PDF."));
        return;
    }
    if (!m_filePicker) {
        setError(tr("File picker is unavailable."));
        return;
    }

    const QFileInfo info(filePath());
    const QString suggested = info.completeBaseName() + ".pdf";
    m_filePicker->savePdf(
        tr("Save as PDF"),
        suggested,
        info.absolutePath(),
        [this](const QString &path) { savePdfTo(path); },
        [this](const QString &error) { setError(error); });
}

bool Backend::savePdfTo(const QString &outputPath) {
    clearMessages();
    if (!hasImage()) {
        setError(tr("Open an image before saving a PDF."));
        return false;
    }

    QString finalPath = outputPath;
    if (!finalPath.endsWith(".pdf", Qt::CaseInsensitive))
        finalPath += ".pdf";

    QString err;
    if (!PrintRenderer::exportPdf(finalPath, buildPrintPages(), m_printSettings, &err)) {
        setError(err);
        return false;
    }

    setStatus(tr("Saved PDF to %1").arg(QFileInfo(finalPath).fileName()));
    return true;
}

void Backend::clearMessages() {
    if (m_statusMessage.isEmpty() && m_errorMessage.isEmpty())
        return;
    m_statusMessage.clear();
    m_errorMessage.clear();
    emit messageChanged();
}

QVariantList Backend::presets() const {
    QVariantList out;
    for (const auto &p : m_presetStore.allPresets()) {
        QVariantMap row;
        row.insert("id", p.id);
        row.insert("name", p.name);
        row.insert("builtIn", p.builtIn);
        row.insert("removable", p.removable);
        out.append(row);
    }
    return out;
}

QString Backend::activePresetName() const {
    PrintPreset found;
    if (m_presetStore.findById(m_activePresetId, found))
        return found.name;
    return "Last Used Settings";
}

bool Backend::activePresetRemovable() const {
    PrintPreset found;
    return m_presetStore.findById(m_activePresetId, found) && found.removable;
}

void Backend::applyPreset(const QString &id) {
    PrintPreset chosen;
    if (!m_presetStore.findById(id, chosen))
        return;

    const QString savedMediaKey = m_printSettings.mediaTypeKey;
    const QString savedQualityKey = m_printSettings.qualityKey;
    const QString savedDuplexKey = m_printSettings.duplexKey;

    m_printSettings = chosen.settings;
    // Keep discovered CUPS option keys and fall back to printer defaults when a
    // preset doesn't specify a printer-specific PPD override.
    if (m_printSettings.mediaTypeKey.isEmpty())
        m_printSettings.mediaTypeKey = savedMediaKey;
    if (!m_mediaTypeOpt.values.contains(m_printSettings.mediaTypeValue))
        m_printSettings.mediaTypeValue = m_mediaTypeOpt.defaultValue;

    if (m_printSettings.qualityKey.isEmpty())
        m_printSettings.qualityKey = savedQualityKey;
    if (!m_qualityOpt.values.contains(m_printSettings.qualityValue))
        m_printSettings.qualityValue = m_qualityOpt.defaultValue;

    if (m_printSettings.duplexKey.isEmpty())
        m_printSettings.duplexKey = savedDuplexKey;
    if (!m_duplexOpt.values.contains(m_printSettings.duplexValue))
        m_printSettings.duplexValue = m_duplexOpt.defaultValue;

    applyBuiltInCupsHints(id);

    if (!chosen.printer.isEmpty() && m_printers.contains(chosen.printer) && chosen.printer != m_selectedPrinter) {
        m_selectedPrinter = chosen.printer;
        m_preferredPrinter = chosen.printer;
        refreshPrinterOptions();
    }

    m_activePresetId = chosen.id;
    m_activePresetModified = false;
    m_presetStore.setActivePresetId(m_activePresetId);
    m_presetStore.setLastUsed(m_printSettings, m_selectedPrinter);
    m_presetStore.save();

    clampPreviewPage();
    emit presetsChanged();
    emit printSettingsChanged();
}

bool Backend::saveCurrentPreset(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(tr("Enter a name for the preset."));
        return false;
    }

    const QString newId = m_presetStore.saveCustomPreset(trimmed, m_printSettings, m_selectedPrinter);
    if (newId.isEmpty()) {
        setError(tr("Could not save preset."));
        return false;
    }

    m_activePresetId = newId;
    m_activePresetModified = false;
    m_presetStore.setLastUsed(m_printSettings, m_selectedPrinter);
    m_presetStore.save();

    emit presetsChanged();
    setStatus(tr("Saved preset \"%1\"").arg(trimmed));
    return true;
}

bool Backend::deleteActivePreset() {
    if (!activePresetRemovable())
        return false;
    const QString removedName = activePresetName();
    if (!m_presetStore.deleteCustomPreset(m_activePresetId))
        return false;

    m_activePresetId = "last-used";
    m_activePresetModified = false;
    m_presetStore.setActivePresetId(m_activePresetId);
    m_presetStore.save();

    emit presetsChanged();
    setStatus(tr("Deleted preset \"%1\"").arg(removedName));
    return true;
}

QVariantList Backend::paperOptions() const {
    QVariantList out;
    for (const auto &spec : PrintRenderer::allPapers()) {
        QVariantMap row;
        row.insert("id", spec.id);
        row.insert("name", spec.id);
        row.insert("widthMm", spec.widthMm);
        row.insert("heightMm", spec.heightMm);
        row.insert("custom", false);
        out.append(row);
    }
    for (const auto &custom : m_presetStore.customPapers()) {
        QVariantMap row;
        row.insert("id", custom.id);
        row.insert("name", custom.name);
        row.insert("widthMm", custom.widthMm);
        row.insert("heightMm", custom.heightMm);
        row.insert("custom", true);
        out.append(row);
    }
    return out;
}

bool Backend::activePaperCustom() const {
    CustomPaperSize found;
    return m_printSettings.paperSize.startsWith("custom:") &&
           m_presetStore.findCustomPaperById(m_printSettings.paperSize, found);
}

QString Backend::activePaperName() const {
    CustomPaperSize found;
    if (m_presetStore.findCustomPaperById(m_printSettings.paperSize, found))
        return found.name;
    return {};
}

double Backend::customPaperWidthMm() const {
    return PrintRenderer::resolvePaperSpec(m_printSettings).widthMm;
}

double Backend::customPaperHeightMm() const {
    return PrintRenderer::resolvePaperSpec(m_printSettings).heightMm;
}

bool Backend::saveCustomPaper(const QString &name, double width, double height, const QString &unit) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(tr("Enter a name for the custom paper size."));
        return false;
    }

    double scale = 1.0;
    if (unit == "cm") scale = 10.0;
    else if (unit == "in") scale = 25.4;

    const double wMm = width * scale;
    const double hMm = height * scale;
    if (wMm < 10.0 || hMm < 10.0 || wMm > 2000.0 || hMm > 2000.0) {
        setError(tr("Paper dimensions must be between 10 mm and 2000 mm."));
        return false;
    }

    const QString id = m_presetStore.saveCustomPaper(trimmed, wMm, hMm);
    if (id.isEmpty()) {
        setError(tr("Could not save custom paper size."));
        return false;
    }

    m_printSettings.paperSize = id;
    m_printSettings.customPaperWidthMm = wMm;
    m_printSettings.customPaperHeightMm = hMm;
    persistLastUsed(true);

    emit papersChanged();
    emit printSettingsChanged();
    setStatus(tr("Saved paper size \"%1\"").arg(trimmed));
    return true;
}

bool Backend::deleteActiveCustomPaper() {
    if (!activePaperCustom())
        return false;
    const QString removedName = activePaperName();
    if (!m_presetStore.deleteCustomPaper(m_printSettings.paperSize))
        return false;

    m_printSettings.paperSize = "a4";
    m_printSettings.customPaperWidthMm = 0.0;
    m_printSettings.customPaperHeightMm = 0.0;
    persistLastUsed(true);

    emit papersChanged();
    emit printSettingsChanged();
    setStatus(tr("Deleted paper size \"%1\"").arg(removedName));
    return true;
}

void Backend::persistLastUsed(bool markPresetModified) {
    m_presetStore.setLastUsed(m_printSettings, m_selectedPrinter);
    if (markPresetModified && m_activePresetId != "last-used" && !m_activePresetModified) {
        m_activePresetModified = true;
        emit presetsChanged();
    }
    m_presetStore.save();
}

void Backend::applyBuiltInCupsHints(const QString &presetId) {
    auto findMatch = [](const QStringList &values, std::initializer_list<const char *> needles) -> QString {
        for (const char *needle : needles) {
            for (const QString &v : values) {
                if (v.contains(QLatin1String(needle), Qt::CaseInsensitive))
                    return v;
            }
        }
        return {};
    };

    if (presetId == "builtin:photo-4x6") {
        const QString glossy = findMatch(m_mediaTypeOpt.values, {"gloss", "photo"});
        if (!glossy.isEmpty())
            m_printSettings.mediaTypeValue = glossy;
        const QString high = findMatch(m_qualityOpt.values, {"high", "best", "photo"});
        if (!high.isEmpty())
            m_printSettings.qualityValue = high;
    } else if (presetId == "builtin:bw-draft") {
        const QString plain = findMatch(m_mediaTypeOpt.values, {"plain", "stationery"});
        if (!plain.isEmpty())
            m_printSettings.mediaTypeValue = plain;
        const QString draft = findMatch(m_qualityOpt.values, {"draft", "fast"});
        if (!draft.isEmpty())
            m_printSettings.qualityValue = draft;
    }
}

QStringList Backend::buildLpArguments(const QString &printer,
                                      const PrintSettings &settings,
                                      const QString &pdfPath,
                                      const QString &jobTitle) {
    const PaperSpec spec = PrintRenderer::resolvePaperSpec(settings);
    QStringList args;
    if (!printer.isEmpty())
        args << "-d" << printer;
    args << "-n" << QString::number(std::clamp(settings.copies, 1, 99));
    if (!jobTitle.isEmpty())
        args << "-t" << jobTitle;
    args << "-o" << ("media=" + spec.cupsMedia);
    args << "-o" << "print-scaling=none";
    if (settings.grayscale)
        args << "-o" << "ColorModel=Gray";
    if (settings.marginMode == "borderless")
        args << "-o" << "Borderless=On";
    if (!settings.mediaTypeKey.isEmpty() && !settings.mediaTypeValue.isEmpty())
        args << "-o" << (settings.mediaTypeKey + "=" + settings.mediaTypeValue);
    if (!settings.qualityKey.isEmpty() && !settings.qualityValue.isEmpty())
        args << "-o" << (settings.qualityKey + "=" + settings.qualityValue);
    if (!settings.duplexKey.isEmpty() && !settings.duplexValue.isEmpty())
        args << "-o" << (settings.duplexKey + "=" + settings.duplexValue);
    args << "--" << pdfPath;
    return args;
}

void Backend::parseLpstatOutput(const QString &output, QStringList &printers, QString &defaultPrinter) {
    printers.clear();
    defaultPrinter.clear();

    static const QRegularExpression printerRe("^printer\\s+(\\S+)\\s+");
    static const QRegularExpression defaultRe("^system default destination:\\s+(\\S+)");

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const auto pm = printerRe.match(line);
        if (pm.hasMatch()) {
            const QString name = pm.captured(1);
            if (!printers.contains(name))
                printers.append(name);
            continue;
        }
        const auto dm = defaultRe.match(line);
        if (dm.hasMatch())
            defaultPrinter = dm.captured(1);
    }
}

void Backend::parseLpoptionsOutput(const QString &output, QHash<QString, CupsOption> &options) {
    options.clear();
    static const QRegularExpression lineRe("^([^/:\\s]+)(?:/[^:]+)?:\\s*(.+)$");

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const auto match = lineRe.match(rawLine.trimmed());
        if (!match.hasMatch())
            continue;

        CupsOption opt;
        opt.key = match.captured(1).trimmed();
        const QStringList tokens = match.captured(2).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        for (QString tok : tokens) {
            const bool isDefault = tok.startsWith('*');
            if (isDefault)
                tok.remove(0, 1);
            if (tok.isEmpty() || tok.startsWith("Custom."))
                continue;
            opt.values.append(tok);
            if (isDefault)
                opt.defaultValue = tok;
        }
        if (opt.defaultValue.isEmpty() && !opt.values.isEmpty())
            opt.defaultValue = opt.values.first();
        if (!opt.values.isEmpty())
            options.insert(opt.key, opt);
    }
}

QString Backend::friendlyCupsValue(const QString &value) {
    const QString lower = value.toLower();
    if (lower == "plain" || lower == "stationery") return tr("Plain Paper");
    if (lower == "ematte" || lower == "matte" || lower == "photographic-matte") return tr("Matte Photo");
    if (lower == "epremgloss" || lower == "glossy" || lower == "photographic-glossy") return tr("Premium Glossy");
    if (lower == "envelope") return tr("Envelope");
    if (lower == "draft") return tr("Draft");
    if (lower == "standard" || lower == "normal") return tr("Standard");
    if (lower == "high" || lower == "best" || lower == "photo") return tr("High");
    if (lower == "none" || lower == "off" || lower == "one-sided") return tr("Off");
    if (lower == "duplexnotumble" || lower == "two-sided-long-edge") return tr("Long Edge");
    if (lower == "duplextumble" || lower == "two-sided-short-edge") return tr("Short Edge");
    return value;
}

bool Backend::loadIndex(int index) {
    if (index < 0 || index >= m_files.size())
        return false;

    const QString path = m_files.at(index);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage loaded = reader.read();
    if (loaded.isNull()) {
        setError(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), reader.errorString()));
        return false;
    }

    m_currentIndex = index;
    m_sourceImage = loaded;
    m_edit = EditState{};
    m_colorEditing = false;
    m_undoStack.clear();
    m_redoStack.clear();
    m_editVersion = 0;
    m_exportedEditVersion = 0;
    if (!m_hasSavedExportFormat)
        m_exportFormat = loaded.hasAlphaChannel() ? "png" : "jpeg";
    clearSelection();
    rebuildPreColorImage();
    rebuildDisplayImageFromPreColor();
    clampPreviewPage();
    emit colorChanged();
    emit exportSettingsChanged();
    emit printSettingsChanged();
    return true;
}

void Backend::applyEditState(const EditState &next, bool recordUndo) {
    if (next == m_edit)
        return;
    if (recordUndo) {
        m_undoStack.push_back(m_edit);
        m_redoStack.clear();
    }
    const bool geometryChanged = m_edit.rotation != next.rotation ||
                                 m_edit.flipH != next.flipH ||
                                 m_edit.flipV != next.flipV ||
                                 m_edit.cropRect != next.cropRect ||
                                 m_edit.resizeTarget != next.resizeTarget;
    m_edit = next;
    ++m_editVersion;
    if (geometryChanged)
        rebuildPreColorImage();
    rebuildDisplayImageFromPreColor();
    emit colorChanged();
    emit printSettingsChanged();
}

void Backend::rebuildPreColorImage() {
    if (m_sourceImage.isNull()) {
        m_preColorImage = {};
        m_preResizeSize = {};
        return;
    }
    QImage img = PrintRenderer::applyTransforms(m_sourceImage, m_edit.rotation, m_edit.flipH, m_edit.flipV, false);
    if (!m_edit.cropRect.isEmpty()) {
        const QRect clamped = m_edit.cropRect.intersected(img.rect());
        if (clamped.isValid() && clamped.width() >= 2 && clamped.height() >= 2)
            img = img.copy(clamped);
    }
    m_preResizeSize = img.size();
    if (!m_edit.resizeTarget.isEmpty() && m_edit.resizeTarget != img.size())
        img = img.scaled(m_edit.resizeTarget, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_preColorImage = img;
}

void Backend::rebuildDisplayImageFromPreColor() {
    QImage img = m_edit.color.isIdentity()
                     ? m_preColorImage
                     : ImageOps::applyColorAdjustments(m_preColorImage, m_edit.color);
    if (!m_edit.annotations.isEmpty())
        img = Markup::compositeAnnotations(img, m_edit.annotations);
    m_displayImage = img;
    m_metadata = Inspector::readMetadata(filePath(), m_displayImage);
    ++m_imageVersion;
    emit imageChanged();
}

void Backend::refreshPrinterOptions() {
    if (m_lpoptionsPath.isEmpty() || m_selectedPrinter.isEmpty()) {
        clearPrinterOptions();
        return;
    }
    if (m_lpoptionsProcess.state() != QProcess::NotRunning) {
        m_lpoptionsProcess.kill();
        m_lpoptionsProcess.waitForFinished(500);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    m_lpoptionsProcess.setProcessEnvironment(env);
    m_lpoptionsProcess.start(m_lpoptionsPath, {"-p", m_selectedPrinter, "-l"});
}

void Backend::clearPrinterOptions() {
    m_mediaTypeOpt = {};
    m_qualityOpt = {};
    m_duplexOpt = {};
    m_printSettings.mediaTypeKey.clear();
    m_printSettings.mediaTypeValue.clear();
    m_printSettings.qualityKey.clear();
    m_printSettings.qualityValue.clear();
    m_printSettings.duplexKey.clear();
    m_printSettings.duplexValue.clear();
    emit printerOptionsChanged();
}

void Backend::clampPreviewPage() {
    const int total = std::max(1, previewPageCount());
    if (m_previewPageIndex >= total)
        m_previewPageIndex = total - 1;
    if (m_previewPageIndex < 0)
        m_previewPageIndex = 0;
}

QRect Backend::selectionPixelRect() const {
    if (m_selectionNorm.isEmpty() || m_displayImage.isNull())
        return {};
    const int x = static_cast<int>(std::round(m_selectionNorm.x() * m_displayImage.width()));
    const int y = static_cast<int>(std::round(m_selectionNorm.y() * m_displayImage.height()));
    const int w = static_cast<int>(std::round(m_selectionNorm.width() * m_displayImage.width()));
    const int h = static_cast<int>(std::round(m_selectionNorm.height() * m_displayImage.height()));
    return QRect(x, y, w, h).intersected(m_displayImage.rect());
}

void Backend::setStatus(const QString &msg) {
    m_errorMessage.clear();
    m_statusMessage = msg;
    emit messageChanged();
}

void Backend::setError(const QString &msg) {
    m_statusMessage.clear();
    m_errorMessage = msg;
    emit messageChanged();
}
