#pragma once

#include "filepicker.h"
#include "imageops.h"
#include "presetstore.h"
#include "printrenderer.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QQuickImageProvider>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVector>

struct CupsOption {
    QString key;
    QStringList values;
    QString defaultValue;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY imageChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY imageChanged)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(int nativeWidth READ nativeWidth NOTIFY imageChanged)
    Q_PROPERTY(int nativeHeight READ nativeHeight NOTIFY imageChanged)
    Q_PROPERTY(int imageVersion READ imageVersion NOTIFY imageChanged)
    Q_PROPERTY(int fileListVersion READ fileListVersion NOTIFY filesChanged)
    Q_PROPERTY(int imageIndex READ imageIndex NOTIFY imageChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY filesChanged)
    Q_PROPERTY(QVariantList fileItems READ fileItems NOTIFY filesChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY filesChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY imageChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY imageChanged)
    Q_PROPERTY(bool isTransformed READ isTransformed NOTIFY imageChanged)
    Q_PROPERTY(bool hasUnsavedEdits READ hasUnsavedEdits NOTIFY imageChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY imageChanged)
    Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY imageChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY imageChanged)

    // Color adjustments (-1.0 .. +1.0).
    Q_PROPERTY(double exposure READ exposure NOTIFY colorChanged)
    Q_PROPERTY(double contrast READ contrast NOTIFY colorChanged)
    Q_PROPERTY(double saturation READ saturation NOTIFY colorChanged)
    Q_PROPERTY(double brightness READ brightness NOTIFY colorChanged)
    Q_PROPERTY(bool hasColorAdjustments READ hasColorAdjustments NOTIFY colorChanged)

    // Export / convert format settings.
    Q_PROPERTY(QString exportFormat READ exportFormat WRITE setExportFormat NOTIFY exportSettingsChanged)
    Q_PROPERTY(int exportQuality READ exportQuality WRITE setExportQuality NOTIFY exportSettingsChanged)
    Q_PROPERTY(bool exportSupportsQuality READ exportSupportsQuality NOTIFY exportSettingsChanged)
    Q_PROPERTY(qint64 estimatedExportBytes READ estimatedExportBytes NOTIFY exportSettingsChanged)

    // Selection region in normalized image coordinates (0..1).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QRectF selectionNorm READ selectionNorm NOTIFY selectionChanged)
    Q_PROPERTY(int selectionPixelWidth READ selectionPixelWidth NOTIFY selectionChanged)
    Q_PROPERTY(int selectionPixelHeight READ selectionPixelHeight NOTIFY selectionChanged)
    Q_PROPERTY(bool printSelectionOnly READ printSelectionOnly WRITE setPrintSelectionOnly NOTIFY printSettingsChanged)

    // Printer discovery & CUPS PPD options.
    Q_PROPERTY(QStringList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QString printer READ printer WRITE setPrinter NOTIFY printSettingsChanged)
    Q_PROPERTY(QString defaultPrinter READ defaultPrinter NOTIFY printersChanged)
    Q_PROPERTY(QString printersError READ printersError NOTIFY printersChanged)
    Q_PROPERTY(bool hasPrinterOptions READ hasPrinterOptions NOTIFY printerOptionsChanged)
    Q_PROPERTY(QVariantList mediaTypeOptions READ mediaTypeOptions NOTIFY printerOptionsChanged)
    Q_PROPERTY(QString mediaType READ mediaType WRITE setMediaType NOTIFY printSettingsChanged)
    Q_PROPERTY(QVariantList qualityOptions READ qualityOptions NOTIFY printerOptionsChanged)
    Q_PROPERTY(QString printQuality READ printQuality WRITE setPrintQuality NOTIFY printSettingsChanged)
    Q_PROPERTY(QVariantList duplexOptions READ duplexOptions NOTIFY printerOptionsChanged)
    Q_PROPERTY(QString duplex READ duplex WRITE setDuplex NOTIFY printSettingsChanged)

    // Print layout settings.
    Q_PROPERTY(int copies READ copies WRITE setCopies NOTIFY printSettingsChanged)
    Q_PROPERTY(QString paperSize READ paperSize WRITE setPaperSize NOTIFY printSettingsChanged)
    Q_PROPERTY(QString orientation READ orientation WRITE setOrientation NOTIFY printSettingsChanged)
    Q_PROPERTY(QString scaleMode READ scaleMode WRITE setScaleMode NOTIFY printSettingsChanged)
    Q_PROPERTY(int scalePercent READ scalePercent WRITE setScalePercent NOTIFY printSettingsChanged)
    Q_PROPERTY(QString marginMode READ marginMode WRITE setMarginMode NOTIFY printSettingsChanged)
    Q_PROPERTY(bool grayscale READ grayscale WRITE setGrayscale NOTIFY printSettingsChanged)
    Q_PROPERTY(int perPage READ perPage WRITE setPerPage NOTIFY printSettingsChanged)
    Q_PROPERTY(QString multiMode READ multiMode WRITE setMultiMode NOTIFY printSettingsChanged)
    Q_PROPERTY(bool cutGuides READ cutGuides WRITE setCutGuides NOTIFY printSettingsChanged)
    Q_PROPERTY(int previewPageIndex READ previewPageIndex WRITE setPreviewPageIndex NOTIFY printSettingsChanged)
    Q_PROPERTY(int previewPageCount READ previewPageCount NOTIFY printSettingsChanged)
    Q_PROPERTY(bool effectiveLandscape READ effectiveLandscape NOTIFY printSettingsChanged)
    Q_PROPERTY(double paperWidthMm READ paperWidthMm NOTIFY printSettingsChanged)
    Q_PROPERTY(double paperHeightMm READ paperHeightMm NOTIFY printSettingsChanged)

    Q_PROPERTY(bool printBusy READ printBusy NOTIFY printStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY messageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messageChanged)

    // macOS-style Print Presets, Custom Paper Sizes & Last Used memory.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString activePresetId READ activePresetId NOTIFY presetsChanged)
    Q_PROPERTY(QString activePresetName READ activePresetName NOTIFY presetsChanged)
    Q_PROPERTY(bool activePresetRemovable READ activePresetRemovable NOTIFY presetsChanged)
    Q_PROPERTY(bool activePresetModified READ activePresetModified NOTIFY presetsChanged)

    Q_PROPERTY(QVariantList paperOptions READ paperOptions NOTIFY papersChanged)
    Q_PROPERTY(bool activePaperCustom READ activePaperCustom NOTIFY printSettingsChanged)
    Q_PROPERTY(QString activePaperName READ activePaperName NOTIFY printSettingsChanged)
    Q_PROPERTY(double customPaperWidthMm READ customPaperWidthMm NOTIFY printSettingsChanged)
    Q_PROPERTY(double customPaperHeightMm READ customPaperHeightMm NOTIFY printSettingsChanged)

public:
    explicit Backend(FilePicker *filePicker = nullptr,
                     const QString &lpstatPath = {},
                     const QString &lpPath = {},
                     const QString &lpoptionsPath = {},
                     const QString &presetStorePath = {},
                     QObject *parent = nullptr);
    ~Backend() override;

    bool hasImage() const { return !m_displayImage.isNull(); }
    QString filePath() const;
    QString fileName() const;
    int imageWidth() const { return m_displayImage.width(); }
    int imageHeight() const { return m_displayImage.height(); }
    int nativeWidth() const { return m_preResizeSize.width(); }
    int nativeHeight() const { return m_preResizeSize.height(); }
    int imageVersion() const { return m_imageVersion; }
    int fileListVersion() const { return m_fileListVersion; }
    int imageIndex() const { return m_currentIndex; }
    int imageCount() const { return m_files.size(); }
    QVariantList fileItems() const;
    int selectedCount() const { return m_selectedIndices.size(); }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    bool isTransformed() const { return !m_edit.isIdentity(); }
    bool hasUnsavedEdits() const { return !m_edit.isIdentity() && m_editVersion != m_exportedEditVersion; }
    int annotationCount() const { return m_edit.annotations.size(); }
    QVariantMap metadata() const { return m_metadata; }
    int rotation() const { return m_edit.rotation; }

    double exposure() const { return m_edit.color.exposure; }
    double contrast() const { return m_edit.color.contrast; }
    double saturation() const { return m_edit.color.saturation; }
    double brightness() const { return m_edit.color.brightness; }
    bool hasColorAdjustments() const { return !m_edit.color.isIdentity(); }

    QString exportFormat() const { return m_exportFormat; }
    void setExportFormat(const QString &format);
    int exportQuality() const { return m_exportQuality; }
    void setExportQuality(int quality);
    bool exportSupportsQuality() const { return ImageOps::formatSupportsQuality(m_exportFormat); }
    qint64 estimatedExportBytes() const { return m_estimatedExportBytes; }

    bool hasSelection() const { return !m_selectionNorm.isEmpty(); }
    QRectF selectionNorm() const { return m_selectionNorm; }
    int selectionPixelWidth() const;
    int selectionPixelHeight() const;
    bool printSelectionOnly() const { return m_printSelectionOnly; }
    void setPrintSelectionOnly(bool enabled);

    QImage displayImage() const { return m_displayImage; }
    QImage thumbnailForIndex(int index) const;
    QImage printableImage() const;
    QVector<QVector<QImage>> buildPrintPages() const;
    QVector<QImage> previewPageImages() const;
    PrintSettings printSettings() const { return m_printSettings; }

    QStringList printers() const { return m_printers; }
    QString printer() const { return m_selectedPrinter; }
    void setPrinter(const QString &name);
    QString defaultPrinter() const { return m_defaultPrinter; }
    QString printersError() const { return m_printersError; }

    bool hasPrinterOptions() const;
    QVariantList mediaTypeOptions() const;
    QString mediaType() const { return m_printSettings.mediaTypeValue; }
    void setMediaType(const QString &value);
    QVariantList qualityOptions() const;
    QString printQuality() const { return m_printSettings.qualityValue; }
    void setPrintQuality(const QString &value);
    QVariantList duplexOptions() const;
    QString duplex() const { return m_printSettings.duplexValue; }
    void setDuplex(const QString &value);

    int copies() const { return m_printSettings.copies; }
    void setCopies(int count);
    QString paperSize() const { return m_printSettings.paperSize; }
    void setPaperSize(const QString &id);
    QString orientation() const { return m_printSettings.orientation; }
    void setOrientation(const QString &mode);
    QString scaleMode() const { return m_printSettings.scaleMode; }
    void setScaleMode(const QString &mode);
    int scalePercent() const { return m_printSettings.scalePercent; }
    void setScalePercent(int percent);
    QString marginMode() const { return m_printSettings.marginMode; }
    void setMarginMode(const QString &mode);
    bool grayscale() const { return m_printSettings.grayscale; }
    void setGrayscale(bool enabled);
    int perPage() const { return m_printSettings.perPage; }
    void setPerPage(int count);
    QString multiMode() const { return m_multiMode; }
    void setMultiMode(const QString &mode);
    bool cutGuides() const { return m_printSettings.cutGuides; }
    void setCutGuides(bool enabled);
    int previewPageIndex() const { return m_previewPageIndex; }
    void setPreviewPageIndex(int index);
    int previewPageCount() const;
    bool effectiveLandscape() const;
    double paperWidthMm() const;
    double paperHeightMm() const;

    bool printBusy() const { return m_printBusy; }
    QString statusMessage() const { return m_statusMessage; }
    QString errorMessage() const { return m_errorMessage; }

    QVariantList presets() const;
    QString activePresetId() const { return m_activePresetId; }
    QString activePresetName() const;
    bool activePresetRemovable() const;
    bool activePresetModified() const { return m_activePresetModified; }

    Q_INVOKABLE void applyPreset(const QString &id);
    Q_INVOKABLE bool saveCurrentPreset(const QString &name);
    Q_INVOKABLE bool deleteActivePreset();

    QVariantList paperOptions() const;
    bool activePaperCustom() const;
    QString activePaperName() const;
    double customPaperWidthMm() const;
    double customPaperHeightMm() const;
    Q_INVOKABLE bool saveCustomPaper(const QString &name, double width, double height, const QString &unit);
    Q_INVOKABLE bool deleteActiveCustomPaper();

    Q_INVOKABLE bool openFiles(const QStringList &paths);
    Q_INVOKABLE bool openUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void selectIndex(int index);
    Q_INVOKABLE void nextImage();
    Q_INVOKABLE void previousImage();

    Q_INVOKABLE void toggleFileSelected(int index);
    Q_INVOKABLE void selectRangeTo(int index);
    Q_INVOKABLE void selectAllFiles();
    Q_INVOKABLE void clearFileSelection();

    Q_INVOKABLE void setSelectionRect(double x, double y, double w, double h);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void cropToSelection();
    Q_INVOKABLE void copyToClipboard();

    Q_INVOKABLE void applyResize(int width, int height);
    Q_INVOKABLE void resetResize();

    Q_INVOKABLE void beginColorEdit();
    Q_INVOKABLE void setColorAdjustments(double exposure, double contrast, double saturation, double brightness);
    Q_INVOKABLE void commitColorEdit();
    Q_INVOKABLE void autoLevels();
    Q_INVOKABLE void resetColorAdjustments();

    Q_INVOKABLE void commitDraftAnnotation(QObject *draftItem);
    Q_INVOKABLE void addTextAnnotation(double nx, double ny, const QString &text, const QColor &color, const QString &strokeSize);
    Q_INVOKABLE void clearAnnotations();

    Q_INVOKABLE void rotateLeft();
    Q_INVOKABLE void rotateRight();
    Q_INVOKABLE void toggleFlipHorizontal();
    Q_INVOKABLE void toggleFlipVertical();
    Q_INVOKABLE void resetTransforms();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void refreshExportEstimate();
    Q_INVOKABLE void exportImageDialog();
    Q_INVOKABLE bool exportImageTo(const QString &outputPath);

    Q_INVOKABLE void preparePrintSheet();
    Q_INVOKABLE void nextPreviewPage();
    Q_INVOKABLE void previousPreviewPage();
    Q_INVOKABLE void refreshPrinters();
    Q_INVOKABLE void printCurrent();
    Q_INVOKABLE void savePdfDialog();
    Q_INVOKABLE bool savePdfTo(const QString &outputPath);
    Q_INVOKABLE void clearMessages();

    static bool isSupportedImageFile(const QString &path);
    static QStringList buildLpArguments(const QString &printer,
                                        const PrintSettings &settings,
                                        const QString &pdfPath,
                                        const QString &jobTitle);
    static void parseLpstatOutput(const QString &output, QStringList &printers, QString &defaultPrinter);
    static void parseLpoptionsOutput(const QString &output, QHash<QString, CupsOption> &options);
    static QString friendlyCupsValue(const QString &value);

signals:
    void imageChanged();
    void colorChanged();
    void exportSettingsChanged();
    void filesChanged();
    void selectionChanged();
    void printersChanged();
    void printerOptionsChanged();
    void printSettingsChanged();
    void presetsChanged();
    void papersChanged();
    void printStateChanged();
    void messageChanged();

private:
    bool loadIndex(int index);
    void applyEditState(const EditState &next, bool recordUndo);
    void rebuildPreColorImage();
    void rebuildDisplayImageFromPreColor();
    void refreshPrinterOptions();
    void clearPrinterOptions();
    void persistLastUsed(bool markPresetModified = true);
    void applyBuiltInCupsHints(const QString &presetId);
    void clampPreviewPage();
    QRect selectionPixelRect() const;
    QVector<int> activeBatchIndices() const;
    void setStatus(const QString &msg);
    void setError(const QString &msg);

    FilePicker *m_filePicker = nullptr;
    QString m_lpstatPath;
    QString m_lpPath;
    QString m_lpoptionsPath;
    PresetStore m_presetStore;
    QString m_preferredPrinter;
    QString m_activePresetId = "last-used";
    bool m_activePresetModified = false;
    bool m_hasSavedExportFormat = false;

    QStringList m_files;
    QSet<int> m_selectedIndices;
    int m_currentIndex = -1;
    QImage m_sourceImage;
    QImage m_preColorImage;
    QImage m_displayImage;
    QSize m_preResizeSize;
    QVariantMap m_metadata;
    mutable QHash<int, QImage> m_thumbCache;
    mutable QHash<QString, QImage> m_batchImageCache;
    int m_imageVersion = 0;
    int m_fileListVersion = 0;
    int m_editVersion = 0;
    int m_exportedEditVersion = 0;

    EditState m_edit;
    EditState m_colorSnapshot;
    bool m_colorEditing = false;
    QVector<EditState> m_undoStack;
    QVector<EditState> m_redoStack;

    QString m_exportFormat = "jpeg";
    int m_exportQuality = 90;
    qint64 m_estimatedExportBytes = 0;

    QRectF m_selectionNorm;
    bool m_printSelectionOnly = false;

    QStringList m_printers;
    QString m_defaultPrinter;
    QString m_selectedPrinter;
    QString m_printersError;
    CupsOption m_mediaTypeOpt;
    CupsOption m_qualityOpt;
    CupsOption m_duplexOpt;

    PrintSettings m_printSettings;
    QString m_multiMode = "repeat"; // repeat | selected
    int m_previewPageIndex = 0;

    QProcess m_lpstatProcess;
    QProcess m_lpoptionsProcess;
    QProcess m_lpProcess;
    QString m_activeTempPdf;
    bool m_printBusy = false;

    QString m_statusMessage;
    QString m_errorMessage;
};

class PeekImageProvider : public QQuickImageProvider {
public:
    explicit PeekImageProvider(Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        QImage img;
        if (m_backend) {
            const QString path = id.section('?', 0, 0);
            if (path.startsWith("thumb/")) {
                bool ok = false;
                const int idx = path.mid(6).toInt(&ok);
                if (ok)
                    img = m_backend->thumbnailForIndex(idx);
            } else {
                img = m_backend->displayImage();
            }
        }
        if (size)
            *size = img.size();
        return img;
    }

private:
    Backend *m_backend = nullptr;
};
