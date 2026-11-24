QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = peek
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/filepicker.h \
    src/imageops.h \
    src/portalfilepicker.h \
    src/printrenderer.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/backend.cpp \
    src/cli.cpp \
    src/imageops.cpp \
    src/main.cpp \
    src/portalfilepicker.cpp \
    src/printrenderer.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
