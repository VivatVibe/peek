#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QString>
#include <QVector>

struct Annotation {
    QString tool;              // rect | oval | arrow | line | pen | text | mosaic | redact
    QPointF p1;                // normalized 0..1
    QPointF p2;                // normalized 0..1
    QVector<QPointF> points;   // normalized 0..1 (pen)
    QString text;
    QColor color = QColor("#ff453a");
    double strokeFactor = 0.0045; // fraction of min(width, height)

    bool operator==(const Annotation &other) const {
        return tool == other.tool &&
               p1 == other.p1 &&
               p2 == other.p2 &&
               points == other.points &&
               text == other.text &&
               color == other.color &&
               std::abs(strokeFactor - other.strokeFactor) < 1e-6;
    }
};

// Single C++ renderer for all vector annotations, text callouts, redactions,
// and pixelation mosaics so on-screen display, exported images, and printed
// PDFs match identically.
namespace Markup {

double strokeFactorForPreset(const QString &sizePreset); // "s" | "m" | "l"
void paintAnnotation(QPainter &painter,
                     const QRectF &bounds,
                     const Annotation &ann,
                     const QImage *underlyingImage = nullptr);
QImage compositeAnnotations(const QImage &baseImage, const QVector<Annotation> &annotations);

} // namespace Markup

// Live interactive draft overlay painted with the exact same Markup::paintAnnotation
// renderer while the user drags a shape, arrow, line, pen stroke, or redaction.
class MarkupDraftItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY draftChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY draftChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY draftChanged)
    Q_PROPERTY(QString strokeSize READ strokeSize WRITE setStrokeSize NOTIFY draftChanged)

public:
    explicit MarkupDraftItem(QQuickItem *parent = nullptr);

    bool active() const { return m_active; }
    void setActive(bool active);
    QString tool() const { return m_annotation.tool; }
    void setTool(const QString &tool);
    QColor color() const { return m_annotation.color; }
    void setColor(const QColor &color);
    QString strokeSize() const { return m_strokeSize; }
    void setStrokeSize(const QString &size);

    Q_INVOKABLE void beginStroke(double nx, double ny);
    Q_INVOKABLE void updateStroke(double nx, double ny);
    Q_INVOKABLE void clear();

    Annotation currentAnnotation() const { return m_annotation; }
    void paint(QPainter *painter) override;

signals:
    void draftChanged();

private:
    bool m_active = false;
    QString m_strokeSize = "m";
    Annotation m_annotation;
};
