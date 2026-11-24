#include "backend.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

} // namespace

Backend::Backend(FilePicker *filePicker,
                 const QString &lpstatPath,
                 const QString &lpPath,
                 const QString &lpoptionsPath,
                 QObject *parent)
    : QObject(parent),
      m_filePicker(filePicker),
      m_lpstatPath(!lpstatPath.isEmpty() ? lpstatPath : QStandardPaths::findExecutable("lpstat")),
      m_lpPath(!lpPath.isEmpty() ? lpPath : QStandardPaths::findExecutable("lp")),
      m_lpoptionsPath(!lpoptionsPath.isEmpty() ? lpoptionsPath : QStandardPaths::findExecutable("lpoptions")) {
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
            if (m_selectedPrinter.isEmpty() || !m_printers.contains(m_selectedPrinter))
                m_selectedPrinter = !m_defaultPrinter.isEmpty() && m_printers.contains(m_defaultPrinter)
                                        ? m_defaultPrinter
                                        : m_printers.first();
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
    QImage img = PrintRenderer::applyTransforms(m_sourceImage,
                                                m_transform.rotation,
                                                m_transform.flipH,
                                                m_transform.flipV,
                                                m_printSettings.grayscale);
    if (m_printSelectionOnly && hasSelection() && !img.isNull()) {
        const QRect crop = selectionPixelRect().intersected(img.rect());
        if (crop.isValid() && crop.width() >= 2 && crop.height() >= 2)
            img = img.copy(crop);
    }
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
    clearPrinterOptions();
    refreshPrinterOptions();
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
    emit printSettingsChanged();
}

QVariantList Backend::qualityOptions() const {
    return toOptionModel(m_qualityOpt);
}

void Backend::setPrintQuality(const QString &value) {
    if (value == m_printSettings.qualityValue)
        return;
    m_printSettings.qualityValue = value;
    emit printSettingsChanged();
}

QVariantList Backend::duplexOptions() const {
    return toOptionModel(m_duplexOpt);
}

void Backend::setDuplex(const QString &value) {
    if (value == m_printSettings.duplexValue)
        return;
    m_printSettings.duplexValue = value;
    emit printSettingsChanged();
}

void Backend::setCopies(int count) {
    const int clamped = std::clamp(count, 1, 99);
    if (clamped == m_printSettings.copies)
        return;
    m_printSettings.copies = clamped;
    emit printSettingsChanged();
}

void Backend::setPaperSize(const QString &id) {
    const QString normalized = PrintRenderer::paperSpec(id).id;
    if (normalized == m_printSettings.paperSize)
        return;
    m_printSettings.paperSize = normalized;
    emit printSettingsChanged();
}

void Backend::setOrientation(const QString &mode) {
    const QString normalized = (mode == "portrait" || mode == "landscape") ? mode : QStringLiteral("auto");
    if (normalized == m_printSettings.orientation)
        return;
    m_printSettings.orientation = normalized;
    emit printSettingsChanged();
}

void Backend::setScaleMode(const QString &mode) {
    const QString normalized = (mode == "fill" || mode == "scale") ? mode : QStringLiteral("fit");
    if (normalized == m_printSettings.scaleMode)
        return;
    m_printSettings.scaleMode = normalized;
    emit printSettingsChanged();
}

void Backend::setScalePercent(int percent) {
    const int clamped = std::clamp(percent, 10, 200);
    if (clamped == m_printSettings.scalePercent)
        return;
    m_printSettings.scalePercent = clamped;
    emit printSettingsChanged();
}

void Backend::setMarginMode(const QString &mode) {
    const QString normalized = (mode == "narrow" || mode == "borderless") ? mode : QStringLiteral("standard");
    if (normalized == m_printSettings.marginMode)
        return;
    m_printSettings.marginMode = normalized;
    emit printSettingsChanged();
}

void Backend::setGrayscale(bool enabled) {
    if (enabled == m_printSettings.grayscale)
        return;
    m_printSettings.grayscale = enabled;
    emit printSettingsChanged();
}

void Backend::setPerPage(int count) {
    const int normalized = PrintRenderer::normalizePerPage(count);
    if (normalized == m_printSettings.perPage)
        return;
    m_printSettings.perPage = normalized;
    clampPreviewPage();
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

    // When a single image is opened, include its sibling images in natural order
    // so the user can step through the folder with Left/Right, like Preview.
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

void Backend::rotateLeft() {
    if (!hasImage())
        return;
    clearSelection();
    TransformState next = m_transform;
    next.rotation = (next.rotation + 270) % 360;
    applyTransformState(next, true);
}

void Backend::rotateRight() {
    if (!hasImage())
        return;
    clearSelection();
    TransformState next = m_transform;
    next.rotation = (next.rotation + 90) % 360;
    applyTransformState(next, true);
}

void Backend::toggleFlipHorizontal() {
    if (!hasImage())
        return;
    clearSelection();
    TransformState next = m_transform;
    next.flipH = !next.flipH;
    applyTransformState(next, true);
}

void Backend::toggleFlipVertical() {
    if (!hasImage())
        return;
    clearSelection();
    TransformState next = m_transform;
    next.flipV = !next.flipV;
    applyTransformState(next, true);
}

void Backend::resetTransforms() {
    if (!hasImage() || !isTransformed())
        return;
    clearSelection();
    applyTransformState(TransformState{}, true);
}

void Backend::undo() {
    if (m_undoStack.isEmpty())
        return;
    clearSelection();
    m_redoStack.push_back(m_transform);
    const TransformState previous = m_undoStack.takeLast();
    applyTransformState(previous, false);
}

void Backend::redo() {
    if (m_redoStack.isEmpty())
        return;
    clearSelection();
    m_undoStack.push_back(m_transform);
    const TransformState next = m_redoStack.takeLast();
    applyTransformState(next, false);
}

void Backend::preparePrintSheet() {
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

QStringList Backend::buildLpArguments(const QString &printer,
                                      const PrintSettings &settings,
                                      const QString &pdfPath,
                                      const QString &jobTitle) {
    const PaperSpec spec = PrintRenderer::paperSpec(settings.paperSize);
    QStringList args;
    if (!printer.isEmpty())
        args << "-d" << printer;
    args << "-n" << QString::number(std::clamp(settings.copies, 1, 99));
    if (!jobTitle.isEmpty())
        args << "-t" << jobTitle;
    // The PDF is already laid out to exact physical paper dimensions and margins,
    // so tell CUPS the matching media size and disable double-scaling.
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
    m_transform = TransformState{};
    m_undoStack.clear();
    m_redoStack.clear();
    clearSelection();
    rebuildDisplayImage();
    clampPreviewPage();
    emit printSettingsChanged();
    return true;
}

void Backend::applyTransformState(const TransformState &next, bool recordUndo) {
    if (next == m_transform)
        return;
    if (recordUndo) {
        m_undoStack.push_back(m_transform);
        m_redoStack.clear();
    }
    m_transform = next;
    rebuildDisplayImage();
    emit printSettingsChanged();
}

void Backend::rebuildDisplayImage() {
    m_displayImage = PrintRenderer::applyTransforms(m_sourceImage,
                                                    m_transform.rotation,
                                                    m_transform.flipH,
                                                    m_transform.flipV,
                                                    false);
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
