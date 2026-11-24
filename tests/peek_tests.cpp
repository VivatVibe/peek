#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "cli.h"
#include "filepicker.h"
#include "imageops.h"
#include "inspector.h"
#include "markup.h"
#include "pdfdocument.h"
#include "presetstore.h"
#include "printrenderer.h"
#include "theme.h"
#include "translator.h"

namespace {

class FakeFilePicker : public FilePicker {
public:
    QStringList nextOpenPaths;
    QString nextSavePath;

    void openImages(const QString &,
                    std::function<void(const QStringList &paths)> onSelected,
                    std::function<void(const QString &error)>) override {
        if (onSelected && !nextOpenPaths.isEmpty())
            onSelected(nextOpenPaths);
    }

    void savePdf(const QString &,
                 const QString &,
                 const QString &,
                 std::function<void(const QString &path)> onSelected,
                 std::function<void(const QString &error)>) override {
        if (onSelected && !nextSavePath.isEmpty())
            onSelected(nextSavePath);
    }

    void saveFile(const QString &,
                  const QString &,
                  const QString &,
                  const QString &,
                  const QString &,
                  std::function<void(const QString &path)> onSelected,
                  std::function<void(const QString &error)>) override {
        if (onSelected && !nextSavePath.isEmpty())
            onSelected(nextSavePath);
    }
};

} // namespace

class PeekTests : public QObject {
    Q_OBJECT

private slots:
    void opensImageAndNavigatesSiblings() {
        QTemporaryDir dir;
        const QString p1 = makeImage(dir.path(), "01.png", 400, 200, Qt::red);
        const QString p2 = makeImage(dir.path(), "02.png", 200, 400, Qt::blue);
        QVERIFY(!p1.isEmpty() && !p2.isEmpty());

        FakeFilePicker picker;
        Backend backend(&picker, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", dir.filePath("presets.json"));
        QVERIFY(backend.openFiles({p1}));
        QVERIFY(backend.hasImage());
        QCOMPARE(backend.fileName(), QString("01.png"));
        QCOMPARE(backend.imageCount(), 2);
        QCOMPARE(backend.imageIndex(), 0);
        QCOMPARE(backend.imageWidth(), 400);
        QCOMPARE(backend.imageHeight(), 200);
        QVERIFY(backend.effectiveLandscape());

        backend.nextImage();
        QCOMPARE(backend.fileName(), QString("02.png"));
        QCOMPARE(backend.imageIndex(), 1);
        QVERIFY(!backend.effectiveLandscape());

        backend.previousImage();
        QCOMPARE(backend.fileName(), QString("01.png"));
    }

    void rotatesFlipsAndUndoesWithoutTouchingOriginal() {
        QTemporaryDir dir;
        const QString path = makeImage(dir.path(), "sample.png", 300, 150, Qt::green);
        const QByteArray originalBytes = readFile(path);

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", dir.filePath("presets.json"));
        QVERIFY(backend.openFiles({path}));
        QCOMPARE(backend.imageWidth(), 300);
        QCOMPARE(backend.imageHeight(), 150);

        backend.rotateRight();
        QCOMPARE(backend.imageWidth(), 150);
        QCOMPARE(backend.imageHeight(), 300);
        QVERIFY(backend.canUndo());
        QVERIFY(!backend.canRedo());

        backend.toggleFlipHorizontal();
        backend.undo();
        QCOMPARE(backend.rotation(), 90);
        QVERIFY(backend.canRedo());

        backend.undo();
        QCOMPARE(backend.imageWidth(), 300);
        QCOMPARE(backend.imageHeight(), 150);
        QVERIFY(!backend.canUndo());

        backend.redo();
        QCOMPARE(backend.rotation(), 90);

        QCOMPARE(readFile(path), originalBytes);
    }

    void cropsResizesAdjustsColorAndExportsAtomically() {
        QTemporaryDir dir;
        // Build a muted dark-gray image so auto-levels and color adjustments produce measurable changes.
        QImage src(400, 200, QImage::Format_ARGB32);
        for (int y = 0; y < src.height(); ++y) {
            for (int x = 0; x < src.width(); ++x)
                src.setPixelColor(x, y, QColor(60 + (x % 40), 70 + (y % 40), 90));
        }
        const QString imgPath = dir.filePath("dark.png");
        QVERIFY(src.save(imgPath));
        const QByteArray originalBytes = readFile(imgPath);

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", dir.filePath("presets.json"));
        QVERIFY(backend.openFiles({imgPath}));
        QVERIFY(!backend.hasUnsavedEdits());

        // Crop to center 200x100.
        backend.setSelectionRect(0.25, 0.25, 0.5, 0.5);
        backend.cropToSelection();
        QVERIFY(!backend.hasSelection());
        QCOMPARE(backend.imageWidth(), 200);
        QCOMPARE(backend.imageHeight(), 100);
        QVERIFY(backend.hasUnsavedEdits());

        // Resize to 100x50.
        backend.applyResize(100, 50);
        QCOMPARE(backend.imageWidth(), 100);
        QCOMPARE(backend.imageHeight(), 50);
        QCOMPARE(backend.nativeWidth(), 200);
        QCOMPARE(backend.nativeHeight(), 100);

        // Live color edit with single undo step.
        const QRgb beforeColor = backend.displayImage().pixel(10, 10);
        backend.beginColorEdit();
        backend.setColorAdjustments(0.5, 0.2, 0.0, 0.1);
        backend.commitColorEdit();
        QVERIFY(backend.hasColorAdjustments());
        QVERIFY(qRed(backend.displayImage().pixel(10, 10)) > qRed(beforeColor));

        // Auto levels produces non-identity adjustments for low-contrast image.
        backend.autoLevels();
        QVERIFY(backend.hasColorAdjustments());

        // Export to JPEG atomically and verify unsaved flag clears.
        backend.setExportFormat("jpeg");
        backend.setExportQuality(85);
        QVERIFY(backend.exportSupportsQuality());
        QVERIFY(backend.estimatedExportBytes() > 100);

        const QString outJpg = dir.filePath("converted.jpg");
        QVERIFY(backend.exportImageTo(outJpg));
        QVERIFY(!backend.hasUnsavedEdits());

        QImageReader check(outJpg);
        QCOMPARE(check.format(), QByteArray("jpeg"));
        QCOMPARE(check.size(), QSize(100, 50));

        // Undo color + resize + crop restores original dimensions.
        backend.undo(); // undo autoLevels
        backend.undo(); // undo manual color
        backend.undo(); // undo resize
        QCOMPARE(backend.imageWidth(), 200);
        backend.undo(); // undo crop
        QCOMPARE(backend.imageWidth(), 400);
        QCOMPARE(backend.imageHeight(), 200);

        // Original file on disk must never be modified.
        QCOMPARE(readFile(imgPath), originalBytes);
    }

    void printRendererComputesFitFillAndScale() {
        const QRectF area(0, 0, 200, 300);
        const QSize imgSize(400, 200);

        PrintSettings fit;
        fit.scaleMode = "fit";
        const QRectF fitRect = PrintRenderer::computeImageTargetRect(area, imgSize, fit);
        QCOMPARE(fitRect.width(), 200.0);
        QCOMPARE(fitRect.height(), 100.0);
        QCOMPARE(fitRect.y(), 100.0);

        PrintSettings fill;
        fill.scaleMode = "fill";
        const QRectF fillRect = PrintRenderer::computeImageTargetRect(area, imgSize, fill);
        QCOMPARE(fillRect.width(), 600.0);
        QCOMPARE(fillRect.height(), 300.0);
        QCOMPARE(fillRect.x(), -200.0);

        PrintSettings custom;
        custom.scaleMode = "scale";
        custom.scalePercent = 50;
        const QRectF customRect = PrintRenderer::computeImageTargetRect(area, imgSize, custom);
        QCOMPARE(customRect.width(), 100.0);
        QCOMPARE(customRect.height(), 50.0);
    }

    void printRendererComputesNUpGridsAndPagination() {
        QCOMPARE(PrintRenderer::gridForPerPage(1, false), qMakePair(1, 1));
        QCOMPARE(PrintRenderer::gridForPerPage(2, true), qMakePair(2, 1));
        QCOMPARE(PrintRenderer::gridForPerPage(2, false), qMakePair(1, 2));
        QCOMPARE(PrintRenderer::gridForPerPage(4, true), qMakePair(2, 2));
        QCOMPARE(PrintRenderer::gridForPerPage(6, true), qMakePair(3, 2));
        QCOMPARE(PrintRenderer::gridForPerPage(6, false), qMakePair(2, 3));
        QCOMPARE(PrintRenderer::gridForPerPage(9, false), qMakePair(3, 3));
        QCOMPARE(PrintRenderer::gridForPerPage(12, true), qMakePair(4, 3));
        QCOMPARE(PrintRenderer::gridForPerPage(12, false), qMakePair(3, 4));
        QCOMPARE(PrintRenderer::gridForPerPage(16, true), qMakePair(4, 4));

        const QVector<QRectF> cells = PrintRenderer::computeGridCellRects(QRectF(10, 10, 210, 210), 2, 2, 10, 10);
        QCOMPARE(cells.size(), 4);
        QCOMPARE(cells.at(0), QRectF(10, 10, 100, 100));
        QCOMPARE(cells.at(3), QRectF(120, 120, 100, 100));

        QImage a(100, 100, QImage::Format_ARGB32);
        a.fill(Qt::red);
        const auto repeated = PrintRenderer::paginateImages({a}, 16, true);
        QCOMPARE(repeated.size(), 1);
        QCOMPARE(repeated.first().size(), 16);

        QVector<QImage> five(5, a);
        const auto batched = PrintRenderer::paginateImages(five, 4, false);
        QCOMPARE(batched.size(), 2);
        QCOMPARE(batched.at(0).size(), 4);
        QCOMPARE(batched.at(1).size(), 1);
    }

    void multiSelectAndSelectionCropWorkForPrinting() {
        QTemporaryDir dir;
        QStringList paths;
        for (int i = 1; i <= 5; ++i)
            paths.append(makeImage(dir.path(), QString("0%1.png").arg(i), 400, 200, Qt::blue));

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", dir.filePath("presets.json"));
        QVERIFY(backend.openFiles({paths.first()}));
        QCOMPARE(backend.imageCount(), 5);

        backend.setSelectionRect(0.25, 0.25, 0.5, 0.5);
        QVERIFY(backend.hasSelection());
        QVERIFY(backend.printSelectionOnly());
        QCOMPARE(backend.selectionPixelWidth(), 200);
        QCOMPARE(backend.selectionPixelHeight(), 100);
        QCOMPARE(backend.printableImage().size(), QSize(200, 100));

        backend.setPrintSelectionOnly(false);
        QCOMPARE(backend.printableImage().size(), QSize(400, 200));

        backend.selectAllFiles();
        QCOMPARE(backend.selectedCount(), 5);
        backend.preparePrintSheet();
        QCOMPARE(backend.multiMode(), QString("selected"));
        backend.setPerPage(4);
        QCOMPARE(backend.previewPageCount(), 2);
        QCOMPARE(backend.previewPageImages().size(), 4);
        backend.nextPreviewPage();
        QCOMPARE(backend.previewPageIndex(), 1);
        QCOMPARE(backend.previewPageImages().size(), 1);

        const QString pdfPath = dir.filePath("batch.pdf");
        backend.setCutGuides(true);
        QVERIFY(backend.savePdfTo(pdfPath));
        const QByteArray bytes = readFile(pdfPath);
        QVERIFY(bytes.startsWith("%PDF-"));
        QVERIFY(bytes.contains("/Count 2"));
    }

    void discoversPrintersCupsOptionsAndSubmitsLpJob() {
        QTemporaryDir dir;
        const QString imgPath = makeImage(dir.path(), "doc.png", 400, 600, Qt::yellow);
        const QString fakeLpstat = dir.filePath("lpstat");
        const QString fakeLpoptions = dir.filePath("lpoptions");
        const QString fakeLp = dir.filePath("lp");
        const QString argsLog = dir.filePath("lp_args.txt");

        writeExecutable(fakeLpstat,
                        "#!/bin/sh\n"
                        "echo 'printer EPSON-L385-Series is idle. enabled since today'\n"
                        "echo 'printer Office-Laser is idle. enabled since today'\n"
                        "echo 'system default destination: EPSON-L385-Series'\n");

        writeExecutable(fakeLpoptions,
                        "#!/bin/sh\n"
                        "echo 'MediaType/Type: *PLAIN EMATTE EPREMGLOSS ENVELOPE'\n"
                        "echo 'PrintQuality/Quality Option: Draft *Standard High'\n"
                        "echo 'Duplex/2-Sided Printing: *None DuplexNoTumble DuplexTumble'\n");

        writeExecutable(fakeLp,
                        "#!/bin/sh\n"
                        "printf '%s\\n' \"$@\" > '" + argsLog.toUtf8() + "'\n"
                        "echo 'request id is EPSON-L385-Series-42 (1 file(s))'\n");

        Backend backend(nullptr, fakeLpstat, fakeLp, fakeLpoptions, dir.filePath("presets.json"));
        QTRY_COMPARE(backend.printers().size(), 2);
        QCOMPARE(backend.defaultPrinter(), QString("EPSON-L385-Series"));
        QCOMPARE(backend.printer(), QString("EPSON-L385-Series"));

        QTRY_VERIFY(backend.hasPrinterOptions());
        QCOMPARE(backend.mediaType(), QString("PLAIN"));
        QCOMPARE(backend.printQuality(), QString("Standard"));
        QCOMPARE(backend.duplex(), QString("None"));

        QVERIFY(backend.openFiles({imgPath}));
        backend.setCopies(2);
        backend.setPaperSize("4x6");
        backend.setGrayscale(true);
        backend.setMediaType("EPREMGLOSS");
        backend.setPrintQuality("High");
        backend.printCurrent();

        QTRY_VERIFY(!backend.printBusy());
        QVERIFY(backend.statusMessage().contains("EPSON-L385-Series-42"));

        const QString logged = QString::fromUtf8(readFile(argsLog));
        QVERIFY(logged.contains("-d\nEPSON-L385-Series"));
        QVERIFY(logged.contains("-n\n2"));
        QVERIFY(logged.contains("media=4x6in"));
        QVERIFY(logged.contains("print-scaling=none"));
        QVERIFY(logged.contains("ColorModel=Gray"));
        QVERIFY(logged.contains("MediaType=EPREMGLOSS"));
        QVERIFY(logged.contains("PrintQuality=High"));
    }

    void remembersLastUsedSettingsAndManagesCustomPresets() {
        QTemporaryDir dir;
        const QString storePath = dir.filePath("presets.json");
        const QString fakeLpstat = dir.filePath("lpstat");
        const QString fakeLpoptions = dir.filePath("lpoptions");

        writeExecutable(fakeLpstat,
                        "#!/bin/sh\n"
                        "echo 'printer EPSON-L385-Series is idle. enabled since today'\n"
                        "echo 'printer Office-Laser is idle. enabled since today'\n"
                        "echo 'system default destination: Office-Laser'\n");

        writeExecutable(fakeLpoptions,
                        "#!/bin/sh\n"
                        "echo 'MediaType/Type: *PLAIN EMATTE EPREMGLOSS ENVELOPE'\n"
                        "echo 'PrintQuality/Quality Option: Draft *Standard High'\n");

        {
            Backend first(nullptr, fakeLpstat, "/nonexistent-lp", fakeLpoptions, storePath);
            QTRY_COMPARE(first.printers().size(), 2);
            QTRY_VERIFY(first.hasPrinterOptions());

            first.setPrinter("EPSON-L385-Series");
            QTRY_VERIFY(first.hasPrinterOptions());
            first.setPaperSize("4x6");
            first.setMarginMode("borderless");
            first.setScaleMode("fill");
            first.setGrayscale(true);
            first.setPerPage(6);
            first.setCutGuides(true);
            first.setCopies(4);
            first.setExportFormat("webp");
            first.setExportQuality(75);

            // Save as a named macOS-style custom preset.
            QVERIFY(first.saveCurrentPreset("ID Photo 6-Up"));
            QVERIFY(first.activePresetRemovable());
            QVERIFY(!first.activePresetModified());
            QCOMPARE(first.activePresetName(), QString("ID Photo 6-Up"));

            // Tweaking a setting marks the preset as modified.
            first.setGrayscale(false);
            QVERIFY(first.activePresetModified());

            // Built-in preset applies smart CUPS hints (glossy + high quality).
            first.applyPreset("builtin:photo-4x6");
            QCOMPARE(first.paperSize(), QString("4x6"));
            QCOMPARE(first.marginMode(), QString("borderless"));
            QCOMPARE(first.scaleMode(), QString("fill"));
            QCOMPARE(first.mediaType(), QString("EPREMGLOSS"));
            QCOMPARE(first.printQuality(), QString("High"));
        }

        // Relaunching Peek restores Last Used Settings (with copies reset to 1 for safety)
        // and keeps all saved custom presets.
        {
            Backend second(nullptr, fakeLpstat, "/nonexistent-lp", fakeLpoptions, storePath);
            QTRY_COMPARE(second.printers().size(), 2);
            QCOMPARE(second.printer(), QString("EPSON-L385-Series"));
            QCOMPARE(second.paperSize(), QString("4x6"));
            QCOMPARE(second.marginMode(), QString("borderless"));
            QCOMPARE(second.copies(), 1);
            QCOMPARE(second.exportFormat(), QString("webp"));
            QCOMPARE(second.exportQuality(), 75);

            // Re-apply saved custom preset and verify exact settings + copies.
            QString customId;
            for (const QVariant &v : second.presets()) {
                const QVariantMap row = v.toMap();
                if (row.value("name").toString() == "ID Photo 6-Up")
                    customId = row.value("id").toString();
            }
            QVERIFY(!customId.isEmpty());
            second.applyPreset(customId);
            QCOMPARE(second.perPage(), 6);
            QCOMPARE(second.copies(), 4);
            QVERIFY(second.grayscale());
            QVERIFY(second.cutGuides());

            // Deleting custom preset returns cleanly to Last Used Settings.
            QVERIFY(second.deleteActivePreset());
            QCOMPARE(second.activePresetId(), QString("last-used"));
            QVERIFY(!second.activePresetRemovable());
        }
    }

    void savesPersistsAndPrintsCustomPaperSizes() {
        QTemporaryDir dir;
        const QString storePath = dir.filePath("presets.json");
        const QString imgPath = makeImage(dir.path(), "label.png", 200, 300, Qt::darkCyan);

        double wMm = 0.0, hMm = 0.0;
        QVERIFY(PrintRenderer::parseCustomPaperDimensions("10x15cm", wMm, hMm));
        QCOMPARE(wMm, 100.0);
        QCOMPARE(hMm, 150.0);

        {
            Backend first(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", storePath);
            QVERIFY(first.openFiles({imgPath}));
            QVERIFY(first.saveCustomPaper("Sticker 10x15", 10.0, 15.0, "cm"));
            QVERIFY(first.activePaperCustom());
            QCOMPARE(first.activePaperName(), QString("Sticker 10x15"));
            QCOMPARE(first.paperWidthMm(), 100.0);
            QCOMPARE(first.paperHeightMm(), 150.0);

            const QStringList args = Backend::buildLpArguments("EPSON-L385-Series", first.printSettings(), "/tmp/x.pdf", "label.png");
            QVERIFY(args.contains("media=Custom.100.0x150.0mm"));

            const QString pdfPath = dir.filePath("custom_size.pdf");
            QVERIFY(first.savePdfTo(pdfPath));
            QVERIFY(readFile(pdfPath).startsWith("%PDF-"));
        }

        // Relaunch restores the custom paper size automatically.
        {
            Backend second(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", storePath);
            QVERIFY(second.openFiles({imgPath}));
            QVERIFY(second.activePaperCustom());
            QCOMPARE(second.activePaperName(), QString("Sticker 10x15"));
            QCOMPARE(second.paperWidthMm(), 100.0);
            QCOMPARE(second.paperHeightMm(), 150.0);

            QVERIFY(second.deleteActiveCustomPaper());
            QVERIFY(!second.activePaperCustom());
            QCOMPARE(second.paperSize(), QString("a4"));
        }

        // CLI accepts custom WxHmm directly.
        QStringList guiFiles;
        const QString cliPdf = dir.filePath("cli_custom.pdf");
        const auto code = Cli::run({"peek", "export", imgPath, "-o", cliPdf, "--paper", "100x150mm", "--json"}, guiFiles);
        QVERIFY(code.has_value());
        QCOMPARE(*code, 0);
        QVERIFY(readFile(cliPdf).startsWith("%PDF-"));
    }

    void markupAnnotationsRenderUndoAndClear() {
        QTemporaryDir dir;
        const QString imgPath = makeImage(dir.path(), "canvas.png", 400, 300, Qt::white);
        const QByteArray originalBytes = readFile(imgPath);

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions", dir.filePath("presets.json"));
        QVERIFY(backend.openFiles({imgPath}));
        QCOMPARE(backend.annotationCount(), 0);

        // Draw a solid black redaction over the center.
        MarkupDraftItem draft;
        draft.setTool("redact");
        draft.beginStroke(0.4, 0.4);
        draft.updateStroke(0.6, 0.6);
        backend.commitDraftAnnotation(&draft);

        QCOMPARE(backend.annotationCount(), 1);
        QVERIFY(backend.hasUnsavedEdits());
        QCOMPARE(QColor(backend.displayImage().pixel(200, 150)), QColor("#0a0a0c"));

        // Add a text callout annotation.
        backend.addTextAnnotation(0.2, 0.2, "Important", QColor("#ff453a"), "m");
        QCOMPARE(backend.annotationCount(), 2);

        // Undo restores count and pixel state.
        backend.undo();
        QCOMPARE(backend.annotationCount(), 1);
        backend.clearAnnotations();
        QCOMPARE(backend.annotationCount(), 0);
        QCOMPARE(QColor(backend.displayImage().pixel(200, 150)), QColor(Qt::white));

        // Original file on disk remains untouched.
        QCOMPARE(readFile(imgPath), originalBytes);
    }

    void inspectorReadsMetadataAndJpegExif() {
        QTemporaryDir dir;
        QImage img(600, 400, QImage::Format_RGB32);
        img.fill(Qt::darkBlue);
        const QString rawJpgPath = dir.filePath("raw.jpg");
        QVERIFY(img.save(rawJpgPath, "JPEG", 90));

        // Inject a minimal valid little-endian APP1 Exif segment right after SOI (FF D8).
        QByteArray rawJpeg = readFile(rawJpgPath);
        QVERIFY(rawJpeg.startsWith("\xFF\xD8"));

        QByteArray tiff;
        auto put16 = [&](uint16_t v) {
            tiff.append(static_cast<char>(v & 0xFF));
            tiff.append(static_cast<char>((v >> 8) & 0xFF));
        };
        auto put32 = [&](uint32_t v) {
            tiff.append(static_cast<char>(v & 0xFF));
            tiff.append(static_cast<char>((v >> 8) & 0xFF));
            tiff.append(static_cast<char>((v >> 16) & 0xFF));
            tiff.append(static_cast<char>((v >> 24) & 0xFF));
        };

        // TIFF header (8 bytes): "II", 42, IFD0 offset = 8.
        tiff.append("II", 2);
        put16(42);
        put32(8);

        // IFD0 at offset 8: 3 entries (2 + 36 + 4 = 42 bytes -> ends at offset 50).
        const uint32_t makeOff = 50;
        const QByteArray makeStr("Fujifilm\0", 9);
        const uint32_t modelOff = makeOff + static_cast<uint32_t>(makeStr.size());
        const QByteArray modelStr("X-T5\0", 5);
        const uint32_t exifIfdOff = modelOff + static_cast<uint32_t>(modelStr.size());

        put16(3);
        put16(0x010F); put16(2); put32(makeStr.size()); put32(makeOff);
        put16(0x0110); put16(2); put32(modelStr.size()); put32(modelOff);
        put16(0x8769); put16(4); put32(1); put32(exifIfdOff);
        put32(0); // next IFD

        tiff.append(makeStr);
        tiff.append(modelStr);
        QCOMPARE(static_cast<uint32_t>(tiff.size()), exifIfdOff);

        // ExifIFD: 4 entries (2 + 48 + 4 = 54 bytes).
        const uint32_t expOff = exifIfdOff + 54;
        const uint32_t fnumOff = expOff + 8;
        const uint32_t focalOff = fnumOff + 8;

        put16(4);
        put16(0x829A); put16(5); put32(1); put32(expOff);   // ExposureTime 1/250
        put16(0x829D); put16(5); put32(1); put32(fnumOff);  // FNumber 28/10
        put16(0x8827); put16(3); put32(1); put32(400);      // ISO 400 (inline)
        put16(0x920A); put16(5); put32(1); put32(focalOff); // FocalLength 35/1
        put32(0);

        put32(1); put32(250);
        put32(28); put32(10);
        put32(35); put32(1);

        QByteArray app1;
        app1.append('\xFF');
        app1.append('\xE1');
        const uint16_t segLen = static_cast<uint16_t>(2 + 6 + tiff.size());
        app1.append(static_cast<char>((segLen >> 8) & 0xFF));
        app1.append(static_cast<char>(segLen & 0xFF));
        app1.append("Exif\0\0", 6);
        app1.append(tiff);

        const QString exifJpgPath = dir.filePath("camera.jpg");
        QByteArray withExif = rawJpeg.left(2) + app1 + rawJpeg.mid(2);
        write(exifJpgPath, withExif);

        const QVariantMap meta = Inspector::readMetadata(exifJpgPath, img);
        QCOMPARE(meta.value("format").toString(), QString("JPEG"));
        QCOMPARE(meta.value("width").toInt(), 600);
        QCOMPARE(meta.value("height").toInt(), 400);
        QVERIFY(meta.value("hasExif").toBool());
        QCOMPARE(meta.value("camera").toString(), QString("Fujifilm X-T5"));
        QCOMPARE(meta.value("exposureTime").toString(), QString("1/250 s"));
        QCOMPARE(meta.value("aperture").toString(), QString("f/2.8"));
        QCOMPARE(meta.value("iso").toString(), QString("ISO 400"));
        QCOMPARE(meta.value("focalLength").toString(), QString("35 mm"));

        QStringList guiFiles;
        const auto code = Cli::run({"peek", "inspect", exifJpgPath, "--json"}, guiFiles);
        QVERIFY(code.has_value());
        QCOMPARE(*code, 0);
    }

    void cliExportsMultiUpPdfAndConvertedImageHeadlessly() {
        QTemporaryDir dir;
        const QString p1 = makeImage(dir.path(), "cli1.png", 500, 300, Qt::magenta);
        const QString p2 = makeImage(dir.path(), "cli2.png", 500, 300, Qt::cyan);
        const QString outPdf = dir.filePath("cli_out.pdf");
        const QString outJpg = dir.filePath("cli_edited.jpg");

        QStringList guiFiles;
        const auto codePdf = Cli::run({"peek", "export", p1, p2, "-o", outPdf,
                                       "--paper", "letter", "--per-page", "2", "--cut-guides", "--grayscale", "--json"},
                                      guiFiles);
        QVERIFY(codePdf.has_value());
        QCOMPARE(*codePdf, 0);
        QVERIFY(readFile(outPdf).startsWith("%PDF-"));

        const auto codeImg = Cli::run({"peek", "export", p1, "-o", outJpg,
                                       "--crop", "50,50,200,100", "--resize", "100x50", "--quality", "80", "--json"},
                                      guiFiles);
        QVERIFY(codeImg.has_value());
        QCOMPARE(*codeImg, 0);
        QImageReader reader(outJpg);
        QCOMPARE(reader.format(), QByteArray("jpeg"));
        QCOMPARE(reader.size(), QSize(100, 50));
    }

    void pdfRangeParserHandlesRangesAndErrors() {
        bool ok = false;
        QCOMPARE(PdfDocument::parsePageRange("", 4, &ok), QVector<int>({0, 1, 2, 3}));
        QVERIFY(ok);

        QCOMPARE(PdfDocument::parsePageRange("1-3, 5", 6, &ok), QVector<int>({0, 1, 2, 4}));
        QVERIFY(ok);

        // Deduplicates overlapping entries while preserving order.
        QCOMPARE(PdfDocument::parsePageRange("3, 1-3, 2", 5, &ok), QVector<int>({2, 0, 1}));
        QVERIFY(ok);

        // Rejects out-of-range, inverted, or malformed syntax.
        QVERIFY(PdfDocument::parsePageRange("0-2", 5, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(PdfDocument::parsePageRange("4-2", 5, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(PdfDocument::parsePageRange("1-6", 5, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(PdfDocument::parsePageRange("1,,3", 5, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(PdfDocument::parsePageRange("abc", 5, &ok).isEmpty());
        QVERIFY(!ok);
    }

    void opensReordersDeletesMergesAndPrintsPdfPages() {
        QTemporaryDir dir;
        const QString imgRed = makeImage(dir.path(), "r.png", 320, 420, QColor("#e63946"));
        const QString imgGreen = makeImage(dir.path(), "g.png", 320, 420, QColor("#2a9d8f"));
        const QString imgBlue = makeImage(dir.path(), "b.png", 320, 420, QColor("#457b9d"));
        const QString imgGold = makeImage(dir.path(), "y.png", 320, 420, QColor("#e9c46a"));

        PrintSettings ps;
        ps.paperSize = "a4";
        ps.orientation = "portrait";

        const QString pdfA = dir.filePath("docA.pdf");
        QVERIFY(PrintRenderer::exportPdf(pdfA, {{QImage(imgRed)}, {QImage(imgGreen)}, {QImage(imgBlue)}}, ps));
        const QString pdfB = dir.filePath("docB.pdf");
        QVERIFY(PrintRenderer::exportPdf(pdfB, QImage(imgGold), ps));

        const QByteArray origPdfABytes = readFile(pdfA);
        QVERIFY(PdfDocument::isPdfFile(pdfA));
        QCOMPARE(PdfDocument::pageCount(pdfA), 3);

        Backend backend(nullptr, "/nonexistent/lpstat", "/bin/true", "/nonexistent/lpoptions", dir.filePath("presets.json"));
        QVERIFY(backend.openFiles({pdfA}));
        QVERIFY(backend.isPdfMode());
        QCOMPARE(backend.imageCount(), 3);
        QCOMPARE(backend.imageIndex(), 0);
        QCOMPARE(backend.pageRangeMode(), QString("all"));
        QCOMPARE(backend.previewPageCount(), 3);
        QVERIFY(backend.metadata().value("isPdf").toBool());
        QCOMPARE(backend.metadata().value("pdfPageCount").toInt(), 3);
        QVERIFY(!backend.thumbnailForIndex(1).isNull());

        // Rotate page 1 (index 0) and verify edit persists when switching pages.
        backend.rotateRight();
        QCOMPARE(backend.rotation(), 90);
        backend.nextImage();
        QCOMPARE(backend.imageIndex(), 1);
        QCOMPARE(backend.rotation(), 0);
        backend.previousImage();
        QCOMPARE(backend.rotation(), 90);

        // Move page 1 down to position 2 (index 1) and verify it stays current.
        QVERIFY(backend.moveCurrentPageDown());
        QCOMPARE(backend.imageIndex(), 1);
        QVERIFY(backend.hasDocumentChanges());
        QVERIFY(backend.hasUnsavedEdits());

        // Delete page 3 (index 2) -> 2 pages left.
        QVERIFY(backend.deletePage(2));
        QCOMPARE(backend.imageCount(), 2);

        // Merge pdfB -> 3 pages total.
        QVERIFY(backend.appendFiles({pdfB}));
        QCOMPARE(backend.imageCount(), 3);

        // Print page range "1, 3" -> 2 preview pages at 1-up, 1 preview page at 2-up.
        backend.setPageRangeMode("range");
        backend.setPageRangeText("1, 3");
        QVERIFY(backend.pageRangeValid());
        QCOMPARE(backend.activePageCount(), 2);
        QCOMPARE(backend.previewPageCount(), 2);

        backend.setPerPage(2);
        QCOMPARE(backend.previewPageCount(), 1);
        backend.setPerPage(1);

        // Invalid range is rejected cleanly.
        backend.setPageRangeText("1-9");
        QVERIFY(!backend.pageRangeValid());
        QVERIFY(!backend.savePdfTo(dir.filePath("bad.pdf")));

        // Save organized all pages to a new PDF and confirm originals are untouched.
        backend.setPageRangeMode("all");
        const QString savedMerged = dir.filePath("organized.pdf");
        QVERIFY(backend.savePdfTo(savedMerged));
        QCOMPARE(PdfDocument::pageCount(savedMerged), 3);
        QVERIFY(!backend.hasDocumentChanges());
        QCOMPARE(readFile(pdfA), origPdfABytes);

        // CLI: inspect PDF & export merged PDFs with --pages.
        QStringList guiFiles;
        const auto inspectCode = Cli::run({"peek", "inspect", pdfA, "--json"}, guiFiles);
        QVERIFY(inspectCode.has_value());
        QCOMPARE(*inspectCode, 0);

        const QString cliMerged = dir.filePath("cli_slice.pdf");
        const auto mergeCode = Cli::run({"peek", "export", pdfA, pdfB, "--pages", "1,4", "-o", cliMerged, "--json"}, guiFiles);
        QVERIFY(mergeCode.has_value());
        QCOMPARE(*mergeCode, 0);
        QCOMPARE(PdfDocument::pageCount(cliMerged), 2);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json");
        const QString command = dir.filePath("hyprctl");
        writeExecutable(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":8}");
        QTRY_COMPARE(theme.radius(), 8);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Print").isNull());
    }

    void translatorFollowsLanguageAndPlurals() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "da\n");
        write(dir.path() + "/en.json", R"({"%n copies": {"one": "%n copy", "other": "%n copies"}})");
        write(dir.path() + "/da.json", R"({"Print": "Udskriv", "%n copies": {"one": "%n kopi", "other": "%n kopier"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("da"));
        QCOMPARE(translator.translate("Main", "Print"), QString("Udskriv"));
        QCOMPARE(translator.translate("Main", "%n copies", nullptr, 1), QString("%n kopi"));
        QCOMPARE(translator.translate("Main", "%n copies", nullptr, 3), QString("%n kopier"));
        QCOMPARE(Translator::pluralCategory("th", 2), QString("other"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
    }

private:
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
    }

    static void writeExecutable(const QString &path, const QByteArray &contents) {
        write(path, contents);
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }

    static QByteArray readFile(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QString makeImage(const QString &dir, const QString &name, int w, int h, const QColor &color) {
        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(color);
        const QString fullPath = QDir(dir).filePath(name);
        return img.save(fullPath) ? fullPath : QString();
    }
};

QTEST_MAIN(PeekTests)
#include "peek_tests.moc"
