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
#include "emulator/devices/common/joystick.h"
#include "emulator/script/script_engine.h"
#include "emulator/script/script_recorder.h"
#include "renderer.h"

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
    std::vector<Joystick*> joysticks;   // every device of the joystick class, fed with the same keys

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

    std::unique_ptr<ScriptEngine> script;
    std::unique_ptr<ScriptRecorder> recorder;
    //An empty file name no longer means "nothing was asked for": an external
    //driver wants the bytes without a file, so the request is a flag of its own
    std::string m_screenshot_file;              //Guarded by m_screenshot_mutex
    bool m_screenshot_requested = false;        //Guarded by m_screenshot_mutex
    std::vector<unsigned char> m_screenshot_png;//Guarded; the last image encoded
    uint64_t m_screenshot_serial = 0;           //Guarded; incremented for every image
    compat_mutex m_screenshot_mutex;
    bool m_settings_readonly = false;
    void store_screenshot();

public:
    DeviceManager *dm;
    std::string work_path;
    std::string data_path;
    std::string software_path;

    bool loaded;

    //CPU cycles since the machine was started. Written on the emulation
    //thread for every instruction; the interface thread reads it to time
    //stamp recorded events, a stale or torn value there costs at most one
    //delay that is a few milliseconds off
    uint64_t clock_counter;

    Emulator(std::string work_path, std::string data_path, std::string software_path, std::string ini_file, VideoRenderer * renderer);
    ~Emulator();

    emulator::Result load_config(std::string file_name);
    void apply_saved_device_options();

    std::string read_setup(std::string section, std::string ident, std::string def_val);
    void write_setup(std::string section, std::string ident, std::string new_val);

    //An external driver runs many short sessions in the working tree, and
    //every one of them would otherwise rewrite the ini file
    void set_settings_readonly(bool on) { m_settings_readonly = on; }
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

    //--------------------------- Scripting --------------------------------//
    //A script is parsed before the machine is loaded, because its MACHINE
    //command may select the configuration to start with
    emulator::Result load_script(const std::string &file_name);
    std::string script_machine() const;
    void start_script();
    void stop_script();
    bool script_active() const;
    bool script_finished() const;
    bool script_exit_requested() const;
    int  script_exit_code() const;

    //The engine and the recorder are created on first use and live as long
    //as the emulator: the GUI record / replay controls work on them directly
    ScriptEngine * script_engine();
    ScriptRecorder * script_recorder();
    void set_script_file(const std::string &file_name);     //Names a recorded buffer after a saved file

    uint64_t clock_now() const { return clock_counter; }
    uint64_t ticks_per_ms() const;                          //CPU cycles per emulated millisecond

    //Recording hooks for the GUI, no-ops unless a recording is in progress
    void record_key(unsigned int code, unsigned int native, bool press);
    void record_command(const std::string &device, const std::string &member, const std::string &params);
    void record_verb(unsigned int verb, const std::vector<std::string> &args);
    void record_verb_at(unsigned int verb, const std::vector<std::string> &args, uint64_t clock);

    //Asks the render thread to store the current screen as a PNG. An empty
    //file name keeps the image in memory only, see take_screenshot_png()
    void request_screenshot(const std::string &file_name);
    bool is_screenshot_pending();

    //Copies the last image the render thread produced. The serial tells the
    //caller whether it is looking at its own image or at an older one
    bool take_screenshot_png(std::vector<unsigned char> &out, uint64_t * serial = nullptr);

private:
public:


};
