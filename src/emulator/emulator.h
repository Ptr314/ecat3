#ifndef EMULATOR_H
#define EMULATOR_H

#include <QString>
#include <QSettings>
#include <QTimer>
#include <QThread>

#ifdef RENDERER_SDL2
    #include <SDL.h>
#endif

#include "core.h"
#include "emulator/devices/common/keyboard.h"
#include "renderer.h"

#ifdef LOGGER
#include "logger.h"
#endif

class Emulator: public QThread
{
    Q_OBJECT

private:
    QSettings *settings;
    bool busy;
    InterfaceManager *im;
    SystemData sd;
    VideoRenderer * renderer;

    QChar * charmap[256];

    CPU * cpu;
    MemoryMapper * mm;
    GenericDisplay * display;
    Keyboard * keyboard;

    QTimer * timer;
    unsigned int clock_freq;
    unsigned int timer_res;
    unsigned int timer_delay;
    unsigned int time_ticks;
    unsigned int local_counter;
    unsigned int clock_counter;

    QTimer * render_timer;

    unsigned int screen_sx;
    unsigned int screen_sy;
    double screen_scale = 1;
    double pixel_scale;
    int screen_ratio = SCREEN_RATIO_43;
    int screen_filtering = 0;

    void register_devices();

public:
    DeviceManager *dm;
    QString work_path;
    QString data_path;
    QString software_path;

    bool loaded;
    bool use_threads;


    Emulator(QString work_path, QString data_path, QString software_path, QString ini_file, VideoRenderer * renderer);
    ~Emulator();

    void load_config(QString file_name);

    QString read_setup(QString section, QString ident, QString def_val);
    void write_setup(QString section, QString ident, QString new_val);
    void load_charmap();
    QChar * translate_char(unsigned int system_code);

    void init_video(void *p);
    void stop_video();

    void run() override;

    // SURFACE * get_surface();
    void get_screen_constraints(unsigned int * sx, unsigned int * sy);

    SystemData * get_system_data();

    void set_scale(int scale);
    void set_ratio(int ratio);
    void set_filtering(int filtering);

    int get_scale();
    int get_ratio();
    int get_filtering();

public slots:
    void timer_proc();
    void render_screen();

    void key_event(QKeyEvent *event, bool press);
    void set_volume(int value);
    void set_muted(bool muted);
    void reset(bool cold);
    void resize_screen();
    void stop_emulation();

signals:

private:
    Logger * logger;
public:
    void logs(ComputerDevice * d, QString s);


};

#endif // EMULATOR_H
