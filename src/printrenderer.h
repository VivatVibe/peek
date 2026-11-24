#pragma once

#include <QImage>
#include <QPageSize>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QSizeF>
#include <QString>

class Backend;

struct PaperSpec {
    QString id;
    QString cupsMedia;
    QPageSize::PageSizeId qtPageSize;
    double widthMm;
    double heightMm;
};

struct PrintSettings {
    QString paperSize = "a4";
    QString orientation = "auto";  // auto | portrait | landscape
    QString scaleMode = "fit";     // fit | fill | scale
    int scalePercent = 100;        // 10..200 (used when scaleMode == "scale")
    QString marginMode = "standard"; // standard | narrow | borderless
    bool grayscale = false;
    int copies = 1;
};

// Single source of truth for page geometry, image rendering, and PDF export so
// the on-screen print preview matches the printed sheet pixel for pixel.
namespace PrintRenderer {

PaperSpec paperSpec(const QString &paperId);
QList<PaperSpec> allPapers();
double marginMm(const QString &marginMode);
bool resolveLandscape(const PrintSettings &settings, const QSize &imageSize);
QSizeF effectivePageSizeMm(const PrintSettings &settings, const QSize &imageSize);
QImage applyTransforms(const QImage &source, int rotationDegrees, bool flipH, bool flipV, bool grayscale);
QRectF computeImageTargetRect(const QRectF &printableRect, const QSize &imageSize, const PrintSettings &settings);
void paintPage(QPainter &painter,
               const QRectF &pageRect,
               const QImage &transformedImage,
               const PrintSettings &settings,
               bool drawMarginGuide = false);
bool exportPdf(const QString &outputPath,
               const QImage &transformedImage,
               const PrintSettings &settings,
               QString *errorMessage = nullptr);

} // namespace PrintRenderer

class PrintPreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    explicit PrintPreviewItem(QQuickItem *parent = nullptr);

    QObject *controller() const;
    void setController(QObject *controller);

    void paint(QPainter *painter) override;

signals:
    void controllerChanged();

private:
    QPointer<Backend> m_backend;
};
