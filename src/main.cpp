// peek — Image preview and effortless printing for Omarchy.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "printrenderer.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Headless CLI commands run without a display server.
    if (Cli::isHeadlessCommand(argc, argv) && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("peek");
    app.setApplicationVersion("0.2.0");

    QStringList guiFiles;
    if (const auto exitCode = Cli::run(app.arguments(), guiFiles))
        return *exitCode;

    // Matches the window to peek.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("peek");
    app.setWindowIcon(QIcon::fromTheme("peek"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker filePicker;
    Backend backend(&filePicker);
    if (!guiFiles.isEmpty())
        backend.openFiles(guiFiles);

    qmlRegisterType<PrintPreviewItem>("Peek", 1, 0, "PrintPreview");

    QQmlApplicationEngine engine;
    engine.addImageProvider("peek", new PeekImageProvider(&backend));
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
