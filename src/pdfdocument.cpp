#include "pdfdocument.h"

#include <poppler-qt6.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <algorithm>
#include <memory>

namespace {

struct CachedPageSearchText {
    bool loaded = false;
    bool hasThai = false;
    bool thaiCmapConflated = false;
    QString rawExact;   // whitespace-collapsed for case-sensitive prefilter
    QString rawLower;   // lowercased whitespace-collapsed for case-insensitive prefilter
    QString thaiStrict; // canonical Thai search text (ำ != า)
    QString thaiFolded; // canonical Thai search text (ำ == า)
};

struct CachedPdf {
    QDateTime modified;
    qint64 size = 0;
    std::shared_ptr<QMutex> docMutex;
    std::shared_ptr<Poppler::Document> doc;
    bool metaLoaded = false;
    PdfMetadata meta;
    bool outlineLoaded = false;
    QVector<PdfOutlineEntry> outline;
    QHash<int, QSizeF> pageSizeMmCache;
    QVector<CachedPageSearchText> searchIndex;
};

struct DocumentHandle {
    std::shared_ptr<QMutex> mutex;
    std::shared_ptr<Poppler::Document> doc;
    QString key;
};

QMutex &cacheMutex() {
    static QMutex mutex;
    return mutex;
}

QHash<QString, CachedPdf> &pdfCache() {
    static QHash<QString, CachedPdf> cache;
    return cache;
}

DocumentHandle openDocument(const QString &path, QString *errorOut) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        if (errorOut)
            *errorOut = QObject::tr("PDF file not found: %1").arg(fi.fileName());
        return {};
    }

    const QString key = fi.absoluteFilePath();
    const QDateTime mod = fi.lastModified();
    const qint64 sz = fi.size();

    {
        QMutexLocker lock(&cacheMutex());
        auto &cache = pdfCache();
        auto it = cache.find(key);
        if (it != cache.end() && it->modified == mod && it->size == sz && it->doc)
            return {it->docMutex, it->doc, key};
    }

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
    auto docMutex = std::make_shared<QMutex>();

    QMutexLocker lock(&cacheMutex());
    auto &cache = pdfCache();
    if (cache.size() >= 16)
        cache.clear();
    CachedPdf entry;
    entry.modified = mod;
    entry.size = sz;
    entry.docMutex = docMutex;
    entry.doc = shared;
    cache.insert(key, entry);
    return {docMutex, shared, key};
}

void collectOutline(const QVector<Poppler::OutlineItem> &items,
                    int depth,
                    int totalPages,
                    QVector<PdfOutlineEntry> &out) {
    for (const Poppler::OutlineItem &item : items) {
        if (item.isNull())
            continue;
        const QString title = item.name().trimmed();
        int targetPage = -1;
        if (const auto dest = item.destination()) {
            const int pageNum = dest->pageNumber();
            if (pageNum >= 1 && pageNum <= totalPages)
                targetPage = pageNum - 1;
        }
        const QVector<Poppler::OutlineItem> kids = item.hasChildren() ? item.children() : QVector<Poppler::OutlineItem>{};
        if (!title.isEmpty()) {
            PdfOutlineEntry entry;
            entry.title = title;
            entry.pageIndex = targetPage;
            entry.depth = depth;
            entry.hasChildren = !kids.isEmpty();
            entry.initiallyOpen = item.isOpen();
            out.append(entry);
        }
        if (!kids.isEmpty())
            collectOutline(kids, depth + 1, totalPages, out);
    }
}

QRectF normalizePageRect(const QRectF &ptRect, const QSizeF &pageSize) {
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0 || ptRect.isNull())
        return {};
    const double x1 = std::clamp(ptRect.left() / pageSize.width(), 0.0, 1.0);
    const double y1 = std::clamp(ptRect.top() / pageSize.height(), 0.0, 1.0);
    const double x2 = std::clamp(ptRect.right() / pageSize.width(), 0.0, 1.0);
    const double y2 = std::clamp(ptRect.bottom() / pageSize.height(), 0.0, 1.0);
    const double w = x2 - x1;
    const double h = y2 - y1;
    if (w <= 1e-6 || h <= 1e-6)
        return {};
    return QRectF(x1, y1, w, h);
}

inline bool isThaiChar(QChar c) {
    const ushort u = c.unicode();
    return u >= 0x0E00 && u <= 0x0E7F;
}

inline QChar mapThaiPua(QChar c) {
    switch (c.unicode()) {
    case 0xF700: return QChar(0x0E10); // ฐ
    case 0xF701: return QChar(0x0E34); // ิ
    case 0xF702: return QChar(0x0E35); // ี
    case 0xF703: return QChar(0x0E36); // ึ
    case 0xF704: return QChar(0x0E37); // ื
    case 0xF705:
    case 0xF70A:
    case 0xF713: return QChar(0x0E48); // ่
    case 0xF706:
    case 0xF70B:
    case 0xF714: return QChar(0x0E49); // ้
    case 0xF707:
    case 0xF70C:
    case 0xF715: return QChar(0x0E4A); // ๊
    case 0xF708:
    case 0xF70D:
    case 0xF716: return QChar(0x0E4B); // ๋
    case 0xF709:
    case 0xF70E:
    case 0xF717: return QChar(0x0E4C); // ์
    case 0xF70F: return QChar(0x0E0D); // ญ
    case 0xF710: return QChar(0x0E31); // ั
    case 0xF711: return QChar(0x0E4D); // ํ
    case 0xF712: return QChar(0x0E47); // ็
    case 0xF718: return QChar(0x0E38); // ุ
    case 0xF719: return QChar(0x0E39); // ู
    case 0xF71A: return QChar(0x0E3A); // ฺ
    default: return c;
    }
}

inline bool isThaiCombiningMark(QChar c) {
    const ushort u = c.unicode();
    return u == 0x0E31 || (u >= 0x0E34 && u <= 0x0E3A) || (u >= 0x0E47 && u <= 0x0E4E);
}

inline bool isThaiAboveBelowVowel(QChar c) {
    const ushort u = c.unicode();
    return u == 0x0E31 || (u >= 0x0E34 && u <= 0x0E3A);
}

inline bool isThaiToneMark(QChar c) {
    const ushort u = c.unicode();
    return u == 0x0E47 || (u >= 0x0E48 && u <= 0x0E4C);
}

// Detects legacy Thai PDF font CMaps that conflated Sara Am (ำ U+0E33) and
// Sara Aa (า U+0E32), either dropping Nikhahit (e.g. ทำงาน -> ทางาน) or mapping
// Sara Aa glyphs to Sara Am (e.g. ผู้ชาย -> ผู้ชำย).
bool detectThaiSaraAmConflation(const QString &canonicalStrict) {
    int cAa = 0;
    int cAm = 0;
    for (QChar c : canonicalStrict) {
        if (c.unicode() == 0x0E32)
            ++cAa;
        else if (c.unicode() == 0x0E33)
            ++cAm;
    }
    return (cAm > cAa && cAm >= 10) || (cAa >= 15 && cAm * 15 <= cAa);
}

QString collapseRawSearchText(const QString &raw) {
    QString out;
    out.reserve(raw.size());
    bool lastWasSpace = true;
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        // Join soft hyphenated line breaks so AcrossLines matches aren't missed.
        if (c == '-' && i + 1 < raw.size() && (raw.at(i + 1) == '\n' || raw.at(i + 1) == '\r')) {
            while (i + 1 < raw.size() && (raw.at(i + 1) == '\n' || raw.at(i + 1) == '\r'))
                ++i;
            continue;
        }
        if (c.isSpace()) {
            if (!lastWasSpace) {
                out.append(' ');
                lastWasSpace = true;
            }
        } else {
            out.append(c);
            lastWasSpace = false;
        }
    }
    return out.trimmed();
}

struct CanonGlyph {
    QChar ch;
    QRectF rect;
    int lineIdx = 0;
};

void canonicalizeThaiGlyphs(QVector<CanonGlyph> &glyphs, bool foldSaraAm, bool caseSensitive) {
    // Pass 1: reorder swapped (tone, vowel) -> (vowel, tone) and compose ํ + [tone] + า -> [tone] + ำ.
    QVector<CanonGlyph> out;
    out.reserve(glyphs.size());
    for (int i = 0; i < glyphs.size(); ++i) {
        const CanonGlyph &g = glyphs.at(i);
        if (g.ch.unicode() == 0x0E4D && i + 1 < glyphs.size()) {
            if (glyphs.at(i + 1).ch.unicode() == 0x0E32) {
                CanonGlyph merged = g;
                merged.ch = QChar(0x0E33);
                merged.rect = g.rect.united(glyphs.at(i + 1).rect);
                out.append(merged);
                ++i;
                continue;
            }
            if (isThaiToneMark(glyphs.at(i + 1).ch) && i + 2 < glyphs.size() &&
                glyphs.at(i + 2).ch.unicode() == 0x0E32) {
                CanonGlyph tone = glyphs.at(i + 1);
                CanonGlyph am = g;
                am.ch = QChar(0x0E33);
                am.rect = g.rect.united(glyphs.at(i + 2).rect);
                out.append(tone);
                out.append(am);
                i += 2;
                continue;
            }
        }
        out.append(g);
    }

    // Pass 2: canonicalize adjacent (tone, vowel) -> (vowel, tone) and (ำ, tone) -> (tone, ำ).
    for (int i = 0; i + 1 < out.size(); ++i) {
        if (isThaiToneMark(out.at(i).ch) && isThaiAboveBelowVowel(out.at(i + 1).ch)) {
            std::swap(out[i].ch, out[i + 1].ch);
        } else if (out.at(i).ch.unicode() == 0x0E33 && isThaiToneMark(out.at(i + 1).ch)) {
            std::swap(out[i].ch, out[i + 1].ch);
        }
    }

    // Pass 3: optional ำ -> า CMap fold and case fold.
    for (CanonGlyph &g : out) {
        if (foldSaraAm && g.ch.unicode() == 0x0E33)
            g.ch = QChar(0x0E32);
        if (!caseSensitive)
            g.ch = g.ch.toLower();
    }

    glyphs = std::move(out);
}

QVector<PdfDocument::PdfPageMatch> searchPageThaiWithTextList(Poppler::Page *page,
                                                              const QString &query,
                                                              bool caseSensitive,
                                                              bool forceFoldSaraAm,
                                                              bool allowFallbackFold) {
    QVector<PdfDocument::PdfPageMatch> matches;
    const QSizeF pageSize = page->pageSizeF();
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0)
        return matches;

    const auto boxes = page->textList();
    if (boxes.empty())
        return matches;

    // Flatten words into per-character normalized glyphs with line indices,
    // dropping spurious whitespace adjacent to Thai characters.
    struct RawBox {
        QString text;
        QRectF normRect;
        QVector<QRectF> charRects;
        bool hasSpaceAfter = false;
        int lineIdx = 0;
    };
    QVector<RawBox> words;
    words.reserve(static_cast<int>(boxes.size()));
    int lineIdx = 0;
    QRectF prevRect;
    for (const auto &box : boxes) {
        if (!box)
            continue;
        const QString rawText = box->text();
        const QRectF wordNorm = normalizePageRect(box->boundingBox(), pageSize);
        if (rawText.isEmpty() || wordNorm.isEmpty())
            continue;

        if (!words.isEmpty()) {
            const double lineTol = std::max(wordNorm.height(), prevRect.height()) * 0.65;
            const bool sameY = std::abs(wordNorm.center().y() - prevRect.center().y()) <= std::max(0.004, lineTol);
            const bool goesBackward = wordNorm.left() + prevRect.height() * 0.5 < prevRect.left();
            if (!sameY || goesBackward)
                ++lineIdx;
        }

        RawBox rb;
        rb.text.reserve(rawText.size());
        rb.normRect = wordNorm;
        rb.hasSpaceAfter = box->hasSpaceAfter();
        rb.lineIdx = lineIdx;
        rb.charRects.reserve(rawText.size());
        const int len = rawText.size();
        for (int i = 0; i < len; ++i) {
            rb.text.append(mapThaiPua(rawText.at(i)));
            QRectF cn = normalizePageRect(box->charBoundingBox(i), pageSize);
            if (cn.isEmpty()) {
                const double cw = wordNorm.width() / std::max(1, len);
                cn = QRectF(wordNorm.x() + cw * i, wordNorm.y(), cw, wordNorm.height());
            }
            rb.charRects.append(cn);
        }
        prevRect = wordNorm;
        words.append(std::move(rb));
    }

    QVector<CanonGlyph> baseGlyphs;
    baseGlyphs.reserve(words.size() * 6);
    for (int wIdx = 0; wIdx < words.size(); ++wIdx) {
        const RawBox &w = words.at(wIdx);
        for (int i = 0; i < w.text.size(); ++i) {
            const QChar c = w.text.at(i);
            if (c.isSpace())
                continue;
            baseGlyphs.append({c, w.charRects.at(i), w.lineIdx});
        }
        // Keep a single space only between two non-Thai words (e.g. "Page 1").
        if (w.hasSpaceAfter && wIdx + 1 < words.size()) {
            const QChar lastCh = !baseGlyphs.isEmpty() ? baseGlyphs.last().ch : QChar();
            const QChar nextCh = !words.at(wIdx + 1).text.isEmpty() ? words.at(wIdx + 1).text.at(0) : QChar();
            if (!isThaiChar(lastCh) && !isThaiChar(nextCh) && !lastCh.isNull() && !nextCh.isNull())
                baseGlyphs.append({' ', QRectF(), w.lineIdx});
        }
    }

    auto runMatch = [&](bool foldSaraAm) -> QVector<PdfDocument::PdfPageMatch> {
        QVector<CanonGlyph> glyphs = baseGlyphs;
        canonicalizeThaiGlyphs(glyphs, foldSaraAm, caseSensitive);

        QString pageStr;
        pageStr.reserve(glyphs.size());
        for (const CanonGlyph &g : glyphs)
            pageStr.append(g.ch);

        QString normQuery = PdfDocument::normalizeThaiText(query, true, foldSaraAm);
        if (!caseSensitive)
            normQuery = normQuery.toLower();
        if (normQuery.isEmpty())
            return {};

        QVector<PdfDocument::PdfPageMatch> found;
        int pos = 0;
        while ((pos = pageStr.indexOf(normQuery, pos)) >= 0) {
            PdfDocument::PdfPageMatch match;
            int curLine = -1;
            QRectF curRect;
            const int end = std::min(pos + static_cast<int>(normQuery.size()), static_cast<int>(glyphs.size()));
            for (int k = pos; k < end; ++k) {
                const CanonGlyph &cg = glyphs.at(k);
                if (cg.rect.isEmpty())
                    continue;
                if (cg.lineIdx != curLine) {
                    if (curLine >= 0 && !curRect.isEmpty())
                        match.rects.append(curRect);
                    curLine = cg.lineIdx;
                    curRect = cg.rect;
                } else {
                    curRect = curRect.united(cg.rect);
                }
            }
            if (curLine >= 0 && !curRect.isEmpty())
                match.rects.append(curRect);
            if (!match.rects.isEmpty())
                found.append(std::move(match));
            pos += std::max(1, static_cast<int>(normQuery.size()));
        }
        return found;
    };

    // Detect CMap conflation on this page from its strict canonical text.
    QVector<CanonGlyph> strictProbe = baseGlyphs;
    canonicalizeThaiGlyphs(strictProbe, false, true);
    QString strictStr;
    strictStr.reserve(strictProbe.size());
    for (const CanonGlyph &g : strictProbe)
        strictStr.append(g.ch);

    const bool useFold = forceFoldSaraAm || detectThaiSaraAmConflation(strictStr);
    matches = runMatch(useFold);
    if (matches.isEmpty() && !useFold && allowFallbackFold) {
        const QString strictQ = PdfDocument::normalizeThaiText(query, true, false);
        if (strictQ.size() >= 3 && (strictQ.contains(QChar(0x0E33)) || strictQ.contains(QChar(0x0E32))))
            matches = runMatch(true);
    }
    return matches;
}

CachedPageSearchText buildPageSearchIndex(Poppler::Page *page) {
    CachedPageSearchText entry;
    entry.loaded = true;
    if (!page)
        return entry;
    const QString raw = page->text(QRectF());
    entry.rawExact = collapseRawSearchText(raw);
    entry.rawLower = entry.rawExact.toLower();
    entry.hasThai = PdfDocument::containsThai(raw);
    if (entry.hasThai) {
        entry.thaiStrict = PdfDocument::normalizeThaiText(raw, true, false);
        entry.thaiCmapConflated = detectThaiSaraAmConflation(entry.thaiStrict);
        entry.thaiFolded = PdfDocument::normalizeThaiText(entry.thaiStrict, true, true);
    }
    return entry;
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
    DocumentHandle h = openDocument(key, errorOut);
    if (!h.doc) {
        // Check specifically if locked so Inspector can report it cleanly.
        std::unique_ptr<Poppler::Document> probe = Poppler::Document::load(key);
        if (probe && probe->isLocked())
            meta.locked = true;
        return meta;
    }

    {
        QMutexLocker cacheLock(&cacheMutex());
        auto it = pdfCache().find(h.key);
        if (it != pdfCache().end() && it->metaLoaded)
            return it->meta;
    }

    {
        QMutexLocker docLock(h.mutex.get());
        meta.valid = true;
        meta.pageCount = h.doc->numPages();
        meta.title = h.doc->info("Title").trimmed();
        meta.author = h.doc->info("Author").trimmed();
        meta.creator = h.doc->info("Creator").trimmed();
        meta.producer = h.doc->info("Producer").trimmed();
        if (meta.pageCount > 0) {
            if (std::unique_ptr<Poppler::Page> page = h.doc->page(0)) {
                const QSizeF pt = page->pageSizeF();
                meta.firstPageSizeMm = QSizeF(pt.width() * 25.4 / 72.0, pt.height() * 25.4 / 72.0);
            }
        }
        QVector<PdfOutlineEntry> outlineEntries;
        const QVector<Poppler::OutlineItem> top = h.doc->outline();
        if (!top.isEmpty())
            collectOutline(top, 0, meta.pageCount, outlineEntries);
        meta.outlineCount = static_cast<int>(outlineEntries.size());

        QMutexLocker cacheLock(&cacheMutex());
        auto it = pdfCache().find(h.key);
        if (it != pdfCache().end()) {
            it->meta = meta;
            it->metaLoaded = true;
            if (!it->outlineLoaded) {
                it->outline = std::move(outlineEntries);
                it->outlineLoaded = true;
            }
        }
    }
    return meta;
}

int pageCount(const QString &path, QString *errorOut) {
    DocumentHandle h = openDocument(path, errorOut);
    if (!h.doc)
        return 0;
    QMutexLocker docLock(h.mutex.get());
    return h.doc->numPages();
}

QSizeF pageSizeMm(const QString &path, int pageIndex) {
    DocumentHandle h = openDocument(path, nullptr);
    if (!h.doc || pageIndex < 0)
        return {};

    {
        QMutexLocker cacheLock(&cacheMutex());
        auto it = pdfCache().find(h.key);
        if (it != pdfCache().end() && it->pageSizeMmCache.contains(pageIndex))
            return it->pageSizeMmCache.value(pageIndex);
    }

    QSizeF mm;
    {
        QMutexLocker docLock(h.mutex.get());
        if (pageIndex >= h.doc->numPages())
            return {};
        std::unique_ptr<Poppler::Page> page = h.doc->page(pageIndex);
        if (!page)
            return {};
        const QSizeF pt = page->pageSizeF();
        mm = QSizeF(pt.width() * 25.4 / 72.0, pt.height() * 25.4 / 72.0);
    }

    QMutexLocker cacheLock(&cacheMutex());
    auto it = pdfCache().find(h.key);
    if (it != pdfCache().end() && it->pageSizeMmCache.size() < 2048)
        it->pageSizeMmCache.insert(pageIndex, mm);
    return mm;
}

QImage renderPage(const QString &path, int pageIndex, double dpi, QString *errorOut) {
    DocumentHandle h = openDocument(path, errorOut);
    if (!h.doc)
        return {};

    QMutexLocker docLock(h.mutex.get());
    if (pageIndex < 0 || pageIndex >= h.doc->numPages()) {
        if (errorOut)
            *errorOut = QObject::tr("PDF page %1 is out of range.").arg(pageIndex + 1);
        return {};
    }

    std::unique_ptr<Poppler::Page> page = h.doc->page(pageIndex);
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

QVector<PdfTextWord> extractTextWords(const QString &path, int pageIndex) {
    QVector<PdfTextWord> words;
    DocumentHandle h = openDocument(path, nullptr);
    if (!h.doc || pageIndex < 0)
        return words;

    QMutexLocker docLock(h.mutex.get());
    if (pageIndex >= h.doc->numPages())
        return words;

    std::unique_ptr<Poppler::Page> page = h.doc->page(pageIndex);
    if (!page)
        return words;

    const QSizeF pageSize = page->pageSizeF();
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0)
        return words;

    const auto boxes = page->textList();
    words.reserve(static_cast<int>(boxes.size()));
    for (const auto &box : boxes) {
        if (!box)
            continue;
        const QString text = box->text();
        const QRectF wordNorm = normalizePageRect(box->boundingBox(), pageSize);
        if (text.isEmpty() || wordNorm.isEmpty())
            continue;

        PdfTextWord word;
        word.text.reserve(text.size());
        for (QChar ch : text)
            word.text.append(mapThaiPua(ch));
        word.normRect = wordNorm;
        word.hasSpaceAfter = box->hasSpaceAfter();
        word.charNormRects.reserve(text.size());

        const int len = text.size();
        for (int i = 0; i < len; ++i) {
            QRectF cn = normalizePageRect(box->charBoundingBox(i), pageSize);
            if (cn.isEmpty()) {
                // Fall back to evenly sliced word box when a glyph reports an empty bbox.
                const double cw = wordNorm.width() / std::max(1, len);
                cn = QRectF(wordNorm.x() + cw * i, wordNorm.y(), cw, wordNorm.height());
            }
            word.charNormRects.append(cn);
        }
        words.append(std::move(word));
    }
    return words;
}

bool containsThai(const QString &text) {
    for (QChar c : text) {
        const QChar m = mapThaiPua(c);
        if (isThaiChar(m))
            return true;
    }
    return false;
}

QString normalizeThaiText(const QString &text, bool forSearch, bool foldSaraAm) {
    if (text.isEmpty())
        return {};

    // Pass 1: map Thai PUA glyphs and clean spurious spaces.
    QString s;
    s.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = mapThaiPua(text.at(i));
        if (forSearch) {
            if (c.isSpace()) {
                // Skip whitespace adjacent to any Thai character; collapse to single space otherwise.
                const QChar prev = !s.isEmpty() ? s.back() : QChar();
                int nextIdx = i + 1;
                while (nextIdx < text.size() && text.at(nextIdx).isSpace())
                    ++nextIdx;
                const QChar next = nextIdx < text.size() ? mapThaiPua(text.at(nextIdx)) : QChar();
                if (!isThaiChar(prev) && !isThaiChar(next) && !prev.isNull() && !next.isNull() && prev != ' ')
                    s.append(' ');
                i = nextIdx - 1;
                continue;
            }
            s.append(c);
        } else {
            // In display/copy mode, drop horizontal spaces inserted by Poppler right
            // before a Thai combining mark (e.g. "รู้จ ัก" -> "รู้จัก"), preserving real word breaks.
            if ((c == ' ' || c == '\t') && i + 1 < text.size() && isThaiCombiningMark(mapThaiPua(text.at(i + 1))))
                continue;
            s.append(c);
        }
    }

    // Pass 2: compose decomposed ํ + [tone] + า -> [tone] + ำ.
    QString composed;
    composed.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i).unicode() == 0x0E4D && i + 1 < s.size()) {
            if (s.at(i + 1).unicode() == 0x0E32) {
                composed.append(QChar(0x0E33));
                ++i;
                continue;
            }
            if (isThaiToneMark(s.at(i + 1)) && i + 2 < s.size() && s.at(i + 2).unicode() == 0x0E32) {
                composed.append(s.at(i + 1));
                composed.append(QChar(0x0E33));
                i += 2;
                continue;
            }
        }
        composed.append(s.at(i));
    }

    // Pass 3: canonicalize swapped (tone, vowel) -> (vowel, tone) and (ำ, tone) -> (tone, ำ).
    for (int i = 0; i + 1 < composed.size(); ++i) {
        if (isThaiToneMark(composed.at(i)) && isThaiAboveBelowVowel(composed.at(i + 1))) {
            const QChar tmp = composed.at(i);
            composed[i] = composed.at(i + 1);
            composed[i + 1] = tmp;
        } else if (composed.at(i).unicode() == 0x0E33 && isThaiToneMark(composed.at(i + 1))) {
            const QChar tmp = composed.at(i);
            composed[i] = composed.at(i + 1);
            composed[i + 1] = tmp;
        }
    }

    if (foldSaraAm)
        composed.replace(QChar(0x0E33), QChar(0x0E32));

    return forSearch ? composed.trimmed() : composed;
}

QString extractPageText(const QString &path, int pageIndex) {
    DocumentHandle h = openDocument(path, nullptr);
    if (!h.doc || pageIndex < 0)
        return {};
    QMutexLocker docLock(h.mutex.get());
    if (pageIndex >= h.doc->numPages())
        return {};
    std::unique_ptr<Poppler::Page> page = h.doc->page(pageIndex);
    if (!page)
        return {};
    const QString raw = page->text(QRectF()).trimmed();
    return containsThai(raw) ? normalizeThaiText(raw, false, false) : raw;
}

QVector<PdfPageMatch> searchPageMatches(const QString &path,
                                        int pageIndex,
                                        const QString &query,
                                        bool caseSensitive) {
    QVector<PdfPageMatch> matches;
    if (query.isEmpty())
        return matches;
    DocumentHandle h = openDocument(path, nullptr);
    if (!h.doc || pageIndex < 0)
        return matches;

    QMutexLocker docLock(h.mutex.get());
    if (pageIndex >= h.doc->numPages())
        return matches;

    std::unique_ptr<Poppler::Page> page = h.doc->page(pageIndex);
    if (!page)
        return matches;

    if (containsThai(query))
        return searchPageThaiWithTextList(page.get(), query, caseSensitive, false, true);

    const QSizeF pageSize = page->pageSizeF();
    if (pageSize.width() <= 0.0 || pageSize.height() <= 0.0)
        return matches;

    Poppler::Page::SearchFlags flags = Poppler::Page::AcrossLines;
    if (!caseSensitive)
        flags |= Poppler::Page::IgnoreCase;

    const QList<QRectF> hits = page->search(query, flags);
    matches.reserve(hits.size());
    for (const QRectF &hit : hits) {
        const QRectF norm = normalizePageRect(hit, pageSize);
        if (!norm.isEmpty())
            matches.append({{norm}});
    }
    return matches;
}

QVector<QRectF> searchPageNormalized(const QString &path,
                                     int pageIndex,
                                     const QString &query,
                                     bool caseSensitive) {
    QVector<QRectF> rects;
    const QVector<PdfPageMatch> matches = searchPageMatches(path, pageIndex, query, caseSensitive);
    rects.reserve(matches.size());
    for (const PdfPageMatch &m : matches) {
        for (const QRectF &r : m.rects)
            rects.append(r);
    }
    return rects;
}

QVector<PdfBatchMatchRef> searchBatchIndexed(const QVector<PdfBatchSearchRequest> &pages,
                                             const QString &query,
                                             bool caseSensitive,
                                             const std::atomic<bool> *cancelFlag) {
    QVector<PdfBatchMatchRef> results;
    if (query.isEmpty() || pages.isEmpty())
        return results;

    const bool thaiQuery = containsThai(query);
    const QString normQueryStrict = thaiQuery
                                        ? (caseSensitive ? normalizeThaiText(query, true, false)
                                                         : normalizeThaiText(query, true, false).toLower())
                                        : (caseSensitive ? collapseRawSearchText(query)
                                                         : collapseRawSearchText(query).toLower());
    if (normQueryStrict.isEmpty())
        return results;
    const QString normQueryFolded = thaiQuery
                                        ? (caseSensitive ? normalizeThaiText(query, true, true)
                                                         : normalizeThaiText(query, true, true).toLower())
                                        : QString();
    const bool canFallbackFold = thaiQuery && normQueryStrict.size() >= 3 &&
                                 (normQueryStrict.contains(QChar(0x0E33)) || normQueryStrict.contains(QChar(0x0E32)));

    // Keep worker-local Poppler documents per sourcePath so building missing
    // search index entries never blocks main-thread rendering or thumbnails.
    struct LocalDocState {
        QString key;
        QVector<CachedPageSearchText> index;
        std::shared_ptr<Poppler::Document> doc;
        int dirtyCount = 0;
    };
    QHash<QString, LocalDocState> docs;

    auto flushCache = [&](LocalDocState &st) {
        if (st.dirtyCount <= 0)
            return;
        QMutexLocker lock(&cacheMutex());
        auto it = pdfCache().find(st.key);
        if (it != pdfCache().end()) {
            if (it->searchIndex.size() < st.index.size())
                it->searchIndex.resize(st.index.size());
            for (int i = 0; i < st.index.size(); ++i) {
                if (st.index.at(i).loaded && !it->searchIndex.at(i).loaded)
                    it->searchIndex[i] = st.index.at(i);
            }
        }
        st.dirtyCount = 0;
    };

    for (const PdfBatchSearchRequest &req : pages) {
        if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
            for (auto &st : docs)
                flushCache(st);
            return {};
        }

        auto it = docs.find(req.sourcePath);
        if (it == docs.end()) {
            LocalDocState st;
            st.key = QFileInfo(req.sourcePath).absoluteFilePath();
            {
                QMutexLocker lock(&cacheMutex());
                auto cit = pdfCache().find(st.key);
                if (cit != pdfCache().end())
                    st.index = cit->searchIndex;
            }
            it = docs.insert(req.sourcePath, std::move(st));
        }

        LocalDocState &st = it.value();
        if (req.sourcePageIndex < 0)
            continue;

        if (req.sourcePageIndex >= st.index.size() || !st.index.at(req.sourcePageIndex).loaded) {
            if (!st.doc) {
                std::unique_ptr<Poppler::Document> raw = Poppler::Document::load(st.key);
                if (raw && !raw->isLocked())
                    st.doc.reset(raw.release());
            }
            if (!st.doc || req.sourcePageIndex >= st.doc->numPages())
                continue;
            if (st.index.size() < st.doc->numPages())
                st.index.resize(st.doc->numPages());
            std::unique_ptr<Poppler::Page> page = st.doc->page(req.sourcePageIndex);
            st.index[req.sourcePageIndex] = buildPageSearchIndex(page.get());
            ++st.dirtyCount;
            if (st.dirtyCount >= 16)
                flushCache(st);
        }

        const CachedPageSearchText &pg = st.index.at(req.sourcePageIndex);
        if (thaiQuery) {
            if (!pg.hasThai)
                continue;
            const bool useFold = pg.thaiCmapConflated;
            const QString &hay = useFold ? pg.thaiFolded : pg.thaiStrict;
            const QString &needle = useFold ? normQueryFolded : normQueryStrict;
            const QString hayCmp = caseSensitive ? hay : hay.toLower();
            int count = 0;
            int pos = 0;
            while ((pos = hayCmp.indexOf(needle, pos)) >= 0) {
                results.append({req.entryIndex, count++});
                pos += std::max(1, static_cast<int>(needle.size()));
            }
            if (count == 0 && !useFold && canFallbackFold) {
                const QString hayFoldCmp = caseSensitive ? pg.thaiFolded : pg.thaiFolded.toLower();
                pos = 0;
                while ((pos = hayFoldCmp.indexOf(normQueryFolded, pos)) >= 0) {
                    results.append({req.entryIndex, count++});
                    pos += std::max(1, static_cast<int>(normQueryFolded.size()));
                }
            }
        } else {
            const QString &hay = caseSensitive ? pg.rawExact : pg.rawLower;
            int count = 0;
            int pos = 0;
            while ((pos = hay.indexOf(normQueryStrict, pos)) >= 0) {
                results.append({req.entryIndex, count++});
                pos += std::max(1, static_cast<int>(normQueryStrict.size()));
            }
        }
    }

    for (auto &st : docs)
        flushCache(st);
    return results;
}

QVector<PdfOutlineEntry> readOutline(const QString &path) {
    QVector<PdfOutlineEntry> entries;
    DocumentHandle h = openDocument(path, nullptr);
    if (!h.doc)
        return entries;

    {
        QMutexLocker cacheLock(&cacheMutex());
        auto it = pdfCache().find(h.key);
        if (it != pdfCache().end() && it->outlineLoaded)
            return it->outline;
    }

    {
        QMutexLocker docLock(h.mutex.get());
        const QVector<Poppler::OutlineItem> top = h.doc->outline();
        if (!top.isEmpty())
            collectOutline(top, 0, h.doc->numPages(), entries);
    }

    QMutexLocker cacheLock(&cacheMutex());
    auto it = pdfCache().find(h.key);
    if (it != pdfCache().end()) {
        it->outline = entries;
        it->outlineLoaded = true;
    }
    return entries;
}

QRectF mapPdfNormToDisplay(const QRectF &pdfNorm,
                           int rotation,
                           bool flipH,
                           bool flipV,
                           const QRect &cropRect,
                           const QSize &sourceSize) {
    if (pdfNorm.isEmpty())
        return {};

    const int rot = ((rotation % 360) + 360) % 360;
    QRectF r = pdfNorm;
    if (rot == 90)
        r = QRectF(1.0 - (pdfNorm.y() + pdfNorm.height()), pdfNorm.x(), pdfNorm.height(), pdfNorm.width());
    else if (rot == 180)
        r = QRectF(1.0 - (pdfNorm.x() + pdfNorm.width()), 1.0 - (pdfNorm.y() + pdfNorm.height()), pdfNorm.width(), pdfNorm.height());
    else if (rot == 270)
        r = QRectF(pdfNorm.y(), 1.0 - (pdfNorm.x() + pdfNorm.width()), pdfNorm.height(), pdfNorm.width());

    if (flipH)
        r.moveLeft(1.0 - (r.x() + r.width()));
    if (flipV)
        r.moveTop(1.0 - (r.y() + r.height()));

    if (!cropRect.isEmpty() && sourceSize.isValid() && !sourceSize.isEmpty()) {
        const QSize postRot = (rot == 90 || rot == 270)
                                  ? QSize(sourceSize.height(), sourceSize.width())
                                  : sourceSize;
        if (postRot.width() > 0 && postRot.height() > 0) {
            const double cx = static_cast<double>(cropRect.x()) / postRot.width();
            const double cy = static_cast<double>(cropRect.y()) / postRot.height();
            const double cw = static_cast<double>(cropRect.width()) / postRot.width();
            const double ch = static_cast<double>(cropRect.height()) / postRot.height();
            if (cw > 1e-6 && ch > 1e-6) {
                r = QRectF((r.x() - cx) / cw, (r.y() - cy) / ch, r.width() / cw, r.height() / ch);
            }
        }
    }

    const QRectF clipped = r.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    return (clipped.width() > 1e-6 && clipped.height() > 1e-6) ? clipped : QRectF();
}

QPointF mapDisplayNormToPdf(const QPointF &displayNorm,
                            int rotation,
                            bool flipH,
                            bool flipV,
                            const QRect &cropRect,
                            const QSize &sourceSize) {
    double x = std::clamp(displayNorm.x(), 0.0, 1.0);
    double y = std::clamp(displayNorm.y(), 0.0, 1.0);
    const int rot = ((rotation % 360) + 360) % 360;

    if (!cropRect.isEmpty() && sourceSize.isValid() && !sourceSize.isEmpty()) {
        const QSize postRot = (rot == 90 || rot == 270)
                                  ? QSize(sourceSize.height(), sourceSize.width())
                                  : sourceSize;
        if (postRot.width() > 0 && postRot.height() > 0) {
            const double cx = static_cast<double>(cropRect.x()) / postRot.width();
            const double cy = static_cast<double>(cropRect.y()) / postRot.height();
            const double cw = static_cast<double>(cropRect.width()) / postRot.width();
            const double ch = static_cast<double>(cropRect.height()) / postRot.height();
            x = cx + x * cw;
            y = cy + y * ch;
        }
    }

    if (flipV)
        y = 1.0 - y;
    if (flipH)
        x = 1.0 - x;

    if (rot == 90)
        return QPointF(std::clamp(y, 0.0, 1.0), std::clamp(1.0 - x, 0.0, 1.0));
    if (rot == 180)
        return QPointF(std::clamp(1.0 - x, 0.0, 1.0), std::clamp(1.0 - y, 0.0, 1.0));
    if (rot == 270)
        return QPointF(std::clamp(1.0 - y, 0.0, 1.0), std::clamp(x, 0.0, 1.0));
    return QPointF(std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0));
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
