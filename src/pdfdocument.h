#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>
#include <QVector>

struct PdfMetadata {
    int pageCount = 0;
    QString title;
    QString author;
    QString creator;
    QString producer;
    QSizeF firstPageSizeMm;
    bool locked = false;
    bool valid = false;
};

namespace PdfDocument {

bool isPdfFile(const QString &path);
PdfMetadata readDocumentInfo(const QString &path, QString *errorOut = nullptr);
int pageCount(const QString &path, QString *errorOut = nullptr);
QSizeF pageSizeMm(const QString &path, int pageIndex);
QImage renderPage(const QString &path, int pageIndex, double dpi = 150.0, QString *errorOut = nullptr);
void evictCache(const QString &path = {});

// Parses 1-based page ranges like "1-3, 5, 8-10" into 0-based page indices.
// An empty expression selects all pages [0..totalPages-1].
QVector<int> parsePageRange(const QString &expr, int totalPages, bool *ok = nullptr);

} // namespace PdfDocument
