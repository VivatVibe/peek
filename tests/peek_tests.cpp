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
        Backend backend(&picker, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions");
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

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions");
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

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions");
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

        const QVector<QRectF> cells = PrintRenderer::computeGridCellRects(QRectF(10, 10, 210, 210), 2, 2, 10, 10);
        QCOMPARE(cells.size(), 4);
        QCOMPARE(cells.at(0), QRectF(10, 10, 100, 100));
        QCOMPARE(cells.at(3), QRectF(120, 120, 100, 100));

        QImage a(100, 100, QImage::Format_ARGB32);
        a.fill(Qt::red);
        const auto repeated = PrintRenderer::paginateImages({a}, 4, true);
        QCOMPARE(repeated.size(), 1);
        QCOMPARE(repeated.first().size(), 4);

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

        Backend backend(nullptr, "/nonexistent-lpstat", "/nonexistent-lp", "/nonexistent-lpoptions");
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

        Backend backend(nullptr, fakeLpstat, fakeLp, fakeLpoptions);
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
