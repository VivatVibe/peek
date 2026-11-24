#include "pdfdocument.h"

#include <poppler-qt6.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <memory>

namespace {

struct CachedPdf {
    QDateTime modified;
    qint64 size = 0;
    std::shared_ptr<Poppler::Document> doc;
};

QMutex &cacheMutex() {
    static QMutex mutex;
    return mutex;
}

QHash<QString, CachedPdf> &pdfCache() {
    static QHash<QString, CachedPdf> cache;
    return cache;
}

std::shared_ptr<Poppler::Document> openDocument(const QString &path, QString *errorOut) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        if (errorOut)
            *errorOut = QObject::tr("PDF file not found: %1").arg(fi.fileName());
        return {};
    }

    const QString key = fi.absoluteFilePath();
    const QDateTime mod = fi.lastModified();
    const qint64 sz = fi.size();

    QMutexLocker lock(&cacheMutex());
    auto &cache = pdfCache();
    auto it = cache.find(key);
    if (it != cache.end() && it->modified == mod && it->size == sz && it->doc)
        return it->doc;

    if (cache.size() >= 16)
        cache.clear();

    std::unique_ptr<Poppler::Document> raw = Poppler::Document::load(key);
    if (!raw) {
        if (errorOut)
            *errorOut = QObject::tr("Could not open PDF: %1").arg(fi.fileName());
        return {};
    }
    if (raw->isLocked()) {
        if (errorOut)
            *errorOut = QObject::tr("PDF is password-protected: %1").arg(fi.fileName());
        return {};
    }

    raw->setRenderHint(Poppler::Document::Antialiasing, true);
    raw->setRenderHint(Poppler::Document::TextAntialiasing, true);

    std::shared_ptr<Poppler::Document> shared(raw.release());
    cache.insert(key, {mod, sz, shared});
    return shared;
}

} // namespace

namespace PdfDocument {

bool isPdfFile(const QString &path) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return false;
    if (fi.suffix().compare("pdf", Qt::CaseInsensitive) == 0)
        return true;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return f.read(5) == "%PDF-";
    return false;
}

PdfMetadata readDocumentInfo(const QString &path, QString *errorOut) {
    PdfMetadata meta;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        if (errorOut)
            *errorOut = QObject::tr("PDF file not found: %1").arg(fi.fileName());
        return meta;
    }

    const QString key = fi.absoluteFilePath();
    std::shared_ptr<Poppler::Document> doc = openDocument(key, errorOut);
    if (!doc) {
        // Check specifically if locked so Inspector can report it cleanly.
        std::unique_ptr<Poppler::Document> probe = Poppler::Document::load(key);
        if (probe && probe->isLocked())
            meta.locked = true;
        return meta;
    }

    meta.valid = true;
    meta.pageCount = doc->numPages();
    meta.title = doc->info("Title").trimmed();
    meta.author = doc->info("Author").trimmed();
    meta.creator = doc->info("Creator").trimmed();
    meta.producer = doc->info("Producer").trimmed();
    if (meta.pageCount > 0) {
        if (std::unique_ptr<Poppler::Page> page = doc->page(0)) {
            const QSizeF pt = page->pageSizeF();
            meta.firstPageSizeMm = QSizeF(pt.width() * 25.4 / 72.0, pt.height() * 25.4 / 72.0);
        }
    }
    return meta;
}

int pageCount(const QString &path, QString *errorOut) {
    auto doc = openDocument(path, errorOut);
    return doc ? doc->numPages() : 0;
}

QSizeF pageSizeMm(const QString &path, int pageIndex) {
    auto doc = openDocument(path, nullptr);
    if (!doc || pageIndex < 0 || pageIndex >= doc->numPages())
        return {};
    std::unique_ptr<Poppler::Page> page = doc->page(pageIndex);
    if (!page)
        return {};
    const QSizeF pt = page->pageSizeF();
    return QSizeF(pt.width() * 25.4 / 72.0, pt.height() * 25.4 / 72.0);
}

QImage renderPage(const QString &path, int pageIndex, double dpi, QString *errorOut) {
    auto doc = openDocument(path, errorOut);
    if (!doc)
        return {};
    if (pageIndex < 0 || pageIndex >= doc->numPages()) {
        if (errorOut)
            *errorOut = QObject::tr("PDF page %1 is out of range.").arg(pageIndex + 1);
        return {};
    }

    std::unique_ptr<Poppler::Page> page = doc->page(pageIndex);
    if (!page) {
        if (errorOut)
            *errorOut = QObject::tr("Could not load PDF page %1.").arg(pageIndex + 1);
        return {};
    }

    const QImage rendered = page->renderToImage(dpi, dpi);
    if (rendered.isNull() && errorOut)
        *errorOut = QObject::tr("Could not render PDF page %1.").arg(pageIndex + 1);
    return rendered;
}

void evictCache(const QString &path) {
    QMutexLocker lock(&cacheMutex());
    if (path.isEmpty())
        pdfCache().clear();
    else
        pdfCache().remove(QFileInfo(path).absoluteFilePath());
}

QVector<int> parsePageRange(const QString &expr, int totalPages, bool *ok) {
    auto finish = [ok](bool valid, QVector<int> out = {}) {
        if (ok)
            *ok = valid;
        return out;
    };

    const QString trimmed = expr.trimmed();
    if (trimmed.isEmpty()) {
        QVector<int> all;
        all.reserve(std::max(0, totalPages));
        for (int i = 0; i < totalPages; ++i)
            all.append(i);
        return finish(true, std::move(all));
    }

    if (totalPages <= 0)
        return finish(false);

    QVector<int> result;
    QSet<int> seen;
    const QStringList tokens = trimmed.split(',');
    for (const QString &rawToken : tokens) {
        const QString tok = rawToken.trimmed();
        if (tok.isEmpty())
            return finish(false);

        const int dash = tok.indexOf('-');
        if (dash >= 0) {
            if (tok.indexOf('-', dash + 1) >= 0)
                return finish(false);
            bool okStart = false, okEnd = false;
            const int start = tok.left(dash).trimmed().toInt(&okStart);
            const int end = tok.mid(dash + 1).trimmed().toInt(&okEnd);
            if (!okStart || !okEnd || start < 1 || end < start || end > totalPages)
                return finish(false);
            for (int p = start; p <= end; ++p) {
                const int idx = p - 1;
                if (!seen.contains(idx)) {
                    seen.insert(idx);
                    result.append(idx);
                }
            }
        } else {
            bool okPage = false;
            const int p = tok.toInt(&okPage);
            if (!okPage || p < 1 || p > totalPages)
                return finish(false);
            const int idx = p - 1;
            if (!seen.contains(idx)) {
                seen.insert(idx);
                result.append(idx);
            }
        }
    }

    const bool nonEmpty = !result.isEmpty();
    return finish(nonEmpty, std::move(result));
}

} // namespace PdfDocument
