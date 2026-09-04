// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: main.cpp

#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLocale>
#include <QTranslator>

#if defined(RENDERER_SDL2) || defined(USE_SDL_AUDIO)
    #include <SDL.h>
#endif

int main(int argc, char *argv[])
{
    #if defined(__linux__)
        qputenv("QT_QPA_PLATFORM", "xcb"); // Enforce using X11 on Linux
    #endif

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    QCoreApplication::setApplicationName("eCat3");
    QCoreApplication::setApplicationVersion(PROJECT_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "eCat3, a universal emulator of retro computers"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configOption(QStringList() << "c" << "config",
        QCoreApplication::translate("main", "Machine configuration to load, overrides the one saved in the ini file."),
        QCoreApplication::translate("main", "file.cfg"));
    parser.addOption(configOption);

    QCommandLineOption scriptOption(QStringList() << "s" << "script",
        QCoreApplication::translate("main", "Script to run, see SCRIPTING.md."),
        QCoreApplication::translate("main", "file.ecat"));
    parser.addOption(scriptOption);

    parser.addPositionalArgument(QCoreApplication::translate("main", "file"),
        QCoreApplication::translate("main", "A .cfg configuration or a .ecat script."));

    parser.process(a);

    QString config_file = parser.value(configOption);
    QString script_file = parser.value(scriptOption);

    //Positional arguments are dispatched by their extension, so that a script
    //or a configuration can simply be dropped onto the executable
    const QStringList positional = parser.positionalArguments();
    for (const QString &arg : positional)
    {
        QString suffix = QFileInfo(arg).suffix().toLower();
        if (suffix == "ecat") {
            if (script_file.isEmpty()) script_file = arg;
        } else if (suffix == "cfg") {
            if (config_file.isEmpty()) config_file = arg;
        }
    }

#ifdef RENDERER_SDL2
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
#elif defined(USE_SDL_AUDIO)
    SDL_Init(SDL_INIT_AUDIO);
#endif

    MainWindow w(config_file, script_file);
    w.setWindowIcon(QIcon(":/icons/tv"));
    w.show();

    int RetVal = a.exec();

#if defined(RENDERER_SDL2) || defined(USE_SDL_AUDIO)
    SDL_Quit();
#endif

    return RetVal;
}
