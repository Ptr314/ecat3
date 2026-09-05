// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: main.cpp

#include "mainwindow.h"

#include <iostream>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
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

    //Registered whatever the build: an MCP client launches the executable
    //with --mcp, and a build without the server has to say so on stderr and
    //exit rather than pop up a modal "unknown option" box that nobody sees
    QCommandLineOption mcpOption(QStringList() << "mcp",
        QCoreApplication::translate("main", "Act as an MCP server on stdin/stdout, needs a build with ENABLE_MCP, see MCP.md."));
    parser.addOption(mcpOption);

    QCommandLineOption mcpTraceOption(QStringList() << "mcp-trace",
        QCoreApplication::translate("main", "Print the MCP conversation to stderr."));
    parser.addOption(mcpTraceOption);

    QCommandLineOption workdirOption(QStringList() << "workdir",
        QCoreApplication::translate("main", "Directory to work in, normally the one holding computers/."),
        QCoreApplication::translate("main", "directory"));
    parser.addOption(workdirOption);

    parser.process(a);

    const bool mcp_mode  = parser.isSet(mcpOption);
    const bool mcp_trace = parser.isSet(mcpTraceOption);

#ifndef ENABLE_MCP
    if (mcp_mode)
    {
        std::cerr << "This build has no MCP server. Rebuild with -DENABLE_MCP=ON, see MCP.md."
                  << std::endl;
        return 2;
    }
#endif

    //Set before the main window is built: it resolves the emulator root and
    //the ini file against the current directory
    const QString workdir = parser.value(workdirOption);
    if (!workdir.isEmpty() && !QDir::setCurrent(workdir))
    {
        std::cerr << "Unable to change the working directory to "
                  << workdir.toStdString() << std::endl;
        return 2;
    }

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

#ifdef ENABLE_MCP
    if (mcp_mode)
    {
        //No show(): the window appears when the client asks for a machine, so
        //that a session that never touches the emulator costs nothing visible
        w.enable_mcp(mcp_trace);
    }
    else
#else
    (void)mcp_trace;
#endif
    w.show();

    int RetVal = a.exec();

#if defined(RENDERER_SDL2) || defined(USE_SDL_AUDIO)
    SDL_Quit();
#endif

    return RetVal;
}
