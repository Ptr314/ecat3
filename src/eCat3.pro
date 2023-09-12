QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    dialogs/dumparea.cpp \
    dialogs/dumpwindow.cpp \
    emulator/config.cpp \
    emulator/core.cpp \
    emulator/devices/common/i8255.cpp \
    emulator/devices/common/keyboard.cpp \
    emulator/devices/common/scankeyboard.cpp \
    emulator/devices/common/speaker.cpp \
    emulator/devices/common/tape.cpp \
    emulator/devices/cpu/i8080.cpp \
    emulator/devices/specific/o128display.cpp \
    emulator/emulator.cpp \
    emulator/utils.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    dialogs/dumparea.h \
    dialogs/dumpwindow.h \
    emulator/config.h \
    emulator/core.h \
    emulator/debug.h \
    emulator/devices/common/i8255.h \
    emulator/devices/common/keyboard.h \
    emulator/devices/common/scankeyboard.h \
    emulator/devices/common/speaker.h \
    emulator/devices/common/tape.h \
    emulator/devices/cpu/i8080.h \
    emulator/devices/specific/o128display.h \
    emulator/emulator.h \
    emulator/utils.h \
    mainwindow.h

FORMS += \
    dialogs/dumpwindow.ui \
    mainwindow.ui

TRANSLATIONS += \
    translations/en_US.ts \
    translations/ru_RU.ts

CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
