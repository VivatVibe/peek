#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

// Abstract file picker interface so Backend uses xdg-desktop-portal in
// production and a fake picker in automated tests.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    ~FilePicker() override = default;

    virtual void openImages(const QString &title,
                            std::function<void(const QStringList &paths)> onSelected,
                            std::function<void(const QString &error)> onError) = 0;

    virtual void savePdf(const QString &title,
                         const QString &suggestedName,
                         const QString &currentFolder,
                         std::function<void(const QString &path)> onSelected,
                         std::function<void(const QString &error)> onError) = 0;
};
