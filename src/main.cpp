#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

#ifdef RENDERER_SDL2
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
    #else
        SDL_Init(SDL_INIT_AUDIO);
    #endif

    MainWindow w;
    w.setWindowIcon(QIcon(":/icons/tv"));
    w.show();

    int RetVal = a.exec();

    #ifdef RENDERER_SDL2
        SDL_Quit();
    #else
        // We still need it for sound
        SDL_Quit();
    #endif

    return RetVal;
}
