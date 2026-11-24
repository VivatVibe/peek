#include "backend.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace {

const QStringList &supportedExtensions() {
    static const QStringList exts = {
        "png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff", "svg", "avif", "heic", "ico"
    };
    return exts;
}

} // namespace

Backend::Backend(FilePicker *filePicker, const QString &lpstatPath, const QString &lpPath, QObject *parent)
    : QObject(parent),
      m_filePicker(filePicker),
      m_lpstatPath(!lpstatPath.isEmpty() ? lpstatPath : QStandardPaths::findExecutable("lpstat")),
      m_lpPath(!lpPath.isEmpty() ? lpPath : QStandardPaths::findExecutable("lp")) {
    connect(&m_lpstatProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString stdoutText = QString::fromUtf8(m_lpstatProcess.readAllStandardOutput());
        const QString stderrText = QString::fromUtf8(m_lpstatProcess.readAllStandardError()).trimmed();

        QStringList found;
        QString defPrinter;
        parseLpstatOutput(stdoutText, found, defPrinter);

        m_printers = found;
        m_defaultPrinter = defPrinter;

        if (m_printers.isEmpty()) {
            if (code != 0 || status != QProcess::NormalExit) {
                m_printersError = stderrText.isEmpty()
                                      ? tr("CUPS printing service is not responding.")
                                      : stderrText;
            } else {
                m_printersError = tr("No printers configured in CUPS.");
            }
            m_selectedPrinter.clear();
        } else {
            m_printersError.clear();
            if (m_selectedPrinter.isEmpty() || !m_printers.contains(m_selectedPrinter))
                m_selectedPrinter = !m_defaultPrinter.isEmpty() && m_printers.contains(m_defaultPrinter)
                                        ? m_defaultPrinter
                                        : m_printers.first();
        }

        emit printersChanged();
        emit printSettingsChanged();
    });

    connect(&m_lpstatProcess, &QProcess::errorOccurred, this, [this] {
        m_printers.clear();
        m_defaultPrinter.clear();
        m_selectedPrinter.clear();
        m_printersError = tr("Could not run lpstat to list printers.");
        emit printersChanged();
        emit printSettingsChanged();
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
    if (m_lpstatProcess.state() != QProcess::NotRunning) {
        m_lpstatProcess.kill();
        m_lpstatProcess.waitForFinished(1000);
    }
    if (m_lpProcess.state() != QProcess::NotRunning) {
        m_lpProcess.kill();
        m_lpProcess.waitForFinished(1000);
    }
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

QImage Backend::printableImage() const {
    return PrintRenderer::applyTransforms(m_sourceImage,
                                          m_transform.rotation,
                                          m_transform.flipH,
                                          m_transform.flipV,
                                          m_printSettings.grayscale);
}

void Backend::setPrinter(const QString &name) {
    if (name == m_selectedPrinter)
        return;
    m_selectedPrinter = name;
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

bool Backend::effectiveLandscape() const {
    return PrintRenderer::resolveLandscape(m_printSettings, m_displayImage.size());
}

double Backend::paperWidthMm() const {
    return PrintRenderer::effectivePageSizeMm(m_printSettings, m_displayImage.size()).width();
}

double Backend::paperHeightMm() const {
    return PrintRenderer::effectivePageSizeMm(m_printSettings, m_displayImage.size()).height();
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

void Backend::rotateLeft() {
    if (!hasImage())
        return;
    TransformState next = m_transform;
    next.rotation = (next.rotation + 270) % 360;
    applyTransformState(next, true);
}

void Backend::rotateRight() {
    if (!hasImage())
        return;
    TransformState next = m_transform;
    next.rotation = (next.rotation + 90) % 360;
    applyTransformState(next, true);
}

void Backend::toggleFlipHorizontal() {
    if (!hasImage())
        return;
    TransformState next = m_transform;
    next.flipH = !next.flipH;
    applyTransformState(next, true);
}

void Backend::toggleFlipVertical() {
    if (!hasImage())
        return;
    TransformState next = m_transform;
    next.flipV = !next.flipV;
    applyTransformState(next, true);
}

void Backend::resetTransforms() {
    if (!hasImage() || !isTransformed())
        return;
    applyTransformState(TransformState{}, true);
}

void Backend::undo() {
    if (m_undoStack.isEmpty())
        return;
    m_redoStack.push_back(m_transform);
    const TransformState previous = m_undoStack.takeLast();
    applyTransformState(previous, false);
}

void Backend::redo() {
    if (m_redoStack.isEmpty())
        return;
    m_undoStack.push_back(m_transform);
    const TransformState next = m_redoStack.takeLast();
    applyTransformState(next, false);
}

void Backend::refreshPrinters() {
    if (m_lpstatPath.isEmpty()) {
        m_printers.clear();
        m_defaultPrinter.clear();
        m_selectedPrinter.clear();
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

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString tempPdf = QDir(tempDir).filePath(
        "peek-print-" + QUuid::createUuid().toString(QUuid::Id128) + ".pdf");

    QString exportError;
    if (!PrintRenderer::exportPdf(tempPdf, printableImage(), m_printSettings, &exportError)) {
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
    if (!PrintRenderer::exportPdf(finalPath, printableImage(), m_printSettings, &err)) {
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
    rebuildDisplayImage();
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
