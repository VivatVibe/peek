#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <cmath>

struct ColorAdjustments {
    double exposure = 0.0;   // -1.0 .. +1.0 (EV stops)
    double contrast = 0.0;   // -1.0 .. +1.0
    double saturation = 0.0; // -1.0 .. +1.0
    double brightness = 0.0; // -1.0 .. +1.0

    bool isIdentity() const {
        return std::abs(exposure) < 1e-4 &&
               std::abs(contrast) < 1e-4 &&
               std::abs(saturation) < 1e-4 &&
               std::abs(brightness) < 1e-4;
    }

    bool operator==(const ColorAdjustments &other) const {
        return std::abs(exposure - other.exposure) < 1e-4 &&
               std::abs(contrast - other.contrast) < 1e-4 &&
               std::abs(saturation - other.saturation) < 1e-4 &&
               std::abs(brightness - other.brightness) < 1e-4;
    }
};

struct EditState {
    int rotation = 0; // 0, 90, 180, 270
    bool flipH = false;
    bool flipV = false;
    QRect cropRect;    // pixel rect in post-rotate/flip coordinates; empty = uncropped
    QSize resizeTarget; // target pixel size after crop; empty = native size
    ColorAdjustments color;

    bool isIdentity() const {
        return rotation == 0 && !flipH && !flipV &&
               cropRect.isEmpty() && resizeTarget.isEmpty() &&
               color.isIdentity();
    }

    bool operator==(const EditState &other) const {
        return rotation == other.rotation &&
               flipH == other.flipH &&
               flipV == other.flipV &&
               cropRect == other.cropRect &&
               resizeTarget == other.resizeTarget &&
               color == other.color;
    }
};

// Pure C++ non-destructive image editing, auto-levels, size estimation, and
// atomic image export used by both the GUI and headless CLI.
namespace ImageOps {

QImage applyColorAdjustments(const QImage &source, const ColorAdjustments &adj);
QImage applyEdits(const QImage &source, const EditState &state, bool grayscale = false);
ColorAdjustments computeAutoLevels(const QImage &source);

QString normalizedFormat(const QString &format);
QString extensionForFormat(const QString &format);
QString mimeTypeForFormat(const QString &format);
bool formatSupportsQuality(const QString &format);

qint64 estimateEncodedSize(const QImage &image, const QString &format, int quality);
bool exportImageAtomic(const QImage &image,
                       const QString &outputPath,
                       const QString &format,
                       int quality,
                       QString *errorMessage = nullptr);

} // namespace ImageOps
