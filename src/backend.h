#pragma once

#include "filepicker.h"
#include "printrenderer.h"

#include <QImage>
#include <QObject>
#include <QProcess>
#include <QQuickImageProvider>
#include <QStringList>
#include <QUrl>
#include <QVector>

struct TransformState {
    int rotation = 0; // 0, 90, 180, 270
    bool flipH = false;
    bool flipV = false;

    bool operator==(const TransformState &other) const {
        return rotation == other.rotation && flipH == other.flipH && flipV == other.flipV;
    }
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY imageChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY imageChanged)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(int imageVersion READ imageVersion NOTIFY imageChanged)
    Q_PROPERTY(int imageIndex READ imageIndex NOTIFY imageChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY imageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY imageChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY imageChanged)
    Q_PROPERTY(bool isTransformed READ isTransformed NOTIFY imageChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY imageChanged)

    Q_PROPERTY(QStringList printers READ printers NOTIFY printersChanged)
    Q_PROPERTY(QString printer READ printer WRITE setPrinter NOTIFY printSettingsChanged)
    Q_PROPERTY(QString defaultPrinter READ defaultPrinter NOTIFY printersChanged)
    Q_PROPERTY(QString printersError READ printersError NOTIFY printersChanged)

    Q_PROPERTY(int copies READ copies WRITE setCopies NOTIFY printSettingsChanged)
    Q_PROPERTY(QString paperSize READ paperSize WRITE setPaperSize NOTIFY printSettingsChanged)
    Q_PROPERTY(QString orientation READ orientation WRITE setOrientation NOTIFY printSettingsChanged)
    Q_PROPERTY(QString scaleMode READ scaleMode WRITE setScaleMode NOTIFY printSettingsChanged)
    Q_PROPERTY(int scalePercent READ scalePercent WRITE setScalePercent NOTIFY printSettingsChanged)
    Q_PROPERTY(QString marginMode READ marginMode WRITE setMarginMode NOTIFY printSettingsChanged)
    Q_PROPERTY(bool grayscale READ grayscale WRITE setGrayscale NOTIFY printSettingsChanged)
    Q_PROPERTY(bool effectiveLandscape READ effectiveLandscape NOTIFY printSettingsChanged)
    Q_PROPERTY(double paperWidthMm READ paperWidthMm NOTIFY printSettingsChanged)
    Q_PROPERTY(double paperHeightMm READ paperHeightMm NOTIFY printSettingsChanged)

    Q_PROPERTY(bool printBusy READ printBusy NOTIFY printStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY messageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messageChanged)

public:
    explicit Backend(FilePicker *filePicker = nullptr,
                     const QString &lpstatPath = {},
                     const QString &lpPath = {},
                     QObject *parent = nullptr);
    ~Backend() override;

    bool hasImage() const { return !m_displayImage.isNull(); }
    QString filePath() const;
    QString fileName() const;
    int imageWidth() const { return m_displayImage.width(); }
    int imageHeight() const { return m_displayImage.height(); }
    int imageVersion() const { return m_imageVersion; }
    int imageIndex() const { return m_currentIndex; }
    int imageCount() const { return m_files.size(); }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    bool isTransformed() const { return !(m_transform == TransformState{}); }
    int rotation() const { return m_transform.rotation; }

    QImage displayImage() const { return m_displayImage; }
    QImage printableImage() const;
    PrintSettings printSettings() const { return m_printSettings; }

    QStringList printers() const { return m_printers; }
    QString printer() const { return m_selectedPrinter; }
    void setPrinter(const QString &name);
    QString defaultPrinter() const { return m_defaultPrinter; }
    QString printersError() const { return m_printersError; }

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
    bool effectiveLandscape() const;
    double paperWidthMm() const;
    double paperHeightMm() const;

    bool printBusy() const { return m_printBusy; }
    QString statusMessage() const { return m_statusMessage; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE bool openFiles(const QStringList &paths);
    Q_INVOKABLE bool openUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void nextImage();
    Q_INVOKABLE void previousImage();

    Q_INVOKABLE void rotateLeft();
    Q_INVOKABLE void rotateRight();
    Q_INVOKABLE void toggleFlipHorizontal();
    Q_INVOKABLE void toggleFlipVertical();
    Q_INVOKABLE void resetTransforms();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

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

signals:
    void imageChanged();
    void printersChanged();
    void printSettingsChanged();
    void printStateChanged();
    void messageChanged();

private:
    bool loadIndex(int index);
    void applyTransformState(const TransformState &next, bool recordUndo);
    void rebuildDisplayImage();
    void setStatus(const QString &msg);
    void setError(const QString &msg);

    FilePicker *m_filePicker = nullptr;
    QString m_lpstatPath;
    QString m_lpPath;

    QStringList m_files;
    int m_currentIndex = -1;
    QImage m_sourceImage;
    QImage m_displayImage;
    int m_imageVersion = 0;

    TransformState m_transform;
    QVector<TransformState> m_undoStack;
    QVector<TransformState> m_redoStack;

    QStringList m_printers;
    QString m_defaultPrinter;
    QString m_selectedPrinter;
    QString m_printersError;
    PrintSettings m_printSettings;

    QProcess m_lpstatProcess;
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

    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        QImage img = m_backend ? m_backend->displayImage() : QImage();
        if (size)
            *size = img.size();
        return img;
    }

private:
    Backend *m_backend = nullptr;
};
