#pragma once

#include <QImage>
#include <QPageSize>
#include <QPair>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

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
    QString orientation = "auto";    // auto | portrait | landscape
    QString scaleMode = "fit";       // fit | fill | scale
    int scalePercent = 100;          // 10..200 (used when scaleMode == "scale")
    QString marginMode = "standard"; // standard | narrow | borderless
    bool grayscale = false;
    int copies = 1;
    int perPage = 1;                 // 1 | 2 | 4 | 6 | 9
    bool cutGuides = false;          // dashed trim guides between cells when perPage > 1

    // Optional CUPS PPD overrides discovered via lpoptions -l.
    QString mediaTypeKey;
    QString mediaTypeValue;
    QString qualityKey;
    QString qualityValue;
    QString duplexKey;
    QString duplexValue;
};

// Single source of truth for page geometry, N-up grid layout, image rendering,
// and multi-page PDF export so the on-screen print preview matches the printed
// sheet pixel for pixel.
namespace PrintRenderer {

PaperSpec paperSpec(const QString &paperId);
QList<PaperSpec> allPapers();
int normalizePerPage(int perPage);
double marginMm(const QString &marginMode);
double cellGapMm(const PrintSettings &settings);
bool resolveLandscape(const PrintSettings &settings, const QSize &imageSize);
QSizeF effectivePageSizeMm(const PrintSettings &settings, const QSize &imageSize);
QPair<int, int> gridForPerPage(int perPage, bool landscape);
QVector<QRectF> computeGridCellRects(const QRectF &printableRect, int cols, int rows, double gapX, double gapY);
QVector<QVector<QImage>> paginateImages(const QVector<QImage> &images, int perPage, bool repeatSingle);
QImage applyTransforms(const QImage &source, int rotationDegrees, bool flipH, bool flipV, bool grayscale);
QRectF computeImageTargetRect(const QRectF &printableRect, const QSize &imageSize, const PrintSettings &settings);

void paintPage(QPainter &painter,
               const QRectF &pageRect,
               const QVector<QImage> &slotImages,
               const PrintSettings &settings,
               bool drawMarginGuide = false);

void paintPage(QPainter &painter,
               const QRectF &pageRect,
               const QImage &transformedImage,
               const PrintSettings &settings,
               bool drawMarginGuide = false);

bool exportPdf(const QString &outputPath,
               const QVector<QVector<QImage>> &pages,
               const PrintSettings &settings,
               QString *errorMessage = nullptr);

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
