#pragma once

#include "filepicker.h"

#include <QDBusObjectPath>
#include <QVariantMap>

// Talks to org.freedesktop.portal.FileChooser over session D-Bus so dialogs
// stay native on Wayland without pulling in Qt Widgets or QFileDialog.
// Pattern follows Omacut (MIT, David Heinemeier Hansson).
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void openImages(const QString &title,
                    std::function<void(const QStringList &paths)> onSelected,
                    std::function<void(const QString &error)> onError) override;

    void savePdf(const QString &title,
                 const QString &suggestedName,
                 const QString &currentFolder,
                 std::function<void(const QString &path)> onSelected,
                 std::function<void(const QString &error)> onError) override;

    void saveFile(const QString &title,
                  const QString &suggestedName,
                  const QString &currentFolder,
                  const QString &filterLabel,
                  const QString &mimeType,
                  std::function<void(const QString &path)> onSelected,
                  std::function<void(const QString &error)> onError) override;

private slots:
    void handlePortalResponse(uint response, const QVariantMap &results);

private:
    void startRequest(const QString &method,
                      const QString &title,
                      const QVariantMap &options,
                      std::function<void(const QStringList &paths)> onSelected,
                      std::function<void(const QString &error)> onError);
    void clearPending();

    QString m_activeRequestPath;
    std::function<void(const QStringList &paths)> m_onSelected;
    std::function<void(const QString &error)> m_onError;
};
