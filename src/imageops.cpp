#include "imageops.h"
#include "printrenderer.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QObject>
#include <QPainter>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>

namespace ImageOps {

QImage applyColorAdjustments(const QImage &source, const ColorAdjustments &adj) {
    if (source.isNull() || adj.isIdentity())
        return source;

    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    const double evMult = std::pow(2.0, std::clamp(adj.exposure, -1.0, 1.0));
    const double bShift = std::clamp(adj.brightness, -1.0, 1.0) * 0.35;
    const double cVal = std::clamp(adj.contrast, -1.0, 1.0);
    const double cMult = cVal >= 0.0 ? (1.0 + cVal * 1.5) : (1.0 + cVal * 0.8);
    const double sMult = 1.0 + std::clamp(adj.saturation, -1.0, 1.0);

    const int h = img.height();
    const int w = img.width();
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            double r = qRed(px) / 255.0;
            double g = qGreen(px) / 255.0;
            double b = qBlue(px) / 255.0;

            // Exposure & brightness.
            r = r * evMult + bShift;
            g = g * evMult + bShift;
            b = b * evMult + bShift;

            // Contrast around mid-gray.
            r = (r - 0.5) * cMult + 0.5;
            g = (g - 0.5) * cMult + 0.5;
            b = (b - 0.5) * cMult + 0.5;

            // Saturation using Rec.709 luminance.
            if (std::abs(sMult - 1.0) > 1e-4) {
                const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                r = luma + (r - luma) * sMult;
                g = luma + (g - luma) * sMult;
                b = luma + (b - luma) * sMult;
            }

            const int ri = static_cast<int>(std::round(std::clamp(r, 0.0, 1.0) * 255.0));
            const int gi = static_cast<int>(std::round(std::clamp(g, 0.0, 1.0) * 255.0));
            const int bi = static_cast<int>(std::round(std::clamp(b, 0.0, 1.0) * 255.0));
            line[x] = qRgba(ri, gi, bi, a);
        }
    }
    return img;
}

QImage applyEdits(const QImage &source, const EditState &state, bool grayscale) {
    if (source.isNull())
        return {};

    QImage img = PrintRenderer::applyTransforms(source, state.rotation, state.flipH, state.flipV, false);
    if (!state.cropRect.isEmpty()) {
        const QRect clamped = state.cropRect.intersected(img.rect());
        if (clamped.isValid() && clamped.width() >= 2 && clamped.height() >= 2)
            img = img.copy(clamped);
    }
    if (!state.resizeTarget.isEmpty() && state.resizeTarget.width() >= 1 && state.resizeTarget.height() >= 1) {
        if (state.resizeTarget != img.size())
            img = img.scaled(state.resizeTarget, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (!state.color.isIdentity())
        img = applyColorAdjustments(img, state.color);
    if (!state.annotations.isEmpty())
        img = Markup::compositeAnnotations(img, state.annotations);
    if (grayscale)
        img = img.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
    return img;
}

ColorAdjustments computeAutoLevels(const QImage &source) {
    ColorAdjustments adj;
    if (source.isNull())
        return adj;

    const QImage sample = (source.width() > 240 || source.height() > 240)
                              ? source.scaled(240, 240, Qt::KeepAspectRatio, Qt::FastTransformation)
                                    .convertToFormat(QImage::Format_ARGB32)
                              : source.convertToFormat(QImage::Format_ARGB32);

    std::array<int, 256> hist{};
    int total = 0;
    for (int y = 0; y < sample.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            if (qAlpha(line[x]) < 16)
                continue;
            const int luma = std::clamp(
                static_cast<int>(std::round(0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) + 0.0722 * qBlue(line[x]))),
                0, 255);
            ++hist[static_cast<size_t>(luma)];
            ++total;
        }
    }
    if (total < 16)
        return adj;

    const int lowCut = std::max(1, total / 100);
    const int highCut = std::max(1, total - lowCut);
    int accum = 0;
    int lo = 0;
    int hi = 255;
    for (int i = 0; i < 256; ++i) {
        accum += hist[static_cast<size_t>(i)];
        if (accum >= lowCut) {
            lo = i;
            break;
        }
    }
    accum = 0;
    for (int i = 0; i < 256; ++i) {
        accum += hist[static_cast<size_t>(i)];
        if (accum >= highCut) {
            hi = i;
            break;
        }
    }

    const double loF = lo / 255.0;
    const double hiF = hi / 255.0;
    const double span = hiF - loF;
    if (span < 0.05)
        return adj;

    // Stretch [loF, hiF] toward [0.04, 0.96] via contrast & brightness.
    const double targetMult = std::clamp(0.92 / span, 0.7, 2.2);
    const double mid = (loF + hiF) * 0.5;
    adj.contrast = targetMult >= 1.0
                       ? std::clamp((targetMult - 1.0) / 1.5, 0.0, 0.85)
                       : std::clamp((targetMult - 1.0) / 0.8, -0.6, 0.0);
    adj.brightness = std::clamp((0.5 - mid) / 0.35, -0.75, 0.75);
    return adj;
}

QString normalizedFormat(const QString &format) {
    const QString f = format.trimmed().toLower();
    if (f == "jpg" || f == "jpeg") return "jpeg";
    if (f == "webp") return "webp";
    if (f == "tif" || f == "tiff") return "tiff";
    if (f == "bmp") return "bmp";
    if (f == "pdf") return "pdf";
    return "png";
}

QString extensionForFormat(const QString &format) {
    const QString norm = normalizedFormat(format);
    if (norm == "jpeg") return "jpg";
    return norm;
}

QString mimeTypeForFormat(const QString &format) {
    const QString norm = normalizedFormat(format);
    if (norm == "jpeg") return "image/jpeg";
    if (norm == "webp") return "image/webp";
    if (norm == "tiff") return "image/tiff";
    if (norm == "bmp") return "image/bmp";
    if (norm == "pdf") return "application/pdf";
    return "image/png";
}

bool formatSupportsQuality(const QString &format) {
    const QString norm = normalizedFormat(format);
    return norm == "jpeg" || norm == "webp";
}

qint64 estimateEncodedSize(const QImage &image, const QString &format, int quality) {
    if (image.isNull())
        return 0;
    const QString norm = normalizedFormat(format);
    if (norm == "pdf")
        return static_cast<qint64>(image.width()) * image.height() / 3;

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);

    QImage toEncode = image;
    if (norm == "jpeg" && toEncode.hasAlphaChannel()) {
        // Composite transparency onto white for JPEG output.
        QImage opaque(toEncode.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, toEncode);
        toEncode = opaque;
    }

    QImageWriter writer(&buffer, norm.toLatin1());
    if (formatSupportsQuality(norm))
        writer.setQuality(std::clamp(quality, 10, 100));
    if (!writer.write(toEncode))
        return 0;
    return bytes.size();
}

bool exportImageAtomic(const QImage &image,
                       const QString &outputPath,
                       const QString &format,
                       int quality,
                       QString *errorMessage) {
    if (image.isNull()) {
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

    const QString norm = normalizedFormat(format);
    QImage toSave = image;
    if (norm == "jpeg" && toSave.hasAlphaChannel()) {
        QImage opaque(toSave.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, toSave);
        toSave = opaque;
    }

    // Write beside target, then rename atomically so originals or prior exports
    // are never corrupted by a partial write.
    const QString tempPath = parentDir.filePath(
        "." + targetInfo.fileName() + ".omarchy-preview-" + QUuid::createUuid().toString(QUuid::Id128) + ".tmp");

    {
        QImageWriter writer(tempPath, norm.toLatin1());
        if (formatSupportsQuality(norm))
            writer.setQuality(std::clamp(quality, 10, 100));
        if (!writer.write(toSave)) {
            QFile::remove(tempPath);
            if (errorMessage)
                *errorMessage = QObject::tr("Could not export image: %1").arg(writer.errorString());
            return false;
        }
    }

    QFile::remove(outputPath);
    if (!QFile::rename(tempPath, outputPath)) {
        QFile::remove(tempPath);
        if (errorMessage)
            *errorMessage = QObject::tr("Could not save file to %1").arg(outputPath);
        return false;
    }

    return true;
}

} // namespace ImageOps
