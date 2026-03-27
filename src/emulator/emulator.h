// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main emulator class, header

#pragma once

#include <memory>
#include <array>
#include <string>

#include "libs/ini_wrapper.h"

#include "thread_compat.h"

#ifdef RENDERER_SDL2
    #include <SDL.h>
#endif

#include "core.h"
#include "emulator/devices/common/keyboard.h"
#include "renderer.h"

#ifdef LOGGER
#include "logger.h"
#endif

class Emulator
{
private:
    IniSettings settings;
    bool busy;
    InterfaceManager *im;
    SystemData sd;
    VideoRenderer * renderer;

    std::array<std::string, 256> charmap;

    CPU * cpu;
    MemoryMapper * mm;
    GenericDisplay * display;
    Keyboard * keyboard;

    unsigned int clock_freq;
    unsigned int timer_res;
    unsigned int timer_delay;
    unsigned int local_counter;

    unsigned int screen_sx;
    unsigned int screen_sy;
    double screen_scale = 1;
    double pixel_scale;
    int screen_ratio = SCREEN_RATIO_43;
    int screen_filtering = 0;

    void register_devices();

    std::atomic<bool> m_running;
#if USE_QT_THREADING
    EmuThread* emulationThread = nullptr;
    EmuThread* renderThread = nullptr;
#else
    std::thread emulationThread;
    std::thread renderThread;
#endif
    void setThreadPriority(bool timeCritical);
    std::atomic<bool> m_ready;

public:
    DeviceManager *dm;
    std::string work_path;
    std::string data_path;
    std::string software_path;

    bool loaded;

    unsigned int clock_counter;

    Emulator(std::string work_path, std::string data_path, std::string software_path, std::string ini_file, VideoRenderer * renderer);
    ~Emulator();

    emulator::Result load_config(std::string file_name);
    void apply_saved_device_options();

    std::string read_setup(std::string section, std::string ident, std::string def_val);
    void write_setup(std::string section, std::string ident, std::string new_val);
    void load_charmap();
    const std::string & translate_char(unsigned int system_code);

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

    void timer_proc(uint64_t time_ticks);
    void render_screen();

    void key_event(int key, int modifiers, bool press);
    void set_volume(int value);
    void set_muted(bool muted);
    void reset(bool cold);
    void resize_screen();
    void stop_emulation();

private:
    Logger * logger;
public:
    void logs(ComputerDevice * d, std::string s);


};
