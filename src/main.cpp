#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

#include <SDL.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.setWindowIcon(QIcon(":/icons/tv"));
    w.show();

    int RetVal = a.exec();

    SDL_Quit();

    return RetVal;
}
