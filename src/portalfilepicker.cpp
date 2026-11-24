// Portal file picker follows Omacut (MIT, David Heinemeier Hansson).
#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QUrl>
#include <QUuid>

namespace {

struct PortalFilterPattern {
    uint type; // 0 = glob, 1 = MIME type
    QString pattern;
};

struct PortalFilter {
    QString label;
    QList<PortalFilterPattern> patterns;
};

QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilterPattern &item) {
    arg.beginStructure();
    arg << item.type << item.pattern;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilterPattern &item) {
    arg.beginStructure();
    arg >> item.type >> item.pattern;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilter &filter) {
    arg.beginStructure();
    arg << filter.label << filter.patterns;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilter &filter) {
    arg.beginStructure();
    arg >> filter.label >> filter.patterns;
    arg.endStructure();
    return arg;
}

void registerPortalMetaTypes() {
    static bool registered = false;
    if (registered)
        return;
    qDBusRegisterMetaType<PortalFilterPattern>();
    qDBusRegisterMetaType<QList<PortalFilterPattern>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<QList<PortalFilter>>();
    registered = true;
}

} // namespace

Q_DECLARE_METATYPE(PortalFilterPattern)
Q_DECLARE_METATYPE(QList<PortalFilterPattern>)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(QList<PortalFilter>)

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    registerPortalMetaTypes();
}

void PortalFilePicker::openImages(const QString &title,
                                  std::function<void(const QStringList &paths)> onSelected,
                                  std::function<void(const QString &error)> onError) {
    QList<PortalFilter> filters;
    filters.append({
        tr("Images"),
        {
            {1, "image/png"},
            {1, "image/jpeg"},
            {1, "image/webp"},
            {1, "image/bmp"},
            {1, "image/gif"},
            {1, "image/tiff"},
            {1, "image/svg+xml"},
        }
    });
    filters.append({tr("All files"), {{0, "*"}}});

    QVariantMap options;
    options.insert("multiple", true);
    options.insert("filters", QVariant::fromValue(filters));
    startRequest("OpenFile", title, options, std::move(onSelected), std::move(onError));
}

void PortalFilePicker::savePdf(const QString &title,
                               const QString &suggestedName,
                               const QString &currentFolder,
                               std::function<void(const QString &path)> onSelected,
                               std::function<void(const QString &error)> onError) {
    saveFile(title, suggestedName, currentFolder, tr("PDF documents"), "application/pdf",
             std::move(onSelected), std::move(onError));
}

void PortalFilePicker::saveFile(const QString &title,
                                const QString &suggestedName,
                                const QString &currentFolder,
                                const QString &filterLabel,
                                const QString &mimeType,
                                std::function<void(const QString &path)> onSelected,
                                std::function<void(const QString &error)> onError) {
    QList<PortalFilter> filters;
    filters.append({filterLabel, {{1, mimeType}}});
    filters.append({tr("All files"), {{0, "*"}}});

    QVariantMap options;
    options.insert("current_name", suggestedName);
    if (!currentFolder.isEmpty()) {
        // The portal expects a NUL-terminated byte array for filesystem paths.
        QByteArray folderBytes = currentFolder.toUtf8();
        folderBytes.append('\0');
        options.insert("current_folder", folderBytes);
    }
    options.insert("filters", QVariant::fromValue(filters));

    startRequest("SaveFile", title, options,
                 [cb = std::move(onSelected)](const QStringList &paths) {
                     if (!paths.isEmpty() && cb)
                         cb(paths.first());
                 },
                 std::move(onError));
}

void PortalFilePicker::startRequest(const QString &method,
                                    const QString &title,
                                    const QVariantMap &options,
                                    std::function<void(const QStringList &paths)> onSelected,
                                    std::function<void(const QString &error)> onError) {
    clearPending();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (onError)
            onError(tr("Session D-Bus is unavailable, so the file picker cannot open."));
        return;
    }

    const QString token = "peek_" + QUuid::createUuid().toString(QUuid::Id128);
    QString senderName = bus.baseService();
    senderName.remove(0, 1); // drop leading ':'
    senderName.replace('.', '_');
    const QString expectedPath = "/org/freedesktop/portal/desktop/request/" + senderName + "/" + token;

    QVariantMap fullOptions = options;
    fullOptions.insert("handle_token", token);

    m_activeRequestPath = expectedPath;
    m_onSelected = std::move(onSelected);
    m_onError = std::move(onError);

    // Connect before issuing the call so fast responses are never missed.
    bus.connect("org.freedesktop.portal.Desktop",
                expectedPath,
                "org.freedesktop.portal.Request",
                "Response",
                this,
                SLOT(handlePortalResponse(uint, QVariantMap)));

    QDBusInterface portal("org.freedesktop.portal.Desktop",
                          "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.FileChooser",
                          bus);
    QDBusReply<QDBusObjectPath> reply = portal.call(method, QString(), title, fullOptions);
    if (!reply.isValid()) {
        auto errCb = std::move(m_onError);
        clearPending();
        if (errCb)
            errCb(tr("Could not open the desktop file picker: %1").arg(reply.error().message()));
        return;
    }

    // Some portal implementations return a different path than predicted; rebind if needed.
    const QString actualPath = reply.value().path();
    if (!actualPath.isEmpty() && actualPath != m_activeRequestPath) {
        bus.disconnect("org.freedesktop.portal.Desktop",
                       m_activeRequestPath,
                       "org.freedesktop.portal.Request",
                       "Response",
                       this,
                       SLOT(handlePortalResponse(uint, QVariantMap)));
        m_activeRequestPath = actualPath;
        bus.connect("org.freedesktop.portal.Desktop",
                    m_activeRequestPath,
                    "org.freedesktop.portal.Request",
                    "Response",
                    this,
                    SLOT(handlePortalResponse(uint, QVariantMap)));
    }
}

void PortalFilePicker::handlePortalResponse(uint response, const QVariantMap &results) {
    auto onSelected = std::move(m_onSelected);
    auto onError = std::move(m_onError);
    clearPending();

    if (response == 1)
        return; // User cancelled cleanly.

    if (response != 0) {
        if (onError)
            onError(tr("The file picker closed unexpectedly."));
        return;
    }

    const QStringList uris = results.value("uris").toStringList();
    QStringList paths;
    for (const QString &uri : uris) {
        const QUrl url(uri);
        const QString localPath = url.isLocalFile() ? url.toLocalFile() : uri;
        if (!localPath.isEmpty())
            paths.append(localPath);
    }

    if (!paths.isEmpty() && onSelected)
        onSelected(paths);
}

void PortalFilePicker::clearPending() {
    if (!m_activeRequestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            "org.freedesktop.portal.Desktop",
            m_activeRequestPath,
            "org.freedesktop.portal.Request",
            "Response",
            this,
            SLOT(handlePortalResponse(uint, QVariantMap)));
        m_activeRequestPath.clear();
    }
    m_onSelected = nullptr;
    m_onError = nullptr;
}
