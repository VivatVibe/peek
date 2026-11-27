#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <atomic>

struct PdfMetadata {
    int pageCount = 0;
    int outlineCount = 0;
    QString title;
    QString author;
    QString creator;
    QString producer;
    QSizeF firstPageSizeMm;
    bool locked = false;
    bool valid = false;
};

struct PdfTextWord {
    QString text;
    QRectF normRect;
    QVector<QRectF> charNormRects;
    bool hasSpaceAfter = false;
};

struct PdfOutlineEntry {
    QString title;
    int pageIndex = -1; // 0-based page index in the source PDF, or -1 if external/none
    int depth = 0;
    bool hasChildren = false;
    bool initiallyOpen = true;
};

namespace PdfDocument {

bool isPdfFile(const QString &path);
PdfMetadata readDocumentInfo(const QString &path, QString *errorOut = nullptr);
int pageCount(const QString &path, QString *errorOut = nullptr);
QSizeF pageSizeMm(const QString &path, int pageIndex);
QImage renderPage(const QString &path, int pageIndex, double dpi = 150.0, QString *errorOut = nullptr);
void evictCache(const QString &path = {});

// Text extraction, search, and document outline (TOC).
QVector<PdfTextWord> extractTextWords(const QString &path, int pageIndex);
QString extractPageText(const QString &path, int pageIndex);
QString normalizeThaiText(const QString &text, bool forSearch = false, bool foldSaraAm = false);
bool containsThai(const QString &text);

// A single search hit on a page may span multiple line rectangles when wrapped.
struct PdfPageMatch {
    QVector<QRectF> rects;
};

QVector<PdfPageMatch> searchPageMatches(const QString &path,
                                        int pageIndex,
                                        const QString &query,
                                        bool caseSensitive = false);
QVector<QRectF> searchPageNormalized(const QString &path,
                                     int pageIndex,
                                     const QString &query,
                                     bool caseSensitive = false);
struct PdfBatchSearchRequest {
    int entryIndex = 0;
    QString sourcePath;
    int sourcePageIndex = 0;
};
struct PdfBatchMatchRef {
    int entryIndex = 0;
    int matchIndexOnPage = 0;
};
// Fast cached cross-page search (builds reusable per-page text index once per PDF
// using worker-local Poppler documents, then scans all pages in milliseconds).
QVector<PdfBatchMatchRef> searchBatchIndexed(const QVector<PdfBatchSearchRequest> &pages,
                                             const QString &query,
                                             bool caseSensitive,
                                             const std::atomic<bool> *cancelFlag = nullptr);
QVector<PdfOutlineEntry> readOutline(const QString &path);

// Coordinate conversions between unrotated normalized PDF page coordinates (0..1)
// and transformed/cropped display coordinates (0..1).
QRectF mapPdfNormToDisplay(const QRectF &pdfNorm,
                           int rotation,
                           bool flipH,
                           bool flipV,
                           const QRect &cropRect,
                           const QSize &sourceSize);
QPointF mapDisplayNormToPdf(const QPointF &displayNorm,
                            int rotation,
                            bool flipH,
                            bool flipV,
                            const QRect &cropRect,
                            const QSize &sourceSize);

// Parses 1-based page ranges like "1-3, 5, 8-10" into 0-based page indices.
// An empty expression selects all pages [0..totalPages-1].
QVector<int> parsePageRange(const QString &expr, int totalPages, bool *ok = nullptr);

} // namespace PdfDocument
