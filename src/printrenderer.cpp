#include "printrenderer.h"
#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QTransform>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace PrintRenderer {

QList<PaperSpec> allPapers() {
    return {
        {"a4", "A4", QPageSize::A4, 210.0, 297.0},
        {"letter", "Letter", QPageSize::Letter, 215.9, 279.4},
        {"4x6", "4x6in", QPageSize::Custom, 101.6, 152.4},
        {"5x7", "5x7in", QPageSize::Custom, 127.0, 177.8},
        {"a5", "A5", QPageSize::A5, 148.0, 210.0},
        {"a3", "A3", QPageSize::A3, 297.0, 420.0},
        {"legal", "Legal", QPageSize::Legal, 215.9, 355.6},
    };
}

PaperSpec paperSpec(const QString &paperId) {
    const QString key = paperId.trimmed().toLower();
    for (const auto &spec : allPapers()) {
        if (spec.id == key)
            return spec;
    }
    return allPapers().first();
}

double marginMm(const QString &marginMode) {
    if (marginMode == "borderless")
        return 0.0;
    if (marginMode == "narrow")
        return 4.0;
    return 10.0;
}

bool resolveLandscape(const PrintSettings &settings, const QSize &imageSize) {
    if (settings.orientation == "landscape")
        return true;
    if (settings.orientation == "portrait")
        return false;
    return imageSize.isValid() && imageSize.width() > imageSize.height();
}

QSizeF effectivePageSizeMm(const PrintSettings &settings, const QSize &imageSize) {
    const PaperSpec spec = paperSpec(settings.paperSize);
    if (resolveLandscape(settings, imageSize))
        return QSizeF(spec.heightMm, spec.widthMm);
    return QSizeF(spec.widthMm, spec.heightMm);
}

QImage applyTransforms(const QImage &source, int rotationDegrees, bool flipH, bool flipV, bool grayscale) {
    if (source.isNull())
        return {};

    QImage img = source;
    const int normalizedRot = ((rotationDegrees % 360) + 360) % 360;
    if (normalizedRot != 0) {
        QTransform transform;
        transform.rotate(normalizedRot);
        img = img.transformed(transform, Qt::SmoothTransformation);
    }
    if (flipH || flipV)
        img = img.flipped((flipH ? Qt::Horizontal : Qt::Orientations{}) |
                          (flipV ? Qt::Vertical : Qt::Orientations{}));
    if (grayscale)
        img = img.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
    return img;
}

QRectF computeImageTargetRect(const QRectF &printableRect, const QSize &imageSize, const PrintSettings &settings) {
    if (printableRect.isEmpty() || !imageSize.isValid() || imageSize.isEmpty())
        return {};

    const double imgW = imageSize.width();
    const double imgH = imageSize.height();
    const double areaW = printableRect.width();
    const double areaH = printableRect.height();

    const double fitScale = std::min(areaW / imgW, areaH / imgH);
    double chosenScale = fitScale;

    if (settings.scaleMode == "fill") {
        chosenScale = std::max(areaW / imgW, areaH / imgH);
    } else if (settings.scaleMode == "scale") {
        const double factor = std::clamp(settings.scalePercent, 10, 200) / 100.0;
        chosenScale = fitScale * factor;
    }

    const double targetW = imgW * chosenScale;
    const double targetH = imgH * chosenScale;
    const double x = printableRect.x() + (areaW - targetW) / 2.0;
    const double y = printableRect.y() + (areaH - targetH) / 2.0;
    return QRectF(x, y, targetW, targetH);
}

void paintPage(QPainter &painter,
               const QRectF &pageRect,
               const QImage &transformedImage,
               const PrintSettings &settings,
               bool drawMarginGuide) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Paper sheet background.
    painter.fillRect(pageRect, Qt::white);

    if (transformedImage.isNull() || pageRect.isEmpty()) {
        painter.restore();
        return;
    }

    const QSizeF pageMm = effectivePageSizeMm(settings, transformedImage.size());
    const double mMm = marginMm(settings.marginMode);
    const double marginX = pageMm.width() > 0.0 ? pageRect.width() * (mMm / pageMm.width()) : 0.0;
    const double marginY = pageMm.height() > 0.0 ? pageRect.height() * (mMm / pageMm.height()) : 0.0;

    const QRectF printableRect = pageRect.adjusted(marginX, marginY, -marginX, -marginY);

    painter.save();
    painter.setClipRect(printableRect);
    const QRectF target = computeImageTargetRect(printableRect, transformedImage.size(), settings);
    if (!target.isEmpty())
        painter.drawImage(target, transformedImage);
    painter.restore();

    if (drawMarginGuide && mMm > 0.0) {
        QPen pen(QColor(0, 0, 0, 38));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(printableRect);
    }

    painter.restore();
}

bool exportPdf(const QString &outputPath,
               const QImage &transformedImage,
               const PrintSettings &settings,
               QString *errorMessage) {
    if (transformedImage.isNull()) {
        if (errorMessage)
            *errorMessage = QObject::tr("No image is loaded to export.");
        return false;
    }

    const QFileInfo targetInfo(outputPath);
    QDir parentDir = targetInfo.dir();
    if (!parentDir.exists() && !parentDir.mkpath(".")) {
        if (errorMessage)
            *errorMessage = QObject::tr("Could not create destination folder: %1").arg(parentDir.path());
        return false;
    }

    // Write beside the target and rename atomically so an interrupted export
    // never leaves a half-written PDF behind.
    const QString tempPath = parentDir.filePath(
        "." + targetInfo.fileName() + ".peek-" + QUuid::createUuid().toString(QUuid::Id128) + ".tmp");

    {
        QPdfWriter writer(tempPath);
        writer.setCreator("Peek");
        writer.setTitle(targetInfo.completeBaseName());
        writer.setResolution(300);

        const PaperSpec spec = paperSpec(settings.paperSize);
        const bool landscape = resolveLandscape(settings, transformedImage.size());
        const QPageSize pageSize = spec.qtPageSize == QPageSize::Custom
                                       ? QPageSize(QSizeF(spec.widthMm, spec.heightMm), QPageSize::Millimeter, spec.id)
                                       : QPageSize(spec.qtPageSize);
        const QPageLayout layout(pageSize,
                                 landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                 QMarginsF(0, 0, 0, 0),
                                 QPageLayout::Millimeter);
        writer.setPageLayout(layout);

        QPainter painter;
        if (!painter.begin(&writer)) {
            QFile::remove(tempPath);
            if (errorMessage)
                *errorMessage = QObject::tr("Could not write PDF to %1").arg(outputPath);
            return false;
        }

        const QRectF pageRect(0.0, 0.0, writer.width(), writer.height());
        paintPage(painter, pageRect, transformedImage, settings, false);
        painter.end();
    }

    QFile::remove(outputPath);
    if (!QFile::rename(tempPath, outputPath)) {
        QFile::remove(tempPath);
        if (errorMessage)
            *errorMessage = QObject::tr("Could not save PDF to %1").arg(outputPath);
        return false;
    }

    return true;
}

} // namespace PrintRenderer

PrintPreviewItem::PrintPreviewItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::Image);
}

QObject *PrintPreviewItem::controller() const {
    return m_backend;
}

void PrintPreviewItem::setController(QObject *obj) {
    auto *b = qobject_cast<Backend *>(obj);
    if (m_backend == b)
        return;

    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);

    m_backend = b;
    if (m_backend) {
        connect(m_backend, &Backend::imageChanged, this, [this] { update(); });
        connect(m_backend, &Backend::printSettingsChanged, this, [this] { update(); });
    }
    emit controllerChanged();
    update();
}

void PrintPreviewItem::paint(QPainter *painter) {
    if (!m_backend || !m_backend->hasImage())
        return;

    const QImage previewImage = m_backend->printableImage();
    const PrintSettings settings = m_backend->printSettings();
    const QSizeF pageMm = PrintRenderer::effectivePageSizeMm(settings, previewImage.size());

    const double pad = 18.0;
    const double availW = std::max(1.0, width() - pad * 2.0);
    const double availH = std::max(1.0, height() - pad * 2.0);
    const double ratio = pageMm.width() / std::max(1.0, pageMm.height());

    double sheetW = availW;
    double sheetH = sheetW / ratio;
    if (sheetH > availH) {
        sheetH = availH;
        sheetW = sheetH * ratio;
    }

    const QRectF pageRect((width() - sheetW) / 2.0, (height() - sheetH) / 2.0, sheetW, sheetH);

    // Soft shadow behind the paper so white edges stand out on the dark sheet.
    painter->save();
    painter->setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        painter->setBrush(QColor(0, 0, 0, 18 * (5 - i)));
        painter->drawRoundedRect(pageRect.adjusted(-i, i, i, i * 2.5), 2, 2);
    }
    painter->restore();

    PrintRenderer::paintPage(*painter, pageRect, previewImage, settings, true);

    // Hairline border around the sheet.
    painter->save();
    painter->setPen(QPen(QColor(255, 255, 255, 45), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(pageRect);
    painter->restore();
}
