// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main emulator class, header

#pragma once

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

class Emulator: public QObject
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
    // uint64_t time_ticks;
    unsigned int local_counter;

    QTimer * render_timer;

    unsigned int screen_sx;
    unsigned int screen_sy;
    double screen_scale = 1;
    double pixel_scale;
    int screen_ratio = SCREEN_RATIO_43;
    int screen_filtering = 0;

    void register_devices();

    std::atomic<bool> running;
    std::thread emulationThread;
    std::thread renderThread;
    void setThreadPriority(bool timeCritical);

public:
    DeviceManager *dm;
    QString work_path;
    QString data_path;
    QString software_path;

    bool loaded;

    unsigned int clock_counter;

    Emulator(QString work_path, QString data_path, QString software_path, QString ini_file, VideoRenderer * renderer);
    ~Emulator();

    void load_config(QString file_name);

    QString read_setup(QString section, QString ident, QString def_val);
    void write_setup(QString section, QString ident, QString new_val);
    void load_charmap();
    QChar * translate_char(unsigned int system_code);

    void init_video(void *p);
    void stop_video();

    void run();

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
    void timer_proc(uint64_t time_ticks);
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
