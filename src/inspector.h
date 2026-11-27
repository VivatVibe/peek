#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

// File metadata, JPEG EXIF, and PDF document metadata reader powering Omarchy Preview's
// macOS Preview-style Inspector HUD and `omarchy-preview inspect` CLI command.
namespace Inspector {

QVariantMap readMetadata(const QString &filePath,
                         const QImage &displayImage,
                         int pdfPageIndex = -1,
                         int totalDocPages = 0);

} // namespace Inspector
