QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += poppler-qt6
TARGET = peek_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/cli.h \
    ../src/filepicker.h \
    ../src/imageops.h \
    ../src/inspector.h \
    ../src/markup.h \
    ../src/pdfdocument.h \
    ../src/presetstore.h \
    ../src/printrenderer.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    peek_tests.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/imageops.cpp \
    ../src/inspector.cpp \
    ../src/markup.cpp \
    ../src/pdfdocument.cpp \
    ../src/presetstore.cpp \
    ../src/printrenderer.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
