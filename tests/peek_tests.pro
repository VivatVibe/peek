QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = peek_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/cli.h \
    ../src/filepicker.h \
    ../src/imageops.h \
    ../src/presetstore.h \
    ../src/printrenderer.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    peek_tests.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/imageops.cpp \
    ../src/presetstore.cpp \
    ../src/printrenderer.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
