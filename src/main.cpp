// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: main.cpp

#include "mainwindow.h"

#include <QApplication>
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

#ifdef RENDERER_SDL2
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
#elif defined(USE_SDL_AUDIO)
    SDL_Init(SDL_INIT_AUDIO);
#endif

    MainWindow w;
    w.setWindowIcon(QIcon(":/icons/tv"));
    w.show();

    int RetVal = a.exec();

#if defined(RENDERER_SDL2) || defined(USE_SDL_AUDIO)
    SDL_Quit();
#endif

    return RetVal;
}
