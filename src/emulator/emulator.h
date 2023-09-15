#ifndef EMULATOR_H
#define EMULATOR_H

#include <QString>
#include <QSettings>
#include <QTimer>

#include <SDL.h>

#include "core.h"
#include "emulator/devices/common/keyboard.h"

class Emulator: public QObject
{
    Q_OBJECT

private:
    QString work_path;
    QString data_path;
    QSettings *settings;
    bool loaded;
    bool busy;
    InterfaceManager *im;
    SystemData sd;

    QChar * charmap[256];

    CPU * cpu;
    MemoryMapper * mm;
    Display * display;
    Keyboard * keyboard;

    QTimer * timer;
    unsigned int clock_freq;
    unsigned int timer_res;
    unsigned int timer_delay;
    unsigned int time_ticks;
    unsigned int local_counter;
    unsigned int clock_counter;

    SDL_Window * SDLWindowRef = nullptr;
    SDL_Renderer * SDLRendererRef = nullptr;
    SDL_Texture * SDLTexture = nullptr;
    QTimer * render_timer;
    SDL_Rect render_rect;

    void register_devices();

public:
    DeviceManager *dm;

    Emulator(QString work_path, QString data_path, QString ini_file);

    QString read_setup(QString section, QString ident, QString def_val);
    void load_config(QString file_name);
    void load_charmap();
    QChar * translate_char(unsigned int system_code);

    void init_video(void *p);
    void stop_video();

    void reset(bool cold);

    void start();
    void stop();

public slots:
    void timer_proc();
    void render_screen();
};

#endif // EMULATOR_H
