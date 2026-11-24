#include "markup.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace Markup {

double strokeFactorForPreset(const QString &sizePreset) {
    if (sizePreset == "s") return 0.0026;
    if (sizePreset == "l") return 0.0085;
    return 0.0048;
}

static QPointF mapPoint(const QRectF &bounds, const QPointF &norm) {
    return QPointF(bounds.x() + norm.x() * bounds.width(),
                   bounds.y() + norm.y() * bounds.height());
}

void paintAnnotation(QPainter &painter,
                     const QRectF &bounds,
                     const Annotation &ann,
                     const QImage *underlyingImage) {
    if (bounds.isEmpty() || ann.tool.isEmpty())
        return;

    const double baseDim = std::max(64.0, std::min(bounds.width(), bounds.height()));
    const double strokeW = std::max(2.0, baseDim * ann.strokeFactor);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QPointF a = mapPoint(bounds, ann.p1);
    const QPointF b = mapPoint(bounds, ann.p2);
    const QRectF rect = QRectF(a, b).normalized();

    if (ann.tool == "rect") {
        QPen pen(ann.color, strokeW, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
    } else if (ann.tool == "oval") {
        QPen pen(ann.color, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect);
    } else if (ann.tool == "line") {
        QPen pen(ann.color, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawLine(a, b);
    } else if (ann.tool == "arrow") {
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        if (len >= 2.0) {
            const double angle = std::atan2(dy, dx);
            const double headLen = std::min(len * 0.45, std::max(strokeW * 4.2, 14.0));
            const double wingAngle = 0.46; // ~26 degrees

            const QPointF tip = b;
            const QPointF left(tip.x() - headLen * std::cos(angle - wingAngle),
                               tip.y() - headLen * std::sin(angle - wingAngle));
            const QPointF right(tip.x() - headLen * std::cos(angle + wingAngle),
                                tip.y() - headLen * std::sin(angle + wingAngle));
            const QPointF shaftEnd(tip.x() - headLen * 0.72 * std::cos(angle),
                                   tip.y() - headLen * 0.72 * std::sin(angle));

            QPen pen(ann.color, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawLine(a, shaftEnd);

            painter.setPen(Qt::NoPen);
            painter.setBrush(ann.color);
            QPolygonF head;
            head << tip << left << right;
            painter.drawPolygon(head);
        }
    } else if (ann.tool == "pen") {
        if (ann.points.size() >= 2) {
            QPainterPath path;
            path.moveTo(mapPoint(bounds, ann.points.first()));
            for (int i = 1; i < ann.points.size(); ++i)
                path.lineTo(mapPoint(bounds, ann.points.at(i)));
            QPen pen(ann.color, strokeW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        } else if (ann.points.size() == 1) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ann.color);
            painter.drawEllipse(mapPoint(bounds, ann.points.first()), strokeW * 0.5, strokeW * 0.5);
        }
    } else if (ann.tool == "text") {
        if (!ann.text.trimmed().isEmpty()) {
            const double fontPx = std::max(14.0, baseDim * (ann.strokeFactor * 7.8));
            QFont font;
            font.setPixelSize(static_cast<int>(std::round(fontPx)));
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);

            const QFontMetricsF fm(font);
            const QRectF textBox = fm.boundingRect(QRectF(0, 0, bounds.width() * 0.85, bounds.height()),
                                                   Qt::TextWordWrap, ann.text);
            const double padX = fontPx * 0.42;
            const double padY = fontPx * 0.24;
            QRectF pill(a.x() - padX, a.y() - textBox.height() * 0.5 - padY,
                        textBox.width() + padX * 2.0, textBox.height() + padY * 2.0);

            // Keep the callout inside the image bounds.
            if (pill.left() < bounds.left() + 4.0)
                pill.moveLeft(bounds.left() + 4.0);
            if (pill.right() > bounds.right() - 4.0)
                pill.moveRight(bounds.right() - 4.0);
            if (pill.top() < bounds.top() + 4.0)
                pill.moveTop(bounds.top() + 4.0);
            if (pill.bottom() > bounds.bottom() - 4.0)
                pill.moveBottom(bounds.bottom() - 4.0);

            // Subtle contrast backdrop so annotations stay legible on any photo.
            const double luma = 0.2126 * ann.color.redF() + 0.7152 * ann.color.greenF() + 0.0722 * ann.color.blueF();
            const QColor pillBg = luma > 0.35 ? QColor(14, 14, 18, 195) : QColor(248, 248, 252, 215);
            painter.setPen(Qt::NoPen);
            painter.setBrush(pillBg);
            painter.drawRoundedRect(pill, std::min(6.0, fontPx * 0.22), std::min(6.0, fontPx * 0.22));

            painter.setPen(ann.color);
            painter.drawText(pill.adjusted(padX, padY, -padX, -padY),
                             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                             ann.text);
        }
    } else if (ann.tool == "redact") {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#0a0a0c"));
        painter.drawRect(rect);
    } else if (ann.tool == "mosaic") {
        if (rect.width() >= 2.0 && rect.height() >= 2.0) {
            if (underlyingImage && !underlyingImage->isNull()) {
                const QRect pxRect(
                    static_cast<int>(std::floor(ann.p1.x() < ann.p2.x() ? ann.p1.x() * underlyingImage->width() : ann.p2.x() * underlyingImage->width())),
                    static_cast<int>(std::floor(ann.p1.y() < ann.p2.y() ? ann.p1.y() * underlyingImage->height() : ann.p2.y() * underlyingImage->height())),
                    std::max(1, static_cast<int>(std::round(std::abs(ann.p2.x() - ann.p1.x()) * underlyingImage->width()))),
                    std::max(1, static_cast<int>(std::round(std::abs(ann.p2.y() - ann.p1.y()) * underlyingImage->height()))));
                const QRect clamped = pxRect.intersected(underlyingImage->rect());
                if (clamped.isValid() && clamped.width() >= 2 && clamped.height() >= 2) {
                    const int blockSize = std::max(8, static_cast<int>(std::round(std::min(underlyingImage->width(), underlyingImage->height()) * 0.018)));
                    const QSize smallSize(std::max(2, clamped.width() / blockSize),
                                          std::max(2, clamped.height() / blockSize));
                    const QImage region = underlyingImage->copy(clamped);
                    const QImage down = region.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    const QImage pixelated = down.scaled(clamped.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                    painter.drawImage(rect, pixelated);
                }
            } else {
                // Live draft preview while dragging over the canvas.
                painter.setPen(QPen(QColor("#ffffff"), 1.5, Qt::DashLine));
                painter.setBrush(QColor(20, 20, 26, 175));
                painter.drawRect(rect);
            }
        }
    }

    painter.restore();
}

QImage compositeAnnotations(const QImage &baseImage, const QVector<Annotation> &annotations) {
    if (baseImage.isNull() || annotations.isEmpty())
        return baseImage;

    QImage canvas = baseImage.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&canvas);
    const QRectF bounds(0.0, 0.0, canvas.width(), canvas.height());
    for (const Annotation &ann : annotations)
        paintAnnotation(painter, bounds, ann, &canvas);
    painter.end();
    return canvas;
}

} // namespace Markup

MarkupDraftItem::MarkupDraftItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::Image);
    m_annotation.tool = "arrow";
    m_annotation.strokeFactor = Markup::strokeFactorForPreset(m_strokeSize);
}

void MarkupDraftItem::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    emit draftChanged();
    update();
}

void MarkupDraftItem::setTool(const QString &tool) {
    if (m_annotation.tool == tool)
        return;
    m_annotation.tool = tool;
    emit draftChanged();
    update();
}

void MarkupDraftItem::setColor(const QColor &color) {
    if (m_annotation.color == color)
        return;
    m_annotation.color = color;
    emit draftChanged();
    update();
}

void MarkupDraftItem::setStrokeSize(const QString &size) {
    if (m_strokeSize == size)
        return;
    m_strokeSize = size;
    m_annotation.strokeFactor = Markup::strokeFactorForPreset(size);
    emit draftChanged();
    update();
}

void MarkupDraftItem::beginStroke(double nx, double ny) {
    const QPointF pt(std::clamp(nx, 0.0, 1.0), std::clamp(ny, 0.0, 1.0));
    m_annotation.p1 = pt;
    m_annotation.p2 = pt;
    m_annotation.points.clear();
    if (m_annotation.tool == "pen")
        m_annotation.points.append(pt);
    m_active = true;
    emit draftChanged();
    update();
}

void MarkupDraftItem::updateStroke(double nx, double ny) {
    if (!m_active)
        return;
    const QPointF pt(std::clamp(nx, 0.0, 1.0), std::clamp(ny, 0.0, 1.0));
    m_annotation.p2 = pt;
    if (m_annotation.tool == "pen") {
        if (m_annotation.points.isEmpty() ||
            std::hypot(pt.x() - m_annotation.points.last().x(), pt.y() - m_annotation.points.last().y()) > 0.0025) {
            m_annotation.points.append(pt);
        }
    }
    update();
}

void MarkupDraftItem::clear() {
    m_active = false;
    m_annotation.points.clear();
    emit draftChanged();
    update();
}

void MarkupDraftItem::paint(QPainter *painter) {
    if (!m_active)
        return;
    Markup::paintAnnotation(*painter, QRectF(0.0, 0.0, width(), height()), m_annotation, nullptr);
}
