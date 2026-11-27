#include "presetstore.h"
#include "imageops.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace {

QString defaultStorePath() {
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(cfg.isEmpty() ? QDir::homePath() + "/.config" : cfg).filePath("omarchy-preview/presets.json");
}

QString slugify(const QString &name) {
    QString slug;
    for (QChar ch : name.trimmed().toLower()) {
        if (ch.isLetterOrNumber())
            slug.append(ch);
        else if (!slug.isEmpty() && !slug.endsWith('-'))
            slug.append('-');
    }
    while (slug.endsWith('-'))
        slug.chop(1);
    return slug.isEmpty() ? QUuid::createUuid().toString(QUuid::Id128).left(8) : slug;
}

} // namespace

PresetStore::PresetStore(const QString &filePath)
    : m_filePath(filePath.isEmpty() ? defaultStorePath() : filePath) {
    load();
}

QVector<PrintPreset> PresetStore::builtInPresets() {
    QVector<PrintPreset> list;

    PrintPreset def;
    def.id = "default";
    def.name = "Default Settings";
    def.builtIn = true;
    def.settings = PrintSettings{};
    list.append(def);

    PrintPreset photo;
    photo.id = "builtin:photo-4x6";
    photo.name = "Photo 4×6 Borderless";
    photo.builtIn = true;
    photo.settings.paperSize = "4x6";
    photo.settings.orientation = "auto";
    photo.settings.scaleMode = "fill";
    photo.settings.marginMode = "borderless";
    photo.settings.grayscale = false;
    photo.settings.perPage = 1;
    list.append(photo);

    PrintPreset bwDraft;
    bwDraft.id = "builtin:bw-draft";
    bwDraft.name = "Black & White Draft";
    bwDraft.builtIn = true;
    bwDraft.settings.paperSize = "a4";
    bwDraft.settings.orientation = "auto";
    bwDraft.settings.scaleMode = "fit";
    bwDraft.settings.marginMode = "standard";
    bwDraft.settings.grayscale = true;
    bwDraft.settings.perPage = 1;
    list.append(bwDraft);

    PrintPreset fourUp;
    fourUp.id = "builtin:four-up";
    fourUp.name = "4-Up Sheet with Cut Guides";
    fourUp.builtIn = true;
    fourUp.settings.paperSize = "a4";
    fourUp.settings.orientation = "auto";
    fourUp.settings.scaleMode = "fit";
    fourUp.settings.marginMode = "narrow";
    fourUp.settings.grayscale = false;
    fourUp.settings.perPage = 4;
    fourUp.settings.cutGuides = true;
    list.append(fourUp);

    return list;
}

QJsonObject PresetStore::settingsToJson(const PrintSettings &s, const QString &printer) {
    QJsonObject obj;
    if (!printer.isEmpty())
        obj.insert("printer", printer);
    obj.insert("paperSize", s.paperSize);
    if (s.paperSize.startsWith("custom") && s.customPaperWidthMm >= 10.0 && s.customPaperHeightMm >= 10.0) {
        obj.insert("customPaperWidthMm", s.customPaperWidthMm);
        obj.insert("customPaperHeightMm", s.customPaperHeightMm);
    }
    obj.insert("orientation", s.orientation);
    obj.insert("scaleMode", s.scaleMode);
    obj.insert("scalePercent", s.scalePercent);
    obj.insert("marginMode", s.marginMode);
    obj.insert("grayscale", s.grayscale);
    obj.insert("copies", s.copies);
    obj.insert("perPage", s.perPage);
    obj.insert("cutGuides", s.cutGuides);
    if (!s.mediaTypeKey.isEmpty() && !s.mediaTypeValue.isEmpty()) {
        obj.insert("mediaTypeKey", s.mediaTypeKey);
        obj.insert("mediaTypeValue", s.mediaTypeValue);
    }
    if (!s.qualityKey.isEmpty() && !s.qualityValue.isEmpty()) {
        obj.insert("qualityKey", s.qualityKey);
        obj.insert("qualityValue", s.qualityValue);
    }
    if (!s.duplexKey.isEmpty() && !s.duplexValue.isEmpty()) {
        obj.insert("duplexKey", s.duplexKey);
        obj.insert("duplexValue", s.duplexValue);
    }
    return obj;
}

PrintSettings PresetStore::settingsFromJson(const QJsonObject &obj, QString *printerOut) {
    PrintSettings s;
    if (printerOut)
        *printerOut = obj.value("printer").toString();
    if (obj.contains("paperSize")) {
        const QString ps = obj.value("paperSize").toString().trimmed();
        if (ps.startsWith("custom")) {
            s.paperSize = ps;
            s.customPaperWidthMm = std::clamp(obj.value("customPaperWidthMm").toDouble(0.0), 0.0, 2000.0);
            s.customPaperHeightMm = std::clamp(obj.value("customPaperHeightMm").toDouble(0.0), 0.0, 2000.0);
            if (s.customPaperWidthMm < 10.0 || s.customPaperHeightMm < 10.0) {
                s.paperSize = "a4";
                s.customPaperWidthMm = 0.0;
                s.customPaperHeightMm = 0.0;
            }
        } else {
            s.paperSize = PrintRenderer::paperSpec(ps).id;
        }
    }
    const QString orient = obj.value("orientation").toString();
    if (orient == "portrait" || orient == "landscape" || orient == "auto")
        s.orientation = orient;
    const QString scaleMode = obj.value("scaleMode").toString();
    if (scaleMode == "fit" || scaleMode == "fill" || scaleMode == "scale")
        s.scaleMode = scaleMode;
    if (obj.contains("scalePercent"))
        s.scalePercent = std::clamp(obj.value("scalePercent").toInt(100), 10, 200);
    const QString margin = obj.value("marginMode").toString();
    if (margin == "standard" || margin == "narrow" || margin == "borderless")
        s.marginMode = margin;
    s.grayscale = obj.value("grayscale").toBool(false);
    s.copies = std::clamp(obj.value("copies").toInt(1), 1, 99);
    s.perPage = PrintRenderer::normalizePerPage(obj.value("perPage").toInt(1));
    s.cutGuides = obj.value("cutGuides").toBool(false);
    s.mediaTypeKey = obj.value("mediaTypeKey").toString();
    s.mediaTypeValue = obj.value("mediaTypeValue").toString();
    s.qualityKey = obj.value("qualityKey").toString();
    s.qualityValue = obj.value("qualityValue").toString();
    s.duplexKey = obj.value("duplexKey").toString();
    s.duplexValue = obj.value("duplexValue").toString();
    return s;
}

bool PresetStore::load() {
    m_hasLastUsed = false;
    m_lastUsedSettings = PrintSettings{};
    m_lastUsedPrinter.clear();
    m_activePresetId = "last-used";
    m_exportFormat.clear();
    m_exportQuality = 90;
    m_customPresets.clear();
    m_customPapers.clear();

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    if (root.value("lastUsed").isObject()) {
        m_lastUsedSettings = settingsFromJson(root.value("lastUsed").toObject(), &m_lastUsedPrinter);
        m_hasLastUsed = true;
    }

    const QString savedActive = root.value("activePresetId").toString();
    if (!savedActive.isEmpty())
        m_activePresetId = savedActive;

    const QString expFmt = root.value("exportFormat").toString();
    if (!expFmt.isEmpty())
        m_exportFormat = ImageOps::normalizedFormat(expFmt);
    if (root.contains("exportQuality"))
        m_exportQuality = std::clamp(root.value("exportQuality").toInt(90), 10, 100);

    const QJsonArray presetsArr = root.value("presets").toArray();
    for (const QJsonValue &val : presetsArr) {
        if (!val.isObject())
            continue;
        const QJsonObject item = val.toObject();
        const QString name = item.value("name").toString().trimmed();
        if (name.isEmpty())
            continue;

        PrintPreset preset;
        preset.id = item.value("id").toString();
        if (preset.id.isEmpty())
            preset.id = "user:" + slugify(name);
        preset.name = name;
        preset.builtIn = false;
        preset.removable = true;
        preset.settings = settingsFromJson(item.value("settings").toObject(), &preset.printer);
        m_customPresets.append(preset);
    }

    const QJsonArray papersArr = root.value("customPapers").toArray();
    for (const QJsonValue &val : papersArr) {
        if (!val.isObject())
            continue;
        const QJsonObject item = val.toObject();
        const QString name = item.value("name").toString().trimmed();
        const double w = item.value("widthMm").toDouble(0.0);
        const double h = item.value("heightMm").toDouble(0.0);
        if (name.isEmpty() || w < 10.0 || h < 10.0 || w > 2000.0 || h > 2000.0)
            continue;
        CustomPaperSize paper;
        paper.id = item.value("id").toString();
        if (paper.id.isEmpty())
            paper.id = "custom:" + slugify(name);
        paper.name = name;
        paper.widthMm = w;
        paper.heightMm = h;
        m_customPapers.append(paper);
    }

    return true;
}

bool PresetStore::save() const {
    const QFileInfo info(m_filePath);
    QDir parentDir = info.dir();
    if (!parentDir.exists() && !parentDir.mkpath("."))
        return false;

    QJsonObject root;
    root.insert("version", 1);
    root.insert("activePresetId", m_activePresetId);
    if (m_hasLastUsed)
        root.insert("lastUsed", settingsToJson(m_lastUsedSettings, m_lastUsedPrinter));
    if (!m_exportFormat.isEmpty())
        root.insert("exportFormat", m_exportFormat);
    root.insert("exportQuality", m_exportQuality);

    QJsonArray presetsArr;
    for (const auto &preset : m_customPresets) {
        QJsonObject item;
        item.insert("id", preset.id);
        item.insert("name", preset.name);
        item.insert("settings", settingsToJson(preset.settings, preset.printer));
        presetsArr.append(item);
    }
    root.insert("presets", presetsArr);

    QJsonArray papersArr;
    for (const auto &paper : m_customPapers) {
        QJsonObject item;
        item.insert("id", paper.id);
        item.insert("name", paper.name);
        item.insert("widthMm", paper.widthMm);
        item.insert("heightMm", paper.heightMm);
        papersArr.append(item);
    }
    root.insert("customPapers", papersArr);

    // Atomic write: write beside target, then rename so settings never corrupt.
    const QString tempPath = parentDir.filePath(
        "." + info.fileName() + "." + QUuid::createUuid().toString(QUuid::Id128) + ".tmp");
    QFile tmp(tempPath);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!tmp.open(QIODevice::WriteOnly) || tmp.write(payload) != payload.size()) {
        QFile::remove(tempPath);
        return false;
    }
    tmp.close();

    QFile::remove(m_filePath);
    if (!QFile::rename(tempPath, m_filePath)) {
        QFile::remove(tempPath);
        return false;
    }
    return true;
}

void PresetStore::setLastUsed(const PrintSettings &settings, const QString &printer) {
    m_lastUsedSettings = settings;
    m_lastUsedPrinter = printer;
    m_hasLastUsed = true;
}

void PresetStore::setActivePresetId(const QString &id) {
    if (!id.isEmpty())
        m_activePresetId = id;
}

void PresetStore::setExportDefaults(const QString &format, int quality) {
    m_exportFormat = ImageOps::normalizedFormat(format);
    m_exportQuality = std::clamp(quality, 10, 100);
}

QVector<PrintPreset> PresetStore::allPresets() const {
    QVector<PrintPreset> all;

    PrintPreset last;
    last.id = "last-used";
    last.name = "Last Used Settings";
    last.builtIn = true;
    last.settings = m_lastUsedSettings;
    last.printer = m_lastUsedPrinter;
    all.append(last);

    for (const auto &b : builtInPresets())
        all.append(b);
    for (const auto &c : m_customPresets)
        all.append(c);
    return all;
}

const PrintPreset *PresetStore::findById(const QString &id, PrintPreset &out) const {
    for (const auto &p : allPresets()) {
        if (p.id == id) {
            out = p;
            return &out;
        }
    }
    return nullptr;
}

bool PresetStore::findByNameOrId(const QString &query, PrintPreset &out) const {
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return false;
    for (const auto &p : allPresets()) {
        if (p.id.toLower() == q || p.name.toLower() == q) {
            out = p;
            return true;
        }
    }
    return false;
}

QString PresetStore::saveCustomPreset(const QString &rawName, const PrintSettings &settings, const QString &printer) {
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return {};

    // Updating an existing custom preset with the same name (case-insensitive)
    // replaces its settings in place, just like macOS Print Presets.
    for (auto &existing : m_customPresets) {
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
            existing.name = name;
            existing.settings = settings;
            existing.printer = printer;
            m_activePresetId = existing.id;
            save();
            return existing.id;
        }
    }

    QString candidateId = "user:" + slugify(name);
    int suffix = 2;
    auto idExists = [&](const QString &id) {
        for (const auto &p : m_customPresets)
            if (p.id == id)
                return true;
        return false;
    };
    while (idExists(candidateId))
        candidateId = "user:" + slugify(name) + "-" + QString::number(suffix++);

    PrintPreset created;
    created.id = candidateId;
    created.name = name;
    created.builtIn = false;
    created.removable = true;
    created.settings = settings;
    created.printer = printer;
    m_customPresets.append(created);
    m_activePresetId = created.id;
    save();
    return created.id;
}

bool PresetStore::deleteCustomPreset(const QString &id) {
    for (int i = 0; i < m_customPresets.size(); ++i) {
        if (m_customPresets.at(i).id == id) {
            m_customPresets.removeAt(i);
            if (m_activePresetId == id)
                m_activePresetId = "last-used";
            save();
            return true;
        }
    }
    return false;
}

bool PresetStore::findCustomPaperById(const QString &id, CustomPaperSize &out) const {
    for (const auto &paper : m_customPapers) {
        if (paper.id == id) {
            out = paper;
            return true;
        }
    }
    return false;
}

bool PresetStore::findCustomPaperByNameOrId(const QString &query, CustomPaperSize &out) const {
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return false;
    for (const auto &paper : m_customPapers) {
        if (paper.id.toLower() == q || paper.name.toLower() == q) {
            out = paper;
            return true;
        }
    }
    return false;
}

QString PresetStore::saveCustomPaper(const QString &rawName, double widthMm, double heightMm) {
    const QString name = rawName.trimmed();
    if (name.isEmpty() || widthMm < 10.0 || heightMm < 10.0 || widthMm > 2000.0 || heightMm > 2000.0)
        return {};

    for (auto &existing : m_customPapers) {
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
            existing.name = name;
            existing.widthMm = widthMm;
            existing.heightMm = heightMm;
            save();
            return existing.id;
        }
    }

    QString candidateId = "custom:" + slugify(name);
    int suffix = 2;
    auto idExists = [&](const QString &id) {
        for (const auto &p : m_customPapers)
            if (p.id == id)
                return true;
        return false;
    };
    while (idExists(candidateId))
        candidateId = "custom:" + slugify(name) + "-" + QString::number(suffix++);

    CustomPaperSize created;
    created.id = candidateId;
    created.name = name;
    created.widthMm = widthMm;
    created.heightMm = heightMm;
    m_customPapers.append(created);
    save();
    return created.id;
}

bool PresetStore::deleteCustomPaper(const QString &id) {
    for (int i = 0; i < m_customPapers.size(); ++i) {
        if (m_customPapers.at(i).id == id) {
            m_customPapers.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}
