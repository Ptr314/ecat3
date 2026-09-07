// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main emulator class, source

#include "dsk_tools/dsk_tools.h"
// MSVC resolves the "utils.h" inside dsk_tools.h against the includer's directory,
// where it finds emulator/utils.h. Pull in the real one explicitly.
#include "libs/dsk_tools/src/utils.h"
#include "host_helpers.h"
#include <cmath>
#include <iostream>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
#endif

#include "globals.h"

#include "core.h"
#include "emulator.h"
#include "emulator/config.h"
#include "emulator/utils.h"
#include "libs/lodepng/lodepng.h"

#include "emulator/devices/cpu/i8080.h"
#include "emulator/devices/common/i8255.h"
#include "emulator/devices/common/sound.h"
#include "emulator/devices/common/speaker.h"
#include "emulator/devices/common/ay8910.h"
#include "emulator/devices/common/tape.h"
#include "emulator/devices/common/scankeyboard.h"
#include "emulator/devices/specific/o128display.h"
#include "emulator/devices/common/wd1793.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/joystick.h"
#include "emulator/devices/common/i8257.h"
#include "emulator/devices/common/i8275.h"
#include "emulator/devices/common/i8275display.h"
#include "emulator/devices/common/i8251.h"
#include "emulator/devices/common/i8253.h"
#include "emulator/devices/common/i8259.h"
#include "emulator/devices/common/register.h"
#include "emulator/devices/common/mux.h"
#include "emulator/devices/cpu/z80.h"
#include "emulator/devices/common/page_mapper.h"
#include "emulator/devices/common/generator.h"
#include "emulator/devices/cpu/6502.h"
#include "emulator/devices/cpu/k1801vm1.h"
#include "emulator/devices/specific/agat_fdc140.h"
#include "emulator/devices/specific/agat_fdc840.h"
#include "emulator/devices/specific/agat_7_display.h"
#include "emulator/devices/specific/agat_9_display.h"
#include "emulator/devices/common/mapkeyboard.h"
#include "emulator/devices/common/ram_address.h"
#include "emulator/devices/specific/irisha_display.h"
#include "emulator/devices/specific/bk_display.h"
#include "emulator/devices/specific/bk_timer.h"
#include "emulator/devices/specific/bk_fdc.h"
#include "devices/common/gmd70.h"
#include "emulator/devices/common/ram_address.h"
#include "emulator/devices/specific/agat_9_mapper.h"


Emulator::Emulator(std::string work_path, std::string data_path, std::string software_path, std::string ini_file, VideoRenderer * renderer):
      work_path(work_path)
    , data_path(data_path)
    , software_path(software_path)
    , loaded(false)
    , busy(false)
    //Assigned by the emulation thread once the machine is up. Until then, and
    //after a machine that failed to load took its devices down with it, there
    //is nothing to point at - and the GUI can reach key_event() at any moment
    , cpu(nullptr)
    , mm(nullptr)
    , display(nullptr)
    , keyboard(nullptr)
    , local_counter(0)
    , clock_counter(0)
    , renderer(renderer)
    , m_running(false)
    , m_ready(false)
    , settings(ini_file)
{

    // connect(this, &Emulator::finished, this, &Emulator::stop_emulation, Qt::DirectConnection);

}

std::string Emulator::read_setup(std::string section, std::string ident, std::string def_val)
{
    return settings.get(section, ident, def_val);
}

std::string Emulator::get_last_path()
{
    std::string path = read_setup("Startup", "last_path", "");
    if (path.empty()) path = software_path;
    return path;
}

void Emulator::set_last_path(const std::string &path)
{
    write_setup("Startup", "last_path", path);
}

void Emulator::write_setup(std::string section, std::string ident, std::string new_val)
{
    settings.set(section, ident, new_val);
    //The value stays in memory, so the running machine sees it; only the file
    //is left alone
    if (m_settings_readonly) return;
    settings.save();
}


emulator::Result Emulator::load_config(std::string file_name)
{
    if (loaded)
    {
        //Delete loaded machine
        delete dm;
        delete im;
        loaded = false;
        //The devices these point at have just been destroyed. If the machine
        //being loaded now fails on the way in, nothing must be left pointing
        //into them: the window stays open and its keys keep arriving
        cpu = nullptr;
        mm = nullptr;
        display = nullptr;
        keyboard = nullptr;
        joysticks.clear();
    }

    dm = new DeviceManager();
    im = new InterfaceManager(dm);

    register_devices();

    EmulatorConfig config;
    emulator::Result res = config.load_from_file(file_name);
    if (!res) return res;

    EmulatorConfigDevice * system = config.get_device("system");
    if (system == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Emulator|" + std::string(QT_TRANSLATE_NOOP("Emulator", "Device 'system' not found in config")) + "}");
    sd.system_file = file_name;
    sd.system_path = dsk_tools::get_file_path(file_name);
    sd.system_type = system->get_parameter("type").value;
    sd.system_name = system->get_parameter("name").value;
    sd.system_version = system->get_parameter("version", false).value;
    sd.system_charmap = system->get_parameter("charmap", false).value;
    sd.software_path = software_path;
    sd.data_path = data_path;
    //A setting of the host, not a number of the machine
    sd.mapper_cache = parse_numeric_value(read_setup("Core", "mapper_cache", "8"), 10);

    //From here on every value without a prefix is read in the notation this
    //machine uses. Validated by EmulatorConfig::load_from_file()
    sd.radix = system->radix;
    set_default_radix(sd.radix);
    sd.read_setup = [this](const std::string &section, const std::string &ident, const std::string &def) {
        return this->read_setup(section, ident, def);
    };

    sd.allowed_files = system->get_parameter("files", false).value;

    load_charmap();

    for (unsigned int i=0; i<config.get_devices_count(); i++)
    {
        EmulatorConfigDevice * d = config.get_device(i);
        if (!d->type.empty())
        {
            res = dm->add_device(im, d);
            if (!res) return res;
        }
    }
    res = dm->load_devices_config(&sd);
    if (!res) return res;
    apply_saved_device_options();
    loaded = true;
    return emulator::Result::ok();
}

void Emulator::apply_saved_device_options()
{
    std::string config_key = dsk_tools::get_file_basename(sd.system_file);

    for (unsigned int i = 0; i < dm->device_count; i++) {
        ComputerDevice * dev = dm->get_device(i)->device.get();
        DeviceOptions options = dev->get_device_options();
        for (size_t j = 0; j < options.size(); j++) {
            const DeviceOption & opt = options[j];
            if (!opt.values.empty()) {
                std::string settings_key = config_key + "_" + dev->name + "_" + std::to_string(opt.id);
                std::string saved = read_setup("DeviceOptions", settings_key, "");
                if (!saved.empty()) {
                    dev->set_device_option(opt.id, static_cast<unsigned int>(std::stoul(saved)));
                }
            }
        }
    }
}

void Emulator::load_charmap()
{
    // Initialize all to default character
    for (auto& ch : charmap) {
        ch = ".";
    }

    if (!sd.system_charmap.empty())
    {
        std::string file_path = data_path + sd.system_charmap + ".chr";
        dsk_tools::UTF8_ifstream f(file_path);
        if (!f.good()) return;

        f.seekg(0, std::ios::end);
        std::streampos size = f.tellg();
        f.seekg(0, std::ios::beg);
        std::string content(static_cast<size_t>(size), '\0');
        f.read(&content[0], size);

        std::vector<std::string> chars = dsk_tools::split_utf8_chars(content);
        unsigned int count = 0;
        for (size_t i = 0; i < chars.size() && count < 256; i++)
        {
            if (chars[i] != "\x0D" && chars[i] != "\x0A") charmap[count++] = chars[i];
        }
    }
}

const std::string & Emulator::translate_char(unsigned int char_code)
{
    if (char_code >= 256) {
        return charmap[0];  // Return default character
    }
    return charmap[char_code];
}

void Emulator::reset(bool cold)
{
    dm->reset_devices(cold);
}

//----------------------------- Scripting ----------------------------------//

ScriptEngine * Emulator::script_engine()
{
    //Created once and kept: the emulation thread dereferences the pointer on
    //every instruction, so it must never be replaced while the machine runs
    if (!script) script.reset(new ScriptEngine(this));
    return script.get();
}

ScriptRecorder * Emulator::script_recorder()
{
    if (!recorder) recorder.reset(new ScriptRecorder(script_engine()));
    return recorder.get();
}

emulator::Result Emulator::load_script(const std::string &file_name)
{
    ScriptEngine * s = script_engine();

    emulator::Result res = s->load(file_name);
    if (!res) return res;

    //Devices look up files next to the script first, so that a script and the
    //images it uses can live in one directory
    sd.script_path = s->get_path();
    return emulator::Result::ok();
}

void Emulator::set_script_file(const std::string &file_name)
{
    ScriptEngine * s = script_engine();
    s->set_file_name(file_name);
    sd.script_path = s->get_path();
}

uint64_t Emulator::ticks_per_ms() const
{
    if (!loaded || cpu == nullptr || cpu->clock < 1000) return 1;
    return cpu->clock / 1000;
}

void Emulator::record_key(unsigned int code, unsigned int native, bool press)
{
    if (recorder && recorder->is_recording()) recorder->key(code, native, press, clock_counter);
}

void Emulator::record_command(const std::string &device, const std::string &member, const std::string &params)
{
    if (recorder && recorder->is_recording()) recorder->command(device, member, params, clock_counter);
}

void Emulator::record_verb(unsigned int verb, const std::vector<std::string> &args)
{
    record_verb_at(verb, args, clock_counter);
}

void Emulator::record_verb_at(unsigned int verb, const std::vector<std::string> &args, uint64_t clock)
{
    if (recorder && recorder->is_recording()) recorder->verb(verb, args, clock);
}

std::string Emulator::script_machine() const
{
    return script?script->get_machine():std::string("");
}

void Emulator::start_script()
{
    if (script) script->start(clock_counter);
}

void Emulator::stop_script()
{
    if (script) script->stop();
}

bool Emulator::script_active() const
{
    return script && script->is_active();
}

bool Emulator::script_finished() const
{
    return !script || script->is_finished();
}

bool Emulator::script_exit_requested() const
{
    return script && script->is_exit_requested();
}

int Emulator::script_exit_code() const
{
    return script?script->get_exit_code():0;
}

void Emulator::run()
{
    if (loaded)
    {
        if (m_running) return;

        m_ready = false;
        m_running = true;

#if USE_QT_THREADING
        emulationThread = EmuThread::create([this]() {
#else
        emulationThread = std::thread([this]() {
#endif
            setThreadPriority(true);

            cpu = dynamic_cast<CPU*>(dm->get_device_by_name("cpu"));
            if (!cpu) {
                // qCritical() << "Error: CPU device not found or wrong type";
                m_running = false;
                return;
            }

            mm = dynamic_cast<MemoryMapper*>(dm->get_device_by_name("mapper"));
            if (!mm) {
                // qCritical() << "Error: Memory mapper device not found or wrong type";
                m_running = false;
                return;
            }

            display = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display"));
            if (!display) {
                // qCritical() << "Error: Display device not found or wrong type";
                m_running = false;
                return;
            }

            keyboard = dynamic_cast<Keyboard*>(dm->get_device_by_name("keyboard"));
            if (!keyboard) {
                // qCritical() << "Error: Keyboard device not found or wrong type";
                m_running = false;
                return;
            }

            joysticks.clear();
            std::vector<ComputerDevice*> joystick_devices = dm->find_devices_by_class("joystick");
            for (size_t i = 0; i < joystick_devices.size(); i++)
                joysticks.push_back(dynamic_cast<Joystick*>(joystick_devices[i]));

            reset(true);

            clock_freq = this->cpu->clock;

            local_counter = 0;
            clock_counter = 0;

            m_ready = true;

#if USE_QT_THREADING
            QElapsedTimer timer;
            timer.start();
            qint64 lastUsecs = timer.nsecsElapsed() / 1000;
            while (m_running) {
                qint64 nowUsecs = timer.nsecsElapsed() / 1000;
                qint64 elapsed = nowUsecs - lastUsecs;

                if (elapsed < 1000) {
                    QThread::yieldCurrentThread();
                    continue;
                }

                lastUsecs = nowUsecs;
#else
            //steady_clock, not high_resolution_clock: the latter is an alias of
            //system_clock in libstdc++, and a clock corrected backwards by NTP
            //would freeze the machine until the wall clock caught up again
            auto lastTime = std::chrono::steady_clock::now();
            while (m_running) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastTime).count();

                if (elapsed < 1000) {
                    std::this_thread::yield();
                    continue;
                }

                lastTime = now;
#endif

                //Everything the machine missed while the host was busy
                //elsewhere is caught up with, without a ceiling: emulated time
                //has to track the wall clock, and a slice capped here would
                //lose the difference for good - a machine under load would run
                //slow, which is exactly what the test suite measures against
                uint64_t time_ticks = (uint64_t)elapsed * clock_freq / 1000000;

                //Last resort: an exception leaving this thread ends the whole
                //process without a word. Devices report through
                //DeviceManager::error() instead, but a stray std::stoi or a
                //bad_alloc must not take the application down with it
                try {
                    timer_proc(time_ticks);
                } catch (const std::exception &) {
                    busy = false;
                    m_running = false;
                }
            }
        });

#if USE_QT_THREADING
        renderThread = EmuThread::create([this]() {
#else
        renderThread = std::thread([this]() {
#endif
            while (m_running) {
                if (!m_ready) {
#if USE_QT_THREADING
                    QThread::yieldCurrentThread();
#else
                    std::this_thread::yield();
#endif
                    continue;
                }
#if USE_QT_THREADING
                QElapsedTimer frameTimer;
                frameTimer.start();

                render_screen();

                qint64 elapsedMs = frameTimer.elapsed();
                int delay = std::max(static_cast<qint64>(0), 20 - elapsedMs);
                if (delay > 0) QThread::msleep(delay);
#else
                auto start = std::chrono::high_resolution_clock::now();

                render_screen();

                auto end = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                int delay = std::max(0, 20 - static_cast<int>(elapsed)); // ~50 FPS

                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
#endif
            }
        });
    }
}

void Emulator::stop_emulation()
{
    if (this->loaded)
    {
        m_running = m_ready = false;

#if USE_QT_THREADING
        if (renderThread) {
            renderThread->join();
            delete renderThread;
            renderThread = nullptr;
        }
        if (emulationThread) {
            emulationThread->join();
            delete emulationThread;
            emulationThread = nullptr;
        }
#else
        if (renderThread.joinable()) {
            renderThread.join();
        }

        if (emulationThread.joinable()) {
            emulationThread.join();
        }
#endif
        renderer->stop();
    }
}

void Emulator::timer_proc(uint64_t time_ticks)
{
    //TODO: Other timer stuff?

    // SDL_Event ev;

    if (!busy)
    {
        busy = true;

        // while (SDL_PollEvent(&ev))
        // {
        //     qDebug() << ev.type;
        // }

        while (local_counter < time_ticks) {

            unsigned int counter = cpu->execute();
            if (counter > 0) {
                local_counter += counter;
                clock_counter += counter;
                dm->clock(counter);

                //Scripts are advanced here, on the emulation thread, so they see
                //the devices in the same state the CPU does. Ticking inside the
                //loop rather than once per time slice makes a script resume at
                //the very same instruction on every run: the size of a slice
                //depends on the host clock, the cycle counter does not.
                if (script) script->tick(clock_counter);
            } else {
                local_counter += 10;

                //A stopped CPU produces no cycles, but the script engine still
                //has to run: the commands that need no emulated time are how a
                //debugging session steps, inspects and starts the CPU again.
                //The delays stay frozen, which is the honest behaviour - they
                //are counted in emulated time and that is what has stopped
                if (script) script->tick(clock_counter);
            }
        }
        //A device refused what the guest asked of it (an unsupported mode, a
        //command the emulator does not model). Throwing from here used to end
        //the process; the machine is stopped instead, and the message stays in
        //the device manager. Starting the CPU again resumes until the next one
        if (dm->error_pending) {
            dm->error_pending = false;
            cpu->m_debug = DEBUG_STOPPED;
        }

        mm->sort_cache();
        local_counter -= time_ticks;

        busy = false;
    }
}


void Emulator::init_video(void *p)
{
    GenericDisplay * d = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display"));
    if (!d) {
        // qCritical() << "Error: Display device not found or wrong type in init_video";
        return;
    }

    d->get_screen_constraints(&screen_sx, &screen_sy);
    screen_scale = std::stod(read_setup("Video", "scale", "2"));
    screen_ratio = std::stoi(read_setup("Video", "ratio", std::to_string(SCREEN_RATIO_43)));
    screen_filtering = std::stoi(read_setup("Video", "filtering", std::to_string(SCREEN_FILTERING_NONE)));

    if (screen_ratio == SCREEN_RATIO_SQ)
        pixel_scale = 1;
    else
    if (screen_ratio == SCREEN_RATIO_43)
        pixel_scale = (4.0 / 3.0) / ((double)screen_sx / (double)screen_sy);
    else
        pixel_scale = ((double)screen_sy / (double)screen_sx);

    renderer->init_screen(p, screen_sx, screen_sy, screen_scale, pixel_scale);
    d->set_renderer(*renderer);
    set_filtering(screen_filtering);
}

void Emulator::stop_video()
{
    renderer->stop();
}

void Emulator::render_screen()
{
    if (m_running && m_ready) {
        unsigned int current_sx, current_sy;
        display->get_screen_constraints(&current_sx, &current_sy);
        if ((current_sx != screen_sx) || (current_sy != screen_sy))
        {
            screen_sx = current_sx;
            screen_sy = current_sy;
            if (screen_ratio == SCREEN_RATIO_SQ)
                pixel_scale = 1;
            else
            if (screen_ratio == SCREEN_RATIO_43)
                pixel_scale = (4.0 / 3.0) / ((double)screen_sx / (double)screen_sy);
            else
                pixel_scale = ((double)screen_sy / (double)screen_sx);
            display->lock_surface();
            renderer->resize(screen_sx, screen_sy, screen_scale, pixel_scale);
            display->set_renderer(*renderer);                                   // We need to update surface
            display->unlock_surface();
        }

        display->validate();

#ifdef WASM_BUILD
        // Always render: raster displays (Agat) write pixels in clock() on the emulation
        // thread and don't set was_updated per frame, so we must push every frame to canvas.
        renderer->render();
        display->was_updated = false;
#else
        if (display->was_updated)
        {
            //Cleared before the frame goes out, not after: the emulation thread
            //sets it whenever it changes the picture, and clearing afterwards
            //would drop a change made while render() was running
            display->was_updated = false;
            renderer->render();
        }
#endif

        //A screenshot requested by a script is taken here, on the render thread,
        //right after a frame has been rendered
        store_screenshot();
    }
}

void Emulator::request_screenshot(const std::string &file_name)
{
    compat_lock_guard lock(m_screenshot_mutex);
    m_screenshot_file = file_name;
    m_screenshot_requested = true;
}

bool Emulator::is_screenshot_pending()
{
    compat_lock_guard lock(m_screenshot_mutex);
    return m_screenshot_requested;
}

bool Emulator::take_screenshot_png(std::vector<unsigned char> &out, uint64_t * serial)
{
    compat_lock_guard lock(m_screenshot_mutex);
    if (serial != nullptr) *serial = m_screenshot_serial;
    if (m_screenshot_png.empty()) return false;
    out = m_screenshot_png;
    return true;
}

void Emulator::store_screenshot()
{
    std::string file_name;
    {
        compat_lock_guard lock(m_screenshot_mutex);
        if (!m_screenshot_requested) return;
        file_name = m_screenshot_file;
    }

    //screen_sx/screen_sy are the dimensions the renderer was last resized to,
    //so they always match the buffer it returns. The display device may already
    //report a new resolution that has not been applied to the renderer yet.
    unsigned int sx = screen_sx;
    unsigned int sy = screen_sy;
    std::vector<uint8_t> image = renderer->get_screenshot();

    if (sx == 0 || sy == 0 || image.size() < static_cast<size_t>(sx) * sy * 4)
    {
        std::cerr << "Screenshot buffer does not match the screen size, skipped" << std::endl;
        compat_lock_guard lock(m_screenshot_mutex);
        m_screenshot_file.clear();
        m_screenshot_requested = false;
        return;
    }

    std::vector<unsigned char> png;
    unsigned int error = lodepng::encode(png, image, sx, sy);
    if (error != 0)
        std::cerr << "Unable to encode a screenshot: " << lodepng_error_text(error) << std::endl;
    else
    {
        //The file is optional: an external driver asks for the bytes instead
        if (!file_name.empty() && lodepng::save_file(png, file_name) != 0)
            std::cerr << "Unable to write a screenshot to " << file_name << std::endl;

        compat_lock_guard lock(m_screenshot_mutex);
        m_screenshot_png.swap(png);
        m_screenshot_serial++;
    }

    //Cleared last: the script waits for this to know the image is ready
    compat_lock_guard lock(m_screenshot_mutex);
    m_screenshot_file.clear();
    m_screenshot_requested = false;
}

void Emulator::resize_screen()
{
    if (display) display->validate(true);
}

void Emulator::key_event(int key, int modifiers, bool press)
{
    //Reached from the GUI thread, which knows nothing about a machine that
    //failed to load or has not started yet
    if (!keyboard || !display) return;

    keyboard->key_event(key, key, press);
    for (size_t i = 0; i < joysticks.size(); i++) joysticks[i]->key_event((unsigned int)key, press);
    if (key == EmuKey::F12) display->validate(true);
    if (press && key == EmuKey::Cancel) {
        reset(modifiers & EmuKey::AltModifier);
    }
}

void Emulator::set_volume(int value)
{
    if (loaded) {
        GenericSound * sound = dynamic_cast<GenericSound*>(dm->get_device_by_name("sound", false));
        if (sound != nullptr) sound->set_volume(value);
    }
}

void Emulator::set_muted(bool muted)
{
    if (loaded) {
        GenericSound * sound = dynamic_cast<GenericSound*>(dm->get_device_by_name("sound", false));
        if (sound != nullptr) sound->set_muted(muted);
    }
}

Emulator::~Emulator()
{
    delete im;
    delete dm;

}

void Emulator::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    if (!display) {
        *sx = *sy = 0;
        return;
    }
    display->get_screen_constraints(sx, sy);
}

SystemData * Emulator::get_system_data()
{
    return &sd;
}

void Emulator::set_scale(int scale)
{
    screen_scale = scale;
    screen_sx = 0;

    write_setup("Video", "scale", std::to_string(screen_scale));
}

void Emulator::set_ratio(int ratio)
{
    screen_ratio = ratio;
    if (ratio == SCREEN_RATIO_SQ)
        pixel_scale = 1;
    else
    if (ratio == SCREEN_RATIO_43)
        pixel_scale = (4.0 / 3.0) / ((double)screen_sx / (double)screen_sy);
    else
        pixel_scale = ((double)screen_sy / (double)screen_sx);

    screen_sx = 0;

    write_setup("Video", "ratio", std::to_string(screen_ratio));
}

void Emulator::set_filtering(int filtering)
{
    renderer->set_filtering(filtering);
    write_setup("Video", "filtering", std::to_string(filtering));
    screen_sx = 0;
// #ifdef RENDERER_SDL2
//     screen_filtering = filtering;
//     std::string s = std::to_string(filtering);
//     char const *pchar = s.c_str();
//     SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, pchar);

//     screen_sx = 0;

//     write_setup("Video", "filtering", std::to_string(filtering));
// #endif
}

int Emulator::get_scale()
{
    return round(screen_scale);
}

int Emulator::get_ratio()
{
    return screen_ratio;
}

int Emulator::get_filtering()
{
    return screen_filtering;
}

void Emulator::setThreadPriority(bool timeCritical) {
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    if (timeCritical) {
        SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    } else {
        SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
    }
#else
    pthread_t thread = pthread_self();
    struct sched_param param;
    int policy;

    pthread_getschedparam(thread, &policy, &param);

    if (timeCritical) {
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        pthread_setschedparam(thread, SCHED_FIFO, &param);
    } else {
        param.sched_priority = sched_get_priority_max(SCHED_OTHER) - 1;
        pthread_setschedparam(thread, SCHED_OTHER, &param);
    }
#endif
}


void Emulator::register_devices()
{
    dm->register_device("ram", create_ram);
    dm->register_device("rom", create_rom);
    dm->register_device("memory-mapper", create_memory_mapper);
    dm->register_device("port", create_port);
    dm->register_device("port-address", create_port_address);
    dm->register_device("speaker", create_speaker);
    dm->register_device("ay8910", create_ay8910);
    dm->register_device("taperecorder", create_tape_recorder);
    dm->register_device("scan-keyboard", create_scankeyboard);
    dm->register_device("i8080", create_i8080);
    dm->register_device("i8255", create_i8255);
    dm->register_device("wd1793", create_WD1793);
    dm->register_device("gmd70", create_GMD70);
    dm->register_device("fdd", create_FDD);
    dm->register_device("orion-128-display", create_o128display);
    dm->register_device("i8257", create_i8257);
    dm->register_device("i8275", create_i8275);
    dm->register_device("i8275-display", create_i8275display);
    dm->register_device("i8251", create_i8251);
    dm->register_device("i8253", create_i8253);
    dm->register_device("i8259", create_i8259);
    dm->register_device("register", create_register);
    dm->register_device("mux", create_mux);
    dm->register_device("z80", create_z80);
    dm->register_device("page-mapper", create_page_mapper);
    dm->register_device("generator", create_generator);
    dm->register_device("6502", create_mos6502);
    dm->register_device("65c02", create_wdc65c02);
    dm->register_device("1801vm1", create_k1801vm1);
    dm->register_device("1801vm2", create_k1801vm2);
    dm->register_device("agat-fdc140", create_agat_fdc140);
    dm->register_device("agat-fdc840", create_agat_fdc840);
    dm->register_device("agat-7-display", create_agat_7_display);
    dm->register_device("agat-9-display", create_agat_9_display);
    dm->register_device("map-keyboard", create_mapkeyboard);
    dm->register_device("joystick", create_joystick);
    dm->register_device("ram-address", create_ram_address);
    dm->register_device("irisha-display", create_irisha_display);
    dm->register_device("bk-display", create_bk_display);
    dm->register_device("bk-timer", create_bk_timer);
    dm->register_device("bk-fdc", create_bk_fdc);
    dm->register_device("agat-9-mapper", create_agat_9_mapper);
}
