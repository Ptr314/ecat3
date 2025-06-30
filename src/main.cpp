#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

#include <SDL.h>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    MainWindow w;
    w.setWindowIcon(QIcon(":/icons/tv"));
    w.show();

    int RetVal = a.exec();

    SDL_Quit();

    return RetVal;
}
