#pragma once

#include "printrenderer.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

struct CustomPaperSize {
    QString id;       // "custom:<slug>"
    QString name;     // User-facing label (e.g. "Sticker 100×150")
    double widthMm = 210.0;
    double heightMm = 297.0;
};

struct PrintPreset {
    QString id;       // "last-used" | "default" | "builtin:..." | "user:..."
    QString name;     // Display name (localized in QML for built-in IDs)
    bool builtIn = false;
    bool removable = false;
    PrintSettings settings;
    QString printer;
};

// Persists "Last Used Settings", export defaults, and named user print presets
// to ~/.config/peek/presets.json using atomic writes. Tests pass a temp path.
class PresetStore {
public:
    explicit PresetStore(const QString &filePath = {});

    QString filePath() const { return m_filePath; }
    bool load();
    bool save() const;

    bool hasLastUsed() const { return m_hasLastUsed; }
    PrintSettings lastUsedSettings() const { return m_lastUsedSettings; }
    QString lastUsedPrinter() const { return m_lastUsedPrinter; }
    void setLastUsed(const PrintSettings &settings, const QString &printer);

    QString activePresetId() const { return m_activePresetId; }
    void setActivePresetId(const QString &id);

    QString exportFormat() const { return m_exportFormat; }
    int exportQuality() const { return m_exportQuality; }
    void setExportDefaults(const QString &format, int quality);

    QVector<PrintPreset> allPresets() const;
    QVector<PrintPreset> customPresets() const { return m_customPresets; }
    const PrintPreset *findById(const QString &id, PrintPreset &out) const;
    bool findByNameOrId(const QString &query, PrintPreset &out) const;

    QString saveCustomPreset(const QString &name, const PrintSettings &settings, const QString &printer);
    bool deleteCustomPreset(const QString &id);

    QVector<CustomPaperSize> customPapers() const { return m_customPapers; }
    bool findCustomPaperById(const QString &id, CustomPaperSize &out) const;
    bool findCustomPaperByNameOrId(const QString &query, CustomPaperSize &out) const;
    QString saveCustomPaper(const QString &name, double widthMm, double heightMm);
    bool deleteCustomPaper(const QString &id);

    static QVector<PrintPreset> builtInPresets();
    static QJsonObject settingsToJson(const PrintSettings &s, const QString &printer);
    static PrintSettings settingsFromJson(const QJsonObject &obj, QString *printerOut = nullptr);

private:
    QString m_filePath;
    bool m_hasLastUsed = false;
    PrintSettings m_lastUsedSettings;
    QString m_lastUsedPrinter;
    QString m_activePresetId = "last-used";
    QString m_exportFormat;
    int m_exportQuality = 90;
    QVector<PrintPreset> m_customPresets;
    QVector<CustomPaperSize> m_customPapers;
};
