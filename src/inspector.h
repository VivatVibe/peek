#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

// Zero-dependency file metadata and EXIF reader (parses JPEG APP1 Exif IFD0 +
// ExifIFD directly) powering Peek's macOS Preview-style Inspector HUD.
namespace Inspector {

QVariantMap readMetadata(const QString &filePath, const QImage &displayImage);

} // namespace Inspector
