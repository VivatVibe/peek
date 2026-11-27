#include "backend.h"
#include "inspector.h"
#include "pdfdocument.h"

#include <QClipboard>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
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
    cancelActiveSearch();
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
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
        return {};
    return m_entries.at(m_currentIndex).sourcePath;
}

QString Backend::fileName() const {
    const QString path = filePath();
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}

bool Backend::currentPageIsPdf() const {
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() && m_entries.at(m_currentIndex).isPdfPage;
}

bool Backend::hasDocumentChanges() const {
    if (m_documentDirty)
        return true;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i != m_currentIndex && m_entries.at(i).dirty)
            return true;
    }
    return false;
}

bool Backend::hasUnsavedEdits() const {
    return (!m_edit.isIdentity() && m_editVersion != m_exportedEditVersion) || hasDocumentChanges();
}

QVariantList Backend::fileItems() const {
    QVariantList items;
    items.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const PageEntry &e = m_entries.at(i);
        QVariantMap row;
        row.insert("index", i);
        row.insert("filePath", e.sourcePath);
        row.insert("fileName", QFileInfo(e.sourcePath).fileName());
        row.insert("isPdfPage", e.isPdfPage);
        row.insert("sourcePageIndex", e.sourcePageIndex);
        row.insert("pageNumber", i + 1);
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
    PageEntry entry;
    QImage currentScaled;
    {
        QMutexLocker lock(&m_thumbMutex);
        if (index < 0 || index >= m_entries.size())
            return {};
        if (index == m_currentIndex && !m_displayImage.isNull())
            currentScaled = m_displayImage;
        else if (m_thumbCache.contains(index))
            return m_thumbCache.value(index);
        else
            entry = m_entries.at(index);
    }

    if (!currentScaled.isNull())
        return currentScaled.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QImage thumb;
    if (entry.isPdfPage) {
        thumb = PdfDocument::renderPage(entry.sourcePath, entry.sourcePageIndex, 36.0);
        if (!thumb.isNull() && !entry.edit.isIdentity()) {
            EditState thumbEdit = entry.edit;
            thumbEdit.cropRect = QRect();
            thumbEdit.resizeTarget = QSize();
            thumb = ImageOps::applyEdits(thumb, thumbEdit, false);
        }
    } else {
        QImageReader reader(entry.sourcePath);
        reader.setAutoTransform(true);
        const QSize orig = reader.size();
        if (orig.isValid() && !orig.isEmpty()) {
            const QSize scaled = orig.scaled(220, 220, Qt::KeepAspectRatio);
            if (scaled.isValid() && !scaled.isEmpty())
                reader.setScaledSize(scaled);
        }
        thumb = reader.read();
        if (!thumb.isNull() && !entry.edit.isIdentity()) {
            EditState thumbEdit = entry.edit;
            thumbEdit.cropRect = QRect();
            thumbEdit.resizeTarget = QSize();
            thumb = ImageOps::applyEdits(thumb, thumbEdit, false);
        }
    }
    if (!thumb.isNull() && (thumb.width() > 220 || thumb.height() > 220))
        thumb = thumb.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    {
        QMutexLocker lock(&m_thumbMutex);
        if (m_thumbCache.size() >= 160)
            m_thumbCache.clear();
        m_thumbCache.insert(index, thumb);
    }
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
    if (m_entries.isEmpty())
        return indices;

    if (m_pageRangeMode == "current") {
        if (m_currentIndex >= 0 && m_currentIndex < m_entries.size())
            indices.append(m_currentIndex);
        return indices;
    }

    if (m_pageRangeMode == "range") {
        bool ok = false;
        indices = PdfDocument::parsePageRange(m_pageRangeText, m_entries.size(), &ok);
        if (ok && !indices.isEmpty())
            return indices;
        if (m_currentIndex >= 0 && m_currentIndex < m_entries.size())
            return {m_currentIndex};
        return {0};
    }

    if (m_pageRangeMode == "selected" && !m_selectedIndices.isEmpty()) {
        for (int idx : m_selectedIndices) {
            if (idx >= 0 && idx < m_entries.size())
                indices.append(idx);
        }
        std::sort(indices.begin(), indices.end());
        return indices;
    }

    indices.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i)
        indices.append(i);
    return indices;
}

QImage Backend::renderEntryForPrint(int idx, bool highRes) const {
    if (idx < 0 || idx >= m_entries.size())
        return {};

    const PageEntry &entry = m_entries.at(idx);
    const EditState &edit = (idx == m_currentIndex) ? m_edit : entry.edit;

    if (idx == m_currentIndex && (!entry.isPdfPage || !highRes))
        return printableImage();

    QImage raw;
    if (entry.isPdfPage) {
        const double dpi = highRes ? 300.0 : 150.0;
        const QString cacheKey = entry.sourcePath + "#" + QString::number(entry.sourcePageIndex) + "@" + QString::number(static_cast<int>(dpi));
        if (!highRes && m_batchImageCache.contains(cacheKey)) {
            raw = m_batchImageCache.value(cacheKey);
        } else {
            raw = PdfDocument::renderPage(entry.sourcePath, entry.sourcePageIndex, dpi);
            if (!highRes && !raw.isNull() && m_batchImageCache.size() < 48)
                m_batchImageCache.insert(cacheKey, raw);
        }
        if (raw.isNull())
            return {};

        EditState scaledEdit = edit;
        if (highRes) {
            // Viewer edits (crop/resize) are recorded in 150-DPI coordinates; scale 2x for 300-DPI print output.
            if (!scaledEdit.cropRect.isEmpty()) {
                scaledEdit.cropRect = QRect(scaledEdit.cropRect.x() * 2,
                                            scaledEdit.cropRect.y() * 2,
                                            scaledEdit.cropRect.width() * 2,
                                            scaledEdit.cropRect.height() * 2);
            }
            if (!scaledEdit.resizeTarget.isEmpty()) {
                scaledEdit.resizeTarget = QSize(scaledEdit.resizeTarget.width() * 2,
                                                scaledEdit.resizeTarget.height() * 2);
            }
        }
        QImage out = ImageOps::applyEdits(raw, scaledEdit, m_printSettings.grayscale);
        if (idx == m_currentIndex && m_printSelectionOnly && hasSelection() && !out.isNull()) {
            const int sx = static_cast<int>(std::round(m_selectionNorm.x() * out.width()));
            const int sy = static_cast<int>(std::round(m_selectionNorm.y() * out.height()));
            const int sw = static_cast<int>(std::round(m_selectionNorm.width() * out.width()));
            const int sh = static_cast<int>(std::round(m_selectionNorm.height() * out.height()));
            const QRect crop = QRect(sx, sy, sw, sh).intersected(out.rect());
            if (crop.isValid() && crop.width() >= 2 && crop.height() >= 2)
                out = out.copy(crop);
        }
        return out;
    }

    const QString path = entry.sourcePath;
    if (m_batchImageCache.contains(path)) {
        raw = m_batchImageCache.value(path);
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        raw = reader.read();
        if (!raw.isNull() && m_batchImageCache.size() < 48)
            m_batchImageCache.insert(path, raw);
    }
    if (raw.isNull())
        return {};
    return ImageOps::applyEdits(raw, edit, m_printSettings.grayscale);
}

QVector<QVector<QImage>> Backend::buildPrintPages(bool highRes) const {
    if (!hasImage())
        return {};

    if (m_pageRangeMode != "current" && m_entries.size() > 1) {
        const QVector<int> indices = activeBatchIndices();
        QVector<QImage> batch;
        batch.reserve(indices.size());
        for (int idx : indices) {
            const QImage rendered = renderEntryForPrint(idx, highRes);
            if (!rendered.isNull())
                batch.append(rendered);
        }
        if (!batch.isEmpty())
            return PrintRenderer::paginateImages(batch, m_printSettings.perPage, false);
    }

    return PrintRenderer::paginateImages({renderEntryForPrint(m_currentIndex, highRes)}, m_printSettings.perPage, true);
}

QVector<QImage> Backend::previewPageImages() const {
    if (!hasImage())
        return {};

    const int n = PrintRenderer::normalizePerPage(m_printSettings.perPage);
    // Only render the 1..perPage entries visible on the active preview sheet
    // instead of rasterizing the entire document.
    if (m_pageRangeMode != "current" && m_entries.size() > 1) {
        const QVector<int> indices = activeBatchIndices();
        if (!indices.isEmpty()) {
            const int totalPages = (static_cast<int>(indices.size()) + n - 1) / n;
            const int pageIdx = std::clamp(m_previewPageIndex, 0, std::max(0, totalPages - 1));
            const int start = pageIdx * n;
            const int end = std::min(start + n, static_cast<int>(indices.size()));
            QVector<QImage> slotImages;
            slotImages.reserve(end - start);
            for (int i = start; i < end; ++i) {
                const QImage rendered = renderEntryForPrint(indices.at(i), false);
                if (!rendered.isNull())
                    slotImages.append(rendered);
            }
            if (!slotImages.isEmpty())
                return slotImages;
        }
    }

    const QImage single = renderEntryForPrint(m_currentIndex, false);
    if (single.isNull())
        return {};
    return QVector<QImage>(n, single);
}

QSize Backend::previewReferenceSize() const {
    if (!hasImage())
        return {};

    int refIdx = m_currentIndex;
    if (m_pageRangeMode != "current" && m_entries.size() > 1) {
        const QVector<int> indices = activeBatchIndices();
        if (!indices.isEmpty()) {
            const int n = PrintRenderer::normalizePerPage(m_printSettings.perPage);
            const int totalPages = (static_cast<int>(indices.size()) + n - 1) / n;
            const int pageIdx = std::clamp(m_previewPageIndex, 0, std::max(0, totalPages - 1));
            const int start = pageIdx * n;
            if (start >= 0 && start < indices.size())
                refIdx = indices.at(start);
        }
    }

    if (refIdx < 0 || refIdx >= m_entries.size())
        return m_displayImage.size();
    if (refIdx == m_currentIndex) {
        if (m_printSelectionOnly && hasSelection()) {
            const QRect crop = selectionPixelRect().intersected(m_displayImage.rect());
            if (crop.isValid() && crop.width() >= 2 && crop.height() >= 2)
                return crop.size();
        }
        return m_displayImage.size();
    }

    const PageEntry &entry = m_entries.at(refIdx);
    QSize sz;
    if (entry.isPdfPage) {
        const QSizeF mm = PdfDocument::pageSizeMm(entry.sourcePath, entry.sourcePageIndex);
        if (mm.isValid() && !mm.isEmpty())
            sz = QSize(std::max(1, static_cast<int>(std::round(mm.width() * 10.0))),
                       std::max(1, static_cast<int>(std::round(mm.height() * 10.0))));
    } else {
        QImageReader reader(entry.sourcePath);
        reader.setAutoTransform(true);
        sz = reader.size();
    }
    if (!sz.isValid() || sz.isEmpty())
        return m_displayImage.size();
    const int rot = ((entry.edit.rotation % 360) + 360) % 360;
    if (rot == 90 || rot == 270)
        sz.transpose();
    return sz;
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
    const QString nextRange = (normalized == "selected")
                                  ? (m_selectedIndices.isEmpty() ? QStringLiteral("all") : QStringLiteral("selected"))
                                  : QStringLiteral("current");
    if (normalized == m_multiMode && nextRange == m_pageRangeMode)
        return;
    m_multiMode = normalized;
    m_pageRangeMode = nextRange;
    m_previewPageIndex = 0;
    clampPreviewPage();
    emit printSettingsChanged();
}

void Backend::setPageRangeMode(const QString &mode) {
    QString normalized = QStringLiteral("current");
    if (mode == "all" || mode == "selected" || mode == "range")
        normalized = mode;
    const QString nextMulti = (normalized == "current") ? QStringLiteral("repeat") : QStringLiteral("selected");
    if (normalized == m_pageRangeMode && nextMulti == m_multiMode)
        return;
    m_pageRangeMode = normalized;
    m_multiMode = nextMulti;
    m_previewPageIndex = 0;
    clampPreviewPage();
    emit printSettingsChanged();
}

void Backend::setPageRangeText(const QString &text) {
    if (text == m_pageRangeText)
        return;
    m_pageRangeText = text;
    if (m_pageRangeMode == "range") {
        m_previewPageIndex = 0;
        clampPreviewPage();
    }
    emit printSettingsChanged();
}

bool Backend::pageRangeValid() const {
    if (m_pageRangeMode != "range")
        return true;
    bool ok = false;
    PdfDocument::parsePageRange(m_pageRangeText, m_entries.size(), &ok);
    return ok;
}

int Backend::activePageCount() const {
    if (!hasImage())
        return 0;
    if (m_pageRangeMode == "current")
        return 1;
    return static_cast<int>(activeBatchIndices().size());
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
    if (m_pageRangeMode != "current" && m_entries.size() > 1) {
        const int itemCount = std::max(1, static_cast<int>(activeBatchIndices().size()));
        const int n = PrintRenderer::normalizePerPage(m_printSettings.perPage);
        return (itemCount + n - 1) / n;
    }
    return 1;
}

bool Backend::effectiveLandscape() const {
    return PrintRenderer::resolveLandscape(m_printSettings, previewReferenceSize());
}

double Backend::paperWidthMm() const {
    return PrintRenderer::effectivePageSizeMm(m_printSettings, previewReferenceSize()).width();
}

double Backend::paperHeightMm() const {
    return PrintRenderer::effectivePageSizeMm(m_printSettings, previewReferenceSize()).height();
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
            setError(tr("No supported image or PDF files found."));
        return false;
    }

    QVector<PageEntry> entries;
    bool pdfMode = false;
    int startIndex = 0;

    if (resolved.size() == 1 && PdfDocument::isPdfFile(resolved.first())) {
        QString pdfErr;
        const int pages = PdfDocument::pageCount(resolved.first(), &pdfErr);
        if (pages <= 0) {
            setError(pdfErr.isEmpty() ? tr("Could not open PDF: %1").arg(QFileInfo(resolved.first()).fileName()) : pdfErr);
            return false;
        }
        entries.reserve(pages);
        for (int p = 0; p < pages; ++p)
            entries.append({true, resolved.first(), p, EditState{}, false});
        pdfMode = true;
    } else if (resolved.size() == 1) {
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

        entries.reserve(siblings.size());
        for (const QString &sib : siblings)
            entries.append({false, sib, 0, EditState{}, false});
        startIndex = siblings.indexOf(targetPath);
    } else {
        resolved.removeDuplicates();
        for (const QString &p : resolved) {
            if (PdfDocument::isPdfFile(p)) {
                QString pdfErr;
                const int pages = PdfDocument::pageCount(p, &pdfErr);
                if (pages <= 0) {
                    setError(pdfErr);
                    continue;
                }
                pdfMode = true;
                for (int i = 0; i < pages; ++i)
                    entries.append({true, p, i, EditState{}, false});
            } else {
                entries.append({false, p, 0, EditState{}, false});
            }
        }
    }

    if (entries.isEmpty()) {
        if (m_errorMessage.isEmpty())
            setError(tr("No supported image or PDF files found."));
        return false;
    }

    cancelActiveSearch();
    {
        QMutexLocker lock(&m_thumbMutex);
        m_entries = std::move(entries);
        m_currentIndex = -1;
        m_thumbCache.clear();
    }
    m_isPdfMode = pdfMode;
    m_documentDirty = false;
    m_selectedIndices.clear();
    m_batchImageCache.clear();
    m_pageRangeText.clear();
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_searchCurrentIndex = -1;
    m_cachedSearchRectsEntry = -1;
    m_cachedSearchPageMatches.clear();
    m_pageTextWords.clear();
    m_pageChars.clear();
    m_textSelStart = -1;
    m_textSelEnd = -1;
    m_selectedText.clear();
    m_textSelPdfRects.clear();
    if (m_isPdfMode && m_entries.size() > 1) {
        m_pageRangeMode = "all";
        m_multiMode = "selected";
    } else {
        m_pageRangeMode = "current";
        m_multiMode = "repeat";
    }
    rebuildOutline();
    ++m_fileListVersion;
    emit filesChanged();
    emit searchChanged();
    emit textSelectionChanged();
    emit pdfTextChanged();

    return loadIndex(std::max(0, startIndex));
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
        tr("Open Image or PDF"),
        [this](const QStringList &paths) { openFiles(paths); },
        [this](const QString &error) { setError(error); });
}

void Backend::selectIndex(int index) {
    if (index < 0 || index >= m_entries.size() || index == m_currentIndex)
        return;
    loadIndex(index);
}

void Backend::nextImage() {
    if (m_entries.size() < 2)
        return;
    loadIndex((m_currentIndex + 1) % m_entries.size());
}

void Backend::previousImage() {
    if (m_entries.size() < 2)
        return;
    loadIndex((m_currentIndex - 1 + m_entries.size()) % m_entries.size());
}

void Backend::toggleFileSelected(int index) {
    if (index < 0 || index >= m_entries.size())
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
    if (index < 0 || index >= m_entries.size())
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
    if (m_entries.isEmpty())
        return;
    for (int i = 0; i < m_entries.size(); ++i)
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

bool Backend::movePage(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_entries.size() ||
        toIndex < 0 || toIndex >= m_entries.size() || fromIndex == toIndex)
        return false;

    commitColorEdit();
    syncCurrentEntryEdits();

    // Remap current index and multi-selection set cleanly.
    auto remap = [fromIndex, toIndex](int idx) {
        if (idx == fromIndex)
            return toIndex;
        if (fromIndex < toIndex && idx > fromIndex && idx <= toIndex)
            return idx - 1;
        if (fromIndex > toIndex && idx >= toIndex && idx < fromIndex)
            return idx + 1;
        return idx;
    };

    {
        QMutexLocker lock(&m_thumbMutex);
        PageEntry moved = m_entries.takeAt(fromIndex);
        m_entries.insert(toIndex, moved);
        m_currentIndex = remap(m_currentIndex);
        m_thumbCache.clear();
    }
    QSet<int> remappedSel;
    for (int idx : m_selectedIndices)
        remappedSel.insert(remap(idx));
    m_selectedIndices = remappedSel;

    m_isPdfMode = true;
    m_documentDirty = true;
    ++m_fileListVersion;
    clampPreviewPage();

    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        const PageEntry &cur = m_entries.at(m_currentIndex);
        m_metadata = Inspector::readMetadata(cur.sourcePath, m_displayImage,
                                             cur.isPdfPage ? cur.sourcePageIndex : -1,
                                             m_isPdfMode ? m_entries.size() : 0);
    }

    emit filesChanged();
    emit imageChanged();
    emit printSettingsChanged();
    emit outlineChanged();
    if (!m_searchQuery.isEmpty())
        runSearch(false);
    return true;
}

bool Backend::moveCurrentPageUp() {
    return movePage(m_currentIndex, m_currentIndex - 1);
}

bool Backend::moveCurrentPageDown() {
    return movePage(m_currentIndex, m_currentIndex + 1);
}

bool Backend::deletePage(int index) {
    if (m_entries.size() <= 1 || index < 0 || index >= m_entries.size())
        return false;

    commitColorEdit();
    syncCurrentEntryEdits();

    const bool deletingCurrent = (index == m_currentIndex);
    {
        QMutexLocker lock(&m_thumbMutex);
        m_entries.removeAt(index);
        m_thumbCache.clear();
    }

    QSet<int> remappedSel;
    for (int idx : m_selectedIndices) {
        if (idx == index)
            continue;
        remappedSel.insert(idx > index ? idx - 1 : idx);
    }
    m_selectedIndices = remappedSel;

    m_isPdfMode = true;
    m_documentDirty = true;
    ++m_fileListVersion;

    if (deletingCurrent) {
        const int nextIndex = std::clamp(index, 0, static_cast<int>(m_entries.size()) - 1);
        {
            QMutexLocker lock(&m_thumbMutex);
            m_currentIndex = -1;
        }
        emit filesChanged();
        loadIndex(nextIndex);
    } else {
        if (index < m_currentIndex) {
            QMutexLocker lock(&m_thumbMutex);
            --m_currentIndex;
        }
        if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
            const PageEntry &cur = m_entries.at(m_currentIndex);
            m_metadata = Inspector::readMetadata(cur.sourcePath, m_displayImage,
                                                 cur.isPdfPage ? cur.sourcePageIndex : -1,
                                                 m_isPdfMode ? m_entries.size() : 0);
        }
        clampPreviewPage();
        emit filesChanged();
        emit imageChanged();
        emit printSettingsChanged();
        emit outlineChanged();
    }
    if (!m_searchQuery.isEmpty())
        runSearch(false);
    setStatus(tr("Removed page (original file unchanged)"));
    return true;
}

bool Backend::deleteCurrentPage() {
    return deletePage(m_currentIndex);
}

bool Backend::appendFiles(const QStringList &paths) {
    clearMessages();
    if (paths.isEmpty())
        return false;
    if (m_entries.isEmpty())
        return openFiles(paths);

    commitColorEdit();
    syncCurrentEntryEdits();

    int added = 0;
    QVector<PageEntry> newEntries;
    for (const QString &raw : paths) {
        const QFileInfo fi(raw);
        if (!fi.exists() || !fi.isFile())
            continue;
        const QString abs = fi.absoluteFilePath();
        if (PdfDocument::isPdfFile(abs)) {
            QString err;
            const int pages = PdfDocument::pageCount(abs, &err);
            if (pages <= 0) {
                setError(err);
                continue;
            }
            for (int p = 0; p < pages; ++p) {
                newEntries.append({true, abs, p, EditState{}, false});
                ++added;
            }
        } else if (isSupportedImageFile(abs)) {
            newEntries.append({false, abs, 0, EditState{}, false});
            ++added;
        }
    }

    if (added == 0) {
        if (m_errorMessage.isEmpty())
            setError(tr("No supported image or PDF files to merge."));
        return false;
    }

    {
        QMutexLocker lock(&m_thumbMutex);
        for (auto &e : newEntries)
            m_entries.append(std::move(e));
        m_thumbCache.clear();
    }
    m_isPdfMode = true;
    m_documentDirty = true;
    m_pageRangeMode = "all";
    m_multiMode = "selected";
    rebuildOutline();
    ++m_fileListVersion;
    clampPreviewPage();
    emit filesChanged();
    emit imageChanged();
    emit printSettingsChanged();
    if (!m_searchQuery.isEmpty())
        runSearch(false);
    setStatus(tr("Added %n pages", "", added));
    return true;
}

void Backend::appendFilesDialog() {
    if (!m_filePicker) {
        setError(tr("File picker is unavailable."));
        return;
    }
    m_filePicker->openImages(
        tr("Merge PDF or Images"),
        [this](const QStringList &paths) { appendFiles(paths); },
        [this](const QString &error) { setError(error); });
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

    clearTextSelection();
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
    if (hasTextSelection()) {
        copySelectedText();
        return;
    }
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

void Backend::setSearchCaseSensitive(bool enabled) {
    if (enabled == m_searchCaseSensitive)
        return;
    m_searchCaseSensitive = enabled;
    if (!m_searchQuery.isEmpty())
        runSearch(true);
    else
        emit searchChanged();
}

void Backend::setSearchQuery(const QString &query) {
    if (query == m_searchQuery)
        return;
    m_searchQuery = query;
    if (m_searchQuery.isEmpty()) {
        cancelActiveSearch();
        m_searchMatches.clear();
        m_searchCurrentIndex = -1;
        emit searchChanged();
        return;
    }
    runSearch(true);
}

void Backend::nextSearchMatch() {
    if (m_searchMatches.isEmpty())
        return;
    m_searchCurrentIndex = (m_searchCurrentIndex + 1) % m_searchMatches.size();
    const int targetEntry = m_searchMatches.at(m_searchCurrentIndex).entryIndex;
    if (targetEntry != m_currentIndex)
        loadIndex(targetEntry);
    emit searchChanged();
}

void Backend::previousSearchMatch() {
    if (m_searchMatches.isEmpty())
        return;
    const int count = static_cast<int>(m_searchMatches.size());
    m_searchCurrentIndex = (m_searchCurrentIndex - 1 + count) % count;
    const int targetEntry = m_searchMatches.at(m_searchCurrentIndex).entryIndex;
    if (targetEntry != m_currentIndex)
        loadIndex(targetEntry);
    emit searchChanged();
}

void Backend::clearSearch() {
    cancelActiveSearch();
    if (m_searchQuery.isEmpty() && m_searchMatches.isEmpty() && m_searchCurrentIndex < 0 && !m_searchBusy)
        return;
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_searchCurrentIndex = -1;
    m_cachedSearchRectsEntry = -1;
    m_cachedSearchPageMatches.clear();
    m_searchBusy = false;
    emit searchChanged();
}

const QVector<PdfDocument::PdfPageMatch> &Backend::ensureCurrentPageSearchMatches() const {
    static const QVector<PdfDocument::PdfPageMatch> empty;
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size() || m_searchQuery.isEmpty())
        return empty;
    const PageEntry &cur = m_entries.at(m_currentIndex);
    if (!cur.isPdfPage)
        return empty;
    if (m_cachedSearchRectsEntry == m_currentIndex &&
        m_cachedSearchRectsQuery == m_searchQuery &&
        m_cachedSearchRectsCase == m_searchCaseSensitive) {
        return m_cachedSearchPageMatches;
    }
    m_cachedSearchPageMatches = PdfDocument::searchPageMatches(
        cur.sourcePath, cur.sourcePageIndex, m_searchQuery, m_searchCaseSensitive);
    m_cachedSearchRectsEntry = m_currentIndex;
    m_cachedSearchRectsQuery = m_searchQuery;
    m_cachedSearchRectsCase = m_searchCaseSensitive;
    return m_cachedSearchPageMatches;
}

QVariantList Backend::currentPageSearchRects() const {
    QVariantList out;
    if (m_currentIndex < 0 || m_searchMatches.isEmpty())
        return out;
    const auto &pageMatches = ensureCurrentPageSearchMatches();
    if (pageMatches.isEmpty())
        return out;

    int activeOrdinal = -1;
    if (m_searchCurrentIndex >= 0 && m_searchCurrentIndex < m_searchMatches.size()) {
        const auto &cur = m_searchMatches.at(m_searchCurrentIndex);
        if (cur.entryIndex == m_currentIndex)
            activeOrdinal = std::clamp(cur.matchIndexOnPage, 0, static_cast<int>(pageMatches.size()) - 1);
    }

    for (int i = 0; i < pageMatches.size(); ++i) {
        const bool isActive = (i == activeOrdinal);
        for (const QRectF &pdfRect : pageMatches.at(i).rects) {
            const QRectF mapped = PdfDocument::mapPdfNormToDisplay(
                pdfRect, m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());
            if (mapped.isEmpty())
                continue;
            QVariantMap row;
            row.insert("x", mapped.x());
            row.insert("y", mapped.y());
            row.insert("width", mapped.width());
            row.insert("height", mapped.height());
            row.insert("active", isActive);
            out.append(row);
        }
    }
    return out;
}

QVariantMap Backend::activeSearchRect() const {
    if (m_searchCurrentIndex < 0 || m_searchCurrentIndex >= m_searchMatches.size())
        return {{"valid", false}};
    const auto &cur = m_searchMatches.at(m_searchCurrentIndex);
    if (cur.entryIndex != m_currentIndex)
        return {{"valid", false}};
    const auto &pageMatches = ensureCurrentPageSearchMatches();
    if (pageMatches.isEmpty())
        return {{"valid", false}};
    const int idx = std::clamp(cur.matchIndexOnPage, 0, static_cast<int>(pageMatches.size()) - 1);
    if (pageMatches.at(idx).rects.isEmpty())
        return {{"valid", false}};
    const QRectF mapped = PdfDocument::mapPdfNormToDisplay(
        pageMatches.at(idx).rects.first(), m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());
    if (mapped.isEmpty())
        return {{"valid", false}};
    return {
        {"valid", true},
        {"x", mapped.x()},
        {"y", mapped.y()},
        {"width", mapped.width()},
        {"height", mapped.height()}
    };
}

int Backend::selectedTextWordCount() const {
    if (m_selectedText.trimmed().isEmpty())
        return 0;
    return static_cast<int>(m_selectedText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size());
}

QVariantList Backend::textSelectionRects() const {
    QVariantList out;
    if (m_textSelPdfRects.isEmpty())
        return out;
    out.reserve(m_textSelPdfRects.size());
    for (const QRectF &pdfRect : m_textSelPdfRects) {
        const QRectF mapped = PdfDocument::mapPdfNormToDisplay(
            pdfRect, m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());
        if (mapped.isEmpty())
            continue;
        QVariantMap row;
        row.insert("x", mapped.x());
        row.insert("y", mapped.y());
        row.insert("width", mapped.width());
        row.insert("height", mapped.height());
        out.append(row);
    }
    return out;
}

bool Backend::isPointOverText(double normX, double normY) const {
    if (m_pageTextWords.isEmpty() || !currentPageIsPdf())
        return false;
    const QPointF pt = PdfDocument::mapDisplayNormToPdf(
        QPointF(normX, normY), m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());
    constexpr double pad = 0.005;
    for (const PdfTextWord &w : m_pageTextWords) {
        if (w.normRect.adjusted(-pad, -pad, pad, pad).contains(pt))
            return true;
    }
    return false;
}

void Backend::selectTextRange(double startNormX, double startNormY, double endNormX, double endNormY) {
    if (m_pageChars.isEmpty() || !currentPageIsPdf()) {
        clearTextSelection();
        return;
    }
    const QPointF p1 = PdfDocument::mapDisplayNormToPdf(
        QPointF(startNormX, startNormY), m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());
    const QPointF p2 = PdfDocument::mapDisplayNormToPdf(
        QPointF(endNormX, endNormY), m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());

    const int idx1 = findNearestCharIndex(p1);
    const int idx2 = findNearestCharIndex(p2);
    if (idx1 < 0 || idx2 < 0) {
        clearTextSelection();
        return;
    }
    applyCharSelectionRange(idx1, idx2);
}

void Backend::selectWordAt(double normX, double normY) {
    if (m_pageChars.isEmpty() || !currentPageIsPdf())
        return;
    const QPointF pt = PdfDocument::mapDisplayNormToPdf(
        QPointF(normX, normY), m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());

    int targetWord = -1;
    constexpr double pad = 0.008;
    for (int i = 0; i < m_pageTextWords.size(); ++i) {
        if (m_pageTextWords.at(i).normRect.adjusted(-pad, -pad, pad, pad).contains(pt)) {
            targetWord = i;
            break;
        }
    }
    if (targetWord < 0)
        return;

    int first = -1, last = -1;
    for (int i = 0; i < m_pageChars.size(); ++i) {
        if (m_pageChars.at(i).wordIndex == targetWord) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first >= 0)
        applyCharSelectionRange(first, last);
}

void Backend::selectLineAt(double normX, double normY) {
    if (m_pageChars.isEmpty() || !currentPageIsPdf())
        return;
    const QPointF pt = PdfDocument::mapDisplayNormToPdf(
        QPointF(normX, normY), m_edit.rotation, m_edit.flipH, m_edit.flipV, m_edit.cropRect, m_sourceImage.size());
    const int idx = findNearestCharIndex(pt);
    if (idx < 0)
        return;
    const int line = m_pageChars.at(idx).lineIndex;
    int first = idx;
    int last = idx;
    while (first > 0 && m_pageChars.at(first - 1).lineIndex == line)
        --first;
    while (last + 1 < m_pageChars.size() && m_pageChars.at(last + 1).lineIndex == line)
        ++last;
    applyCharSelectionRange(first, last);
}

void Backend::selectAllPageText() {
    if (m_pageChars.isEmpty() || !currentPageIsPdf())
        return;
    applyCharSelectionRange(0, static_cast<int>(m_pageChars.size()) - 1);
}

void Backend::clearTextSelection() {
    if (m_textSelStart < 0 && m_selectedText.isEmpty() && m_textSelPdfRects.isEmpty())
        return;
    m_textSelStart = -1;
    m_textSelEnd = -1;
    m_selectedText.clear();
    m_textSelPdfRects.clear();
    emit textSelectionChanged();
}

void Backend::copySelectedText() {
    if (m_selectedText.isEmpty())
        return;
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip) {
        setError(tr("Clipboard is unavailable."));
        return;
    }
    clip->setText(m_selectedText);
    setStatus(tr("Copied text to clipboard"));
}

QVariantList Backend::outlineItems() const {
    QVariantList out;
    if (m_outlineNodes.isEmpty())
        return out;

    // Resolve each node's current entry index in m_entries and find the active section.
    QVector<int> resolvedEntries;
    resolvedEntries.reserve(m_outlineNodes.size());
    int activeNodeId = -1;
    for (int i = 0; i < m_outlineNodes.size(); ++i) {
        const OutlineNode &n = m_outlineNodes.at(i);
        const int entryIdx = findEntryIndexForOutline(n.sourcePath, n.sourcePageIndex);
        resolvedEntries.append(entryIdx);
        if (entryIdx >= 0 && entryIdx <= m_currentIndex)
            activeNodeId = i;
    }

    int hiddenAboveDepth = -1;
    for (int i = 0; i < m_outlineNodes.size(); ++i) {
        const OutlineNode &n = m_outlineNodes.at(i);
        if (hiddenAboveDepth >= 0) {
            if (n.depth > hiddenAboveDepth)
                continue;
            hiddenAboveDepth = -1;
        }

        const int entryIdx = resolvedEntries.at(i);
        QVariantMap row;
        row.insert("id", i);
        row.insert("title", n.title);
        row.insert("depth", n.depth);
        row.insert("entryIndex", entryIdx);
        row.insert("pageNumber", entryIdx >= 0 ? (entryIdx + 1) : 0);
        row.insert("hasChildren", n.hasChildren);
        row.insert("expanded", n.expanded);
        row.insert("active", i == activeNodeId);
        out.append(row);

        if (n.hasChildren && !n.expanded)
            hiddenAboveDepth = n.depth;
    }
    return out;
}

void Backend::toggleOutlineItem(int id) {
    if (id < 0 || id >= m_outlineNodes.size() || !m_outlineNodes.at(id).hasChildren)
        return;
    m_outlineNodes[id].expanded = !m_outlineNodes[id].expanded;
    emit outlineChanged();
}

void Backend::jumpToOutlineItem(int id) {
    if (id < 0 || id >= m_outlineNodes.size())
        return;
    const OutlineNode &node = m_outlineNodes.at(id);
    const int entryIdx = findEntryIndexForOutline(node.sourcePath, node.sourcePageIndex);
    if (entryIdx >= 0)
        selectIndex(entryIdx);
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
        if (!pageRangeValid()) {
            setError(tr("Invalid page range. Use numbers like 1-3, 5."));
            return false;
        }
        ok = PrintRenderer::exportPdf(finalPath, buildPrintPages(true), m_printSettings, &err);
        if (ok) {
            m_documentDirty = false;
            for (auto &entry : m_entries)
                entry.dirty = false;
            emit filesChanged();
        }
    } else {
        ok = ImageOps::exportImageAtomic(m_displayImage, finalPath, norm, m_exportQuality, &err);
    }

    if (!ok) {
        setError(err);
        return false;
    }

    m_exportedEditVersion = m_editVersion;
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size())
        m_entries[m_currentIndex].dirty = false;
    emit imageChanged();
    setStatus(tr("Exported %1").arg(QFileInfo(finalPath).fileName()));
    return true;
}

void Backend::preparePrintSheet() {
    commitColorEdit();
    refreshPrinters();
    if (m_selectedIndices.size() > 1) {
        m_pageRangeMode = "selected";
        m_multiMode = "selected";
    } else if (m_isPdfMode && m_entries.size() > 1) {
        if (m_pageRangeMode != "range" && m_pageRangeMode != "current") {
            m_pageRangeMode = "all";
            m_multiMode = "selected";
        }
    } else if (m_selectedIndices.isEmpty() && m_entries.size() <= 1) {
        m_pageRangeMode = "current";
        m_multiMode = "repeat";
    }
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
        setError(tr("CUPS (lp) is not installed, so Omarchy Preview cannot send jobs to a printer."));
        return;
    }
    if (m_selectedPrinter.isEmpty()) {
        setError(tr("No printer selected. Check your CUPS printer setup."));
        return;
    }
    if (m_printBusy)
        return;
    if (!pageRangeValid()) {
        setError(tr("Invalid page range. Use numbers like 1-3, 5."));
        return;
    }

    const QVector<QVector<QImage>> pages = buildPrintPages(true);
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString tempPdf = QDir(tempDir).filePath(
        "omarchy-preview-print-" + QUuid::createUuid().toString(QUuid::Id128) + ".pdf");

    QString exportError;
    if (!PrintRenderer::exportPdf(tempPdf, pages, m_printSettings, &exportError, true)) {
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
    const QString suffix = (m_isPdfMode || hasDocumentChanges()) ? "-edited" : "";
    const QString suggested = info.completeBaseName() + suffix + ".pdf";
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
    if (!pageRangeValid()) {
        setError(tr("Invalid page range. Use numbers like 1-3, 5."));
        return false;
    }

    commitColorEdit();
    syncCurrentEntryEdits();

    QString finalPath = outputPath;
    if (!finalPath.endsWith(".pdf", Qt::CaseInsensitive))
        finalPath += ".pdf";

    QString err;
    if (!PrintRenderer::exportPdf(finalPath, buildPrintPages(true), m_printSettings, &err)) {
        setError(err);
        return false;
    }

    PdfDocument::evictCache(finalPath);
    m_documentDirty = false;
    for (auto &entry : m_entries)
        entry.dirty = false;
    m_exportedEditVersion = m_editVersion;
    emit filesChanged();
    emit imageChanged();
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

void Backend::syncCurrentEntryEdits() {
    QMutexLocker lock(&m_thumbMutex);
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
        return;
    m_entries[m_currentIndex].edit = m_edit;
    m_entries[m_currentIndex].dirty = !m_edit.isIdentity() && (m_editVersion != m_exportedEditVersion);
    m_thumbCache.remove(m_currentIndex);
}

bool Backend::loadIndex(int index) {
    if (index < 0 || index >= m_entries.size())
        return false;

    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        commitColorEdit();
        syncCurrentEntryEdits();
    }

    const PageEntry &entry = m_entries.at(index);
    QImage loaded;
    if (entry.isPdfPage) {
        QString pdfErr;
        loaded = PdfDocument::renderPage(entry.sourcePath, entry.sourcePageIndex, 150.0, &pdfErr);
        if (loaded.isNull()) {
            setError(pdfErr.isEmpty()
                         ? tr("Could not render page %1 of %2").arg(entry.sourcePageIndex + 1).arg(QFileInfo(entry.sourcePath).fileName())
                         : pdfErr);
            return false;
        }
    } else {
        QImageReader reader(entry.sourcePath);
        reader.setAutoTransform(true);
        loaded = reader.read();
        if (loaded.isNull()) {
            setError(tr("Could not open %1: %2").arg(QFileInfo(entry.sourcePath).fileName(), reader.errorString()));
            return false;
        }
    }

    {
        QMutexLocker lock(&m_thumbMutex);
        m_currentIndex = index;
    }
    m_sourceImage = loaded;
    m_edit = entry.edit;
    m_colorEditing = false;
    m_undoStack.clear();
    m_redoStack.clear();
    m_editVersion = entry.dirty ? 1 : 0;
    m_exportedEditVersion = 0;
    if (!m_hasSavedExportFormat)
        m_exportFormat = entry.isPdfPage ? "pdf" : (loaded.hasAlphaChannel() ? "png" : "jpeg");
    clearSelection();
    clearTextSelection();
    reloadCurrentPageText();
    rebuildPreColorImage();
    rebuildDisplayImageFromPreColor();
    clampPreviewPage();
    emit colorChanged();
    emit exportSettingsChanged();
    emit printSettingsChanged();
    emit outlineChanged();
    if (!m_searchMatches.isEmpty())
        emit searchChanged();
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
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        QMutexLocker lock(&m_thumbMutex);
        m_entries[m_currentIndex].edit = m_edit;
        m_entries[m_currentIndex].dirty = !m_edit.isIdentity();
        m_thumbCache.remove(m_currentIndex);
    }
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
    {
        QMutexLocker lock(&m_thumbMutex);
        m_displayImage = img;
    }
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        const PageEntry &cur = m_entries.at(m_currentIndex);
        m_metadata = Inspector::readMetadata(cur.sourcePath, m_displayImage,
                                             cur.isPdfPage ? cur.sourcePageIndex : -1,
                                             m_isPdfMode ? m_entries.size() : 0);
    } else {
        m_metadata = {};
    }
    ++m_imageVersion;
    emit imageChanged();
    if (!m_searchMatches.isEmpty())
        emit searchChanged();
    if (!m_textSelPdfRects.isEmpty())
        emit textSelectionChanged();
}

void Backend::reloadCurrentPageText() {
    m_pageTextWords.clear();
    m_pageChars.clear();
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        const PageEntry &cur = m_entries.at(m_currentIndex);
        if (cur.isPdfPage) {
            m_pageTextWords = PdfDocument::extractTextWords(cur.sourcePath, cur.sourcePageIndex);
            int lineIdx = 0;
            QRectF prevWordRect;
            for (int wIdx = 0; wIdx < m_pageTextWords.size(); ++wIdx) {
                const PdfTextWord &w = m_pageTextWords.at(wIdx);
                bool newLine = false;
                if (wIdx > 0) {
                    const double lineTol = std::max(w.normRect.height(), prevWordRect.height()) * 0.65;
                    const bool sameY = std::abs(w.normRect.center().y() - prevWordRect.center().y()) <= std::max(0.004, lineTol);
                    const bool goesBackward = w.normRect.left() + prevWordRect.height() * 0.5 < prevWordRect.left();
                    if (!sameY || goesBackward) {
                        ++lineIdx;
                        newLine = true;
                    }
                }
                const int len = w.text.size();
                for (int cIdx = 0; cIdx < len; ++cIdx) {
                    FlatTextChar fc;
                    fc.wordIndex = wIdx;
                    fc.lineIndex = lineIdx;
                    fc.ch = w.text.at(cIdx);
                    fc.pdfNormRect = (cIdx < w.charNormRects.size()) ? w.charNormRects.at(cIdx) : w.normRect;
                    fc.spaceAfter = (cIdx == len - 1) && w.hasSpaceAfter;
                    fc.lineBreakBefore = (cIdx == 0) && newLine;
                    m_pageChars.append(fc);
                }
                prevWordRect = w.normRect;
            }
        }
    }
    emit pdfTextChanged();
}

void Backend::rebuildOutline() {
    m_outlineNodes.clear();
    QSet<QString> seenPaths;
    for (const PageEntry &e : m_entries) {
        if (!e.isPdfPage || seenPaths.contains(e.sourcePath))
            continue;
        seenPaths.insert(e.sourcePath);
        const QVector<PdfOutlineEntry> docOutline = PdfDocument::readOutline(e.sourcePath);
        for (const PdfOutlineEntry &item : docOutline) {
            OutlineNode node;
            node.title = item.title;
            node.sourcePath = e.sourcePath;
            node.sourcePageIndex = item.pageIndex;
            node.depth = item.depth;
            node.hasChildren = item.hasChildren;
            node.expanded = item.initiallyOpen || item.depth == 0;
            m_outlineNodes.append(std::move(node));
        }
    }
    emit outlineChanged();
}

void Backend::cancelActiveSearch() {
    ++m_searchGeneration;
    if (m_searchCancel) {
        m_searchCancel->store(true, std::memory_order_relaxed);
        m_searchCancel.reset();
    }
    m_searchBusy = false;
}

void Backend::applySearchResults(QVector<PdfDocument::PdfBatchMatchRef> &&hits, bool jumpToMatch) {
    m_searchMatches = std::move(hits);
    m_cachedSearchRectsEntry = -1;
    m_cachedSearchPageMatches.clear();

    m_searchCurrentIndex = -1;
    if (!m_searchMatches.isEmpty()) {
        m_searchCurrentIndex = 0;
        for (int i = 0; i < m_searchMatches.size(); ++i) {
            if (m_searchMatches.at(i).entryIndex >= m_currentIndex) {
                m_searchCurrentIndex = i;
                break;
            }
        }
        if (jumpToMatch) {
            const int targetEntry = m_searchMatches.at(m_searchCurrentIndex).entryIndex;
            if (targetEntry != m_currentIndex && targetEntry >= 0 && targetEntry < m_entries.size())
                loadIndex(targetEntry);
        }
    }

    emit searchChanged();
}

void Backend::runSearch(bool jumpToMatch) {
    cancelActiveSearch();
    m_searchMatches.clear();
    m_searchCurrentIndex = -1;
    m_cachedSearchRectsEntry = -1;
    m_cachedSearchPageMatches.clear();

    if (m_searchQuery.isEmpty()) {
        emit searchChanged();
        return;
    }

    QVector<PdfDocument::PdfBatchSearchRequest> pages;
    pages.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const PageEntry &entry = m_entries.at(i);
        if (entry.isPdfPage)
            pages.append({i, entry.sourcePath, entry.sourcePageIndex});
    }

    if (pages.isEmpty()) {
        emit searchChanged();
        return;
    }

    // Small documents finish in <25 ms synchronously; larger PDFs run on a
    // cancellable background thread with a reusable cached text index.
    if (pages.size() <= 16) {
        auto hits = PdfDocument::searchBatchIndexed(pages, m_searchQuery, m_searchCaseSensitive, nullptr);
        applySearchResults(std::move(hits), jumpToMatch);
        return;
    }

    const quint64 gen = m_searchGeneration;
    const QString query = m_searchQuery;
    const bool caseSensitive = m_searchCaseSensitive;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_searchCancel = cancel;
    m_searchBusy = true;
    emit searchChanged();

    QPointer<Backend> self(this);
    QThread *worker = QThread::create([self, gen, pages, query, caseSensitive, jumpToMatch, cancel]() mutable {
        auto hits = PdfDocument::searchBatchIndexed(pages, query, caseSensitive, cancel.get());
        if (cancel->load(std::memory_order_relaxed))
            return;
        QMetaObject::invokeMethod(
            self,
            [self, gen, jumpToMatch, hits = std::move(hits)]() mutable {
                if (!self || gen != self->m_searchGeneration)
                    return;
                self->m_searchBusy = false;
                self->m_searchCancel.reset();
                self->applySearchResults(std::move(hits), jumpToMatch);
            },
            Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void Backend::applyCharSelectionRange(int startIdx, int endIdx) {
    if (m_pageChars.isEmpty())
        return;
    const int maxIdx = static_cast<int>(m_pageChars.size()) - 1;
    const int lo = std::clamp(std::min(startIdx, endIdx), 0, maxIdx);
    const int hi = std::clamp(std::max(startIdx, endIdx), 0, maxIdx);

    clearSelection();
    m_textSelStart = lo;
    m_textSelEnd = hi;

    QString text;
    text.reserve(hi - lo + 16);
    QVector<QRectF> lineRects;

    int curLine = -1;
    QRectF curRect;
    for (int i = lo; i <= hi; ++i) {
        const FlatTextChar &fc = m_pageChars.at(i);
        if (i > lo && fc.lineBreakBefore)
            text.append('\n');
        text.append(fc.ch);
        if (i < hi && fc.spaceAfter && !m_pageChars.at(i + 1).lineBreakBefore)
            text.append(' ');

        if (fc.lineIndex != curLine) {
            if (curLine >= 0 && !curRect.isEmpty())
                lineRects.append(curRect);
            curLine = fc.lineIndex;
            curRect = fc.pdfNormRect;
        } else {
            curRect = curRect.united(fc.pdfNormRect);
        }
    }
    if (curLine >= 0 && !curRect.isEmpty())
        lineRects.append(curRect);

    m_selectedText = PdfDocument::containsThai(text)
                         ? PdfDocument::normalizeThaiText(text, false, false)
                         : text;
    m_textSelPdfRects = std::move(lineRects);
    emit textSelectionChanged();
}

int Backend::findNearestCharIndex(const QPointF &pdfPt) const {
    if (m_pageChars.isEmpty())
        return -1;

    // First prefer an exact hit inside a character box.
    for (int i = 0; i < m_pageChars.size(); ++i) {
        if (m_pageChars.at(i).pdfNormRect.contains(pdfPt))
            return i;
    }

    // Otherwise weight vertical distance more heavily than horizontal so dragging
    // across a line stays locked to that line before jumping vertically.
    int bestIdx = -1;
    double bestScore = 1e18;
    for (int i = 0; i < m_pageChars.size(); ++i) {
        const QRectF &r = m_pageChars.at(i).pdfNormRect;
        const double dx = pdfPt.x() < r.left() ? (r.left() - pdfPt.x())
                        : (pdfPt.x() > r.right() ? (pdfPt.x() - r.right()) : 0.0);
        const double dy = pdfPt.y() < r.top() ? (r.top() - pdfPt.y())
                        : (pdfPt.y() > r.bottom() ? (pdfPt.y() - r.bottom()) : 0.0);
        const double score = dx * dx + (dy * 5.0) * (dy * 5.0);
        if (score < bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }
    return bestIdx;
}

int Backend::findEntryIndexForOutline(const QString &sourcePath, int sourcePageIndex) const {
    if (sourcePageIndex < 0)
        return -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        const PageEntry &e = m_entries.at(i);
        if (e.isPdfPage && e.sourcePath == sourcePath && e.sourcePageIndex == sourcePageIndex)
            return i;
    }
    return -1;
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
