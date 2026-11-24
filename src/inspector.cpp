#include "inspector.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <cmath>
#include <cstdint>

namespace {

struct TiffReader {
    const unsigned char *data = nullptr;
    int size = 0;
    bool littleEndian = true;

    bool inRange(int offset, int len) const {
        return offset >= 0 && len >= 0 && offset <= size - len;
    }

    uint16_t u16(int off) const {
        if (!inRange(off, 2)) return 0;
        if (littleEndian)
            return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
        return static_cast<uint16_t>((data[off] << 8) | data[off + 1]);
    }

    uint32_t u32(int off) const {
        if (!inRange(off, 4)) return 0;
        if (littleEndian)
            return static_cast<uint32_t>(data[off]) |
                   (static_cast<uint32_t>(data[off + 1]) << 8) |
                   (static_cast<uint32_t>(data[off + 2]) << 16) |
                   (static_cast<uint32_t>(data[off + 3]) << 24);
        return (static_cast<uint32_t>(data[off]) << 24) |
               (static_cast<uint32_t>(data[off + 1]) << 16) |
               (static_cast<uint32_t>(data[off + 2]) << 8) |
               static_cast<uint32_t>(data[off + 3]);
    }

    QString readAscii(int entryOffset, uint32_t count) const {
        if (count == 0 || count > 256)
            return {};
        int valueOffset = count <= 4 ? entryOffset + 8 : static_cast<int>(u32(entryOffset + 8));
        if (!inRange(valueOffset, static_cast<int>(count)))
            return {};
        QByteArray raw(reinterpret_cast<const char *>(data + valueOffset), static_cast<int>(count));
        const int nul = raw.indexOf('\0');
        if (nul >= 0)
            raw.truncate(nul);
        return QString::fromUtf8(raw).trimmed();
    }

    bool readRational(int entryOffset, uint32_t &num, uint32_t &den) const {
        const int valOff = static_cast<int>(u32(entryOffset + 8));
        if (!inRange(valOff, 8))
            return false;
        num = u32(valOff);
        den = u32(valOff + 4);
        return den != 0;
    }

    uint32_t readShortOrLong(int entryOffset, uint16_t type) const {
        if (type == 3)
            return u16(entryOffset + 8);
        if (type == 4)
            return u32(entryOffset + 8);
        return 0;
    }
};

void parseExifFromJpeg(const QByteArray &bytes, QVariantMap &out) {
    const auto *raw = reinterpret_cast<const unsigned char *>(bytes.constData());
    const int n = bytes.size();
    if (n < 16 || raw[0] != 0xFF || raw[1] != 0xD8)
        return;

    // Scan JPEG markers for APP1 (0xFFE1) starting with "Exif\0\0".
    int pos = 2;
    int tiffStart = -1;
    int tiffLen = 0;
    while (pos + 4 < n) {
        if (raw[pos] != 0xFF)
            break;
        const unsigned char marker = raw[pos + 1];
        if (marker == 0xDA || marker == 0xD9) // SOS or EOI
            break;
        const int segLen = (raw[pos + 2] << 8) | raw[pos + 3];
        if (segLen < 2 || pos + 2 + segLen > n)
            break;
        if (marker == 0xE1 && segLen > 8 &&
            std::memcmp(raw + pos + 4, "Exif\0\0", 6) == 0) {
            tiffStart = pos + 10;
            tiffLen = segLen - 8;
            break;
        }
        pos += 2 + segLen;
    }
    if (tiffStart < 0 || tiffLen < 12)
        return;

    TiffReader tiff;
    tiff.data = raw + tiffStart;
    tiff.size = tiffLen;
    if (tiff.data[0] == 'I' && tiff.data[1] == 'I')
        tiff.littleEndian = true;
    else if (tiff.data[0] == 'M' && tiff.data[1] == 'M')
        tiff.littleEndian = false;
    else
        return;

    if (tiff.u16(2) != 42)
        return;

    const int ifd0 = static_cast<int>(tiff.u32(4));
    if (!tiff.inRange(ifd0, 2))
        return;

    QString make, model, dateTaken, exposure, aperture, iso, focal, lens;
    int exifIfdOffset = -1;

    const int count0 = tiff.u16(ifd0);
    for (int i = 0; i < count0; ++i) {
        const int e = ifd0 + 2 + i * 12;
        if (!tiff.inRange(e, 12))
            break;
        const uint16_t tag = tiff.u16(e);
        const uint16_t type = tiff.u16(e + 2);
        const uint32_t cnt = tiff.u32(e + 4);

        if (tag == 0x010F && type == 2)
            make = tiff.readAscii(e, cnt);
        else if (tag == 0x0110 && type == 2)
            model = tiff.readAscii(e, cnt);
        else if (tag == 0x8769)
            exifIfdOffset = static_cast<int>(tiff.readShortOrLong(e, type));
    }

    if (exifIfdOffset > 0 && tiff.inRange(exifIfdOffset, 2)) {
        const int countExif = tiff.u16(exifIfdOffset);
        for (int i = 0; i < countExif; ++i) {
            const int e = exifIfdOffset + 2 + i * 12;
            if (!tiff.inRange(e, 12))
                break;
            const uint16_t tag = tiff.u16(e);
            const uint16_t type = tiff.u16(e + 2);
            const uint32_t cnt = tiff.u32(e + 4);

            if (tag == 0x9003 && type == 2) {
                dateTaken = tiff.readAscii(e, cnt);
            } else if (tag == 0x829A && type == 5) {
                uint32_t num = 0, den = 0;
                if (tiff.readRational(e, num, den) && num > 0) {
                    if (num < den)
                        exposure = QString("1/%1 s").arg(std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(den) / num))));
                    else
                        exposure = QString("%1 s").arg(QString::number(static_cast<double>(num) / den, 'g', 3));
                }
            } else if (tag == 0x829D && type == 5) {
                uint32_t num = 0, den = 0;
                if (tiff.readRational(e, num, den))
                    aperture = QString("f/%1").arg(QString::number(static_cast<double>(num) / den, 'f', 1));
            } else if (tag == 0x8827 && (type == 3 || type == 4)) {
                const uint32_t val = tiff.readShortOrLong(e, type);
                if (val > 0)
                    iso = QString("ISO %1").arg(val);
            } else if (tag == 0x920A && type == 5) {
                uint32_t num = 0, den = 0;
                if (tiff.readRational(e, num, den))
                    focal = QString("%1 mm").arg(QString::number(static_cast<double>(num) / den, 'f', 0));
            } else if (tag == 0xA434 && type == 2) {
                lens = tiff.readAscii(e, cnt);
            }
        }
    }

    QString camera = model;
    if (!make.isEmpty() && !model.startsWith(make, Qt::CaseInsensitive))
        camera = model.isEmpty() ? make : (make + " " + model);

    if (!camera.isEmpty()) out.insert("camera", camera);
    if (!lens.isEmpty()) out.insert("lens", lens);
    if (!dateTaken.isEmpty()) out.insert("dateTaken", dateTaken);
    if (!exposure.isEmpty()) out.insert("exposureTime", exposure);
    if (!aperture.isEmpty()) out.insert("aperture", aperture);
    if (!iso.isEmpty()) out.insert("iso", iso);
    if (!focal.isEmpty()) out.insert("focalLength", focal);
}

} // namespace

namespace Inspector {

QVariantMap readMetadata(const QString &filePath, const QImage &displayImage) {
    QVariantMap info;
    if (filePath.isEmpty() || displayImage.isNull())
        return info;

    const QFileInfo fi(filePath);
    QImageReader reader(filePath);
    const QString fmt = QString::fromLatin1(reader.format()).toUpper();

    info.insert("fileName", fi.fileName());
    info.insert("folderPath", fi.absolutePath());
    info.insert("format", fmt.isEmpty() ? fi.suffix().toUpper() : fmt);
    info.insert("fileBytes", fi.size());
    info.insert("modifiedIso", fi.lastModified().toString(Qt::ISODate));
    info.insert("modifiedMs", fi.lastModified().toMSecsSinceEpoch());
    info.insert("width", displayImage.width());
    info.insert("height", displayImage.height());
    info.insert("megapixels", (static_cast<double>(displayImage.width()) * displayImage.height()) / 1000000.0);
    info.insert("printWidthMm", displayImage.width() * 25.4 / 300.0);
    info.insert("printHeightMm", displayImage.height() * 25.4 / 300.0);
    info.insert("depth", displayImage.depth());
    info.insert("hasAlpha", displayImage.hasAlphaChannel());

    // Read up to first 128 KB of JPEG files for EXIF metadata.
    if (fmt == "JPEG" || fi.suffix().compare("jpg", Qt::CaseInsensitive) == 0 ||
        fi.suffix().compare("jpeg", Qt::CaseInsensitive) == 0) {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly))
            parseExifFromJpeg(f.read(131072), info);
    }

    const bool hasExif = info.contains("camera") || info.contains("dateTaken") ||
                         info.contains("exposureTime") || info.contains("aperture") ||
                         info.contains("iso") || info.contains("focalLength") ||
                         info.contains("lens");
    info.insert("hasExif", hasExif);
    return info;
}

} // namespace Inspector
