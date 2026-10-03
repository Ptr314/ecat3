// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main emulator class, source

#include "dsk_tools/core.h"
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
#include "emulator/state_save.h"
#include "libs/lodepng/lodepng.h"

#include "emulator/devices/cpu/i8080.h"
#include "emulator/devices/common/i8255.h"
#include "emulator/devices/common/sound.h"
#include "emulator/devices/common/speaker.h"
#include "emulator/devices/common/ay8910.h"
#include "emulator/devices/common/covox.h"
#include "emulator/devices/common/tape.h"
#include "emulator/devices/common/scankeyboard.h"
#include "emulator/devices/specific/o128display.h"
#include "emulator/devices/common/wd1793.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/joystick.h"
#include "emulator/devices/common/mouse.h"
#include "emulator/devices/common/connector.h"
#include "emulator/devices/common/i8257.h"
#include "emulator/devices/common/i8275.h"
#include "emulator/devices/common/i8275display.h"
#include "emulator/devices/common/i8251.h"
#include "emulator/devices/common/dl11.h"
#include "emulator/devices/common/i8253.h"
#include "emulator/devices/common/i8259.h"
#include "emulator/devices/common/register.h"
#include "emulator/devices/common/mux.h"
#include "emulator/devices/cpu/z80.h"
#include "emulator/devices/common/page_mapper.h"
#include "emulator/devices/common/plane_pair.h"
#include "emulator/devices/common/indirect_memory.h"
#include "emulator/devices/common/generator.h"
#include "emulator/devices/cpu/6502.h"
#include "emulator/devices/cpu/k1801vm1.h"
#include "emulator/devices/cpu/i8086.h"
#include "emulator/devices/specific/agat_fdc140.h"
#include "emulator/devices/specific/agat_fdc840.h"
#include "emulator/devices/specific/agat_7_display.h"
#include "emulator/devices/specific/agat_9_display.h"
#include "emulator/devices/common/mapkeyboard.h"
#include "emulator/devices/common/ram_address.h"
#include "emulator/devices/specific/irisha_display.h"
#include "emulator/devices/specific/bk_display.h"
#include "emulator/devices/specific/poisk.h"
#include "emulator/devices/specific/bk_timer.h"
#include "emulator/devices/specific/bk_fdc.h"
#include "devices/common/gmd70.h"
#include "emulator/devices/common/ram_address.h"
#include "emulator/devices/specific/agat_9_mapper.h"
#include "emulator/devices/specific/agat_yazs.h"
#include "emulator/devices/specific/uknc_channels.h"
#include "emulator/devices/specific/uknc_display.h"
#include "emulator/devices/specific/uknc_graphics.h"
#include "emulator/devices/specific/uknc_hdd.h"
#include "emulator/devices/specific/smk512.h"
#include "emulator/devices/specific/a16m.h"
#include "emulator/devices/specific/uknc_timer.h"
#include "emulator/devices/specific/uknc_keyboard.h"
#include "emulator/devices/specific/uknc_sound.h"
#include "emulator/devices/specific/argo_keyboard.h"
#include "emulator/devices/specific/argo_memory.h"
#include "emulator/devices/specific/zx_keyboard.h"
#include "emulator/devices/specific/unior_memory.h"


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
    //The destructor deletes both. Without this, quitting before the first
    //machine has loaded - a script file that is not there, a bad argument -
    //deletes whatever the stack happened to hold
    , dm(nullptr)
    , im(nullptr)
    , clock_counter(0)
    , renderer(renderer)
    , m_running(false)
    , m_ready(false)
    , m_calls_pending(false)
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


void Emulator::drop_machine()
{
    //Whatever stopped the old machine is no news about the next one
    clear_machine_error();

    //Not only a loaded machine: one that failed on the way in has its devices
    //too, some of them with an audio device already open
    delete dm;
    delete im;
    dm = nullptr;
    im = nullptr;
    //Only now: every device holds a ComputerDevice::cd pointing into it
    m_config.free_devices();
    loaded = false;
    //The devices these point at have just been destroyed. If the machine
    //being loaded now fails on the way in, nothing must be left pointing
    //into them: the window stays open and its keys keep arriving
    cpu = nullptr;
    mm = nullptr;
    display = nullptr;
    keyboard = nullptr;
    domains.clear();
    joysticks.clear();
    mice.clear();
}

emulator::Result Emulator::load_config(std::string file_name)
{
    drop_machine();

    emulator::Result res = emulator::Result::ok();
    try {
        res = build_machine(file_name);
    } catch (const std::exception &ex) {
        //A required parameter that is missing throws, and so may a device
        //constructor; no frontend expects an exception from a load
        res = emulator::Result::error(emulator::ErrorCode::ConfigError, ex.what());
    }

    if (!res)
    {
        //Half a machine is of no use to anybody, and the frontend would go on
        //listing its devices. What is left is a machine with no devices
        drop_machine();
        dm = new DeviceManager();
        im = new InterfaceManager(dm);
    }
    return res;
}

emulator::Result Emulator::build_machine(const std::string &file_name)
{
    dm = new DeviceManager();
    im = new InterfaceManager(dm);

    register_devices();

    //The script of the previous extension belongs to that machine, and so
    //does its directory in the file search. One loaded explicitly stays: a
    //replay or a MACHINE line may be what switches machines. The emulation
    //thread is stopped by now, a demo may still be waiting in a WAIT
    if (m_embedded_loaded && script)
    {
        script->stop();
        script->clear();
        sd.script_path.clear();
    }
    m_embedded_loaded = false;

    //A .cfg as it is, or an extension applied to its base. Kept for the
    //lifetime of the machine: every ComputerDevice::cd points into it, and
    //the snapshot writer writes it out as the state's own configuration
    EmulatorConfig &config = m_config;
    MachinePaths paths;
    paths.computers_path = work_path;
    paths.cache_path = cache_path;
    emulator::Result res = load_machine_description(file_name, paths, config, m_source);
    if (!res) return res;

    EmulatorConfigDevice * system = config.get_device("system");
    if (system == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Emulator|" + std::string(QT_TRANSLATE_NOOP("Emulator", "Device 'system' not found in config")) + "}");
    //The file asked for names the machine (ini keys, MACHINE of a recording),
    //the base is where its own files are
    sd.system_file = file_name;
    sd.system_path = dsk_tools::get_file_path(m_source.base_cfg);
    sd.ext_path = m_source.ext_path;
    sd.system_type = system->get_parameter("type").value;
    sd.system_name = system->get_parameter("name").value;
    sd.system_version = system->get_parameter("version", false).value;
    sd.system_charmap = system->get_parameter("charmap", false).value;
    sd.software_path = software_path;
    sd.data_path = data_path;
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

//----------------------------- Saved state ---------------------------------//

emulator::Result Emulator::save_state(const std::string &file_name)
{
    if (!loaded) return emulator::Result::error(emulator::ErrorCode::CommandFailed,
        "{Emulator|" + std::string(QT_TRANSLATE_NOOP("Emulator", "No machine is loaded")) + "}");

    StateSaveRequest request;
    request.file_name = file_name;
    request.config = &m_config;
    request.dm = dm;
    request.sd = &sd;
    request.machine = dsk_tools::get_filename(m_source.base_cfg);
    request.version = sd.system_version.empty() ? sd.system_name : sd.system_version;
    request.clock = clock_counter;

    //The positions are rebased on the one furthest behind. Kept as they are,
    //the first timer_proc() after a restore would find every domain past its
    //target, subtract it from all of them and wrap a uint64_t - and the
    //machine would never run again. Only the difference between them means
    //anything, and that is preserved exactly
    uint64_t base = 0;
    for (size_t i = 0; i < domains.size(); i++)
        if (i == 0 || domains[i].norm < base) base = domains[i].norm;
    for (size_t i = 0; i < domains.size(); i++)
        request.domains.push_back(domains[i].norm - base);

    return write_state_file(request);
}

void Emulator::request_state(const std::string &file_name)
{
    compat_lock_guard lock(m_state_mutex);
    m_state_file = file_name;
    m_state_error.clear();
    m_state_requested = true;
    m_state_serial++;
}

bool Emulator::state_pending()
{
    compat_lock_guard lock(m_state_mutex);
    return m_state_requested;
}

std::string Emulator::take_state_error()
{
    compat_lock_guard lock(m_state_mutex);
    std::string r;
    r.swap(m_state_error);
    return r;
}

//Consumed at a slice boundary, where every domain has finished a whole slice.
//A script does not come through here: its tick() already runs between two
//instructions of the emulation thread, which is a finer and repeatable moment
void Emulator::store_state()
{
    std::string file;
    uint64_t serial = 0;
    {
        compat_lock_guard lock(m_state_mutex);
        if (!m_state_requested) return;
        file = m_state_file;
        serial = m_state_serial;
    }
    const emulator::Result res = save_state(file);
    {
        compat_lock_guard lock(m_state_mutex);
        //Cleared last, so that the flag means "finished", not "started": the
        //caller waits on it and then reads the error. A request that came in
        //while this file was being written is still to be served - it keeps
        //the flag up and is taken at the next slice
        if (serial == m_state_serial) {
            if (!res) m_state_error = res.message;
            m_state_requested = false;
        }
    }
}

emulator::Result Emulator::apply_state()
{
    m_state_report.clear();
    if (!m_source.is_state || m_source.state.empty()) return emulator::Result::ok();

    EmulatorConfig state;
    emulator::Result res = state.load_from_text(m_source.state);
    if (!res) return res;

    std::string problems;

    //Pass 1: the options first. Connector::set_device_option() plugs and
    //unplugs whole devices, which decides who drives which line, and a
    //display option changes the surface. Everything below assumes the machine
    //is already wired the way the snapshot found it. This also overrides what
    //apply_saved_device_options() took from the ini: the snapshot wins
    for (unsigned int i = 0; i < state.get_devices_count(); i++)
    {
        EmulatorConfigDevice * sd_dev = state.get_device(static_cast<int>(i));
        if (sd_dev->name == "system") continue;
        ComputerDevice * d = dm->get_device_by_name(sd_dev->name, false);
        if (d == nullptr) continue;
        for (size_t k = 0; k < sd_dev->parameters.size(); k++)
        {
            const EmulatorConfigParameter &p = sd_dev->parameters[k];
            if (p.name != "option" || p.left_range.size() < 3) continue;
            try {
                const unsigned int id = parse_numeric_value(
                    p.left_range.substr(1, p.left_range.size() - 2), 10);
                d->set_device_option(id, parse_numeric_value(p.value));
            } catch (std::exception &e) {
                problems += sd_dev->name + ".option: " + e.what() + "\n";
            }
        }
    }

    //Pass 2: the devices themselves, in the order they were declared. Each
    //one restores its own lines through ComputerDevice::load_state()
    for (unsigned int i = 0; i < state.get_devices_count(); i++)
    {
        EmulatorConfigDevice * sd_dev = state.get_device(static_cast<int>(i));
        if (sd_dev->name == "system") continue;

        ComputerDevice * d = dm->get_device_by_name(sd_dev->name, false);
        if (d == nullptr)
        {
            //Not an error: the configuration is in the same file, so a
            //section with no device means the file was edited by hand
            problems += "no device named '" + sd_dev->name + "'\n";
            continue;
        }
        if (!sd_dev->type.empty() && sd_dev->type != d->type)
        {
            problems += sd_dev->name + ": state says '" + sd_dev->type
                      + "', the machine has '" + d->type + "'\n";
            continue;
        }

        std::vector<char> used(sd_dev->parameters.size(), 0);
        std::string error;
        StateReader r(sd_dev, &sd, &used, &error);
        res = d->load_state(r);
        if (!res) return res;
        if (!error.empty()) return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{MachineState|Saved state} " + error);

        for (size_t k = 0; k < used.size(); k++)
            if (!used[k] && sd_dev->parameters[k].name != "option")
                problems += sd_dev->name + "." + sd_dev->parameters[k].key() + ": unknown here\n";
    }

    //Pass 3: caches that depend on a line or on another device's buffer
    for (unsigned int i = 0; i < dm->device_count; i++)
        dm->get_device(i)->device->state_restored();

    //Pass 4: what belongs to the machine as a whole
    EmulatorConfigDevice * sys = state.get_device("system");
    if (sys != nullptr)
    {
        std::vector<char> used(sys->parameters.size(), 0);
        std::string error;
        StateReader r(sys, &sd, &used, &error);

        unsigned int radix = sd.radix;
        if (r.u("radix", radix) && (radix == 2 || radix == 8 || radix == 10 || radix == 16))
        {
            sd.radix = radix;
            set_default_radix(radix);
        }
        r.n64("clock", clock_counter);
        //Rebased by the writer, so the positions are small and the first
        //slice after the restore behaves like any other
        for (size_t k = 0; k < domains.size(); k++)
            r.n64(("domain[" + std::to_string(k) + "]").c_str(), domains[k].norm);
        if (!error.empty()) return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{MachineState|Saved state} " + error);
    }

    //The host is holding nothing at the moment a snapshot is opened. The
    //keyboard's own lists of held keys were emptied by the reset this runs
    //after, and nothing restores them: a key that comes back down would stay
    //down forever, auto repeat included. Only the machine side of a keyboard
    //- its code register and ready trigger, which are ports - is restored, so
    //a program waiting for a keystroke sees exactly what it saw
    {
        compat_lock_guard lock(m_host_keys_mutex);
        m_host_keys.clear();
    }

    m_state_report = problems;
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
                    //Written by the window, but the ini is the user's file: an
                    //entry that does not parse is skipped, not thrown
                    try {
                        dev->set_device_option(opt.id, static_cast<unsigned int>(std::stoul(saved)));
                    } catch (const std::exception &) {
                    }
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

//----------------------- Why the machine stopped ----------------------------//

void Emulator::set_machine_error(const std::string &message, bool fatal)
{
    compat_lock_guard lock(m_error_mutex);
    //The first cause is the one worth telling; a fatal one wins over a device
    //that merely stopped the machine
    if (!m_machine_error.empty() && (m_machine_fatal || !fatal)) return;
    m_machine_error = message.empty() ? std::string("unknown error") : message;
    m_machine_fatal = fatal;
}

std::string Emulator::machine_error(bool * fatal) const
{
    compat_lock_guard lock(m_error_mutex);
    if (fatal != nullptr) *fatal = m_machine_fatal;
    return m_machine_error;
}

void Emulator::clear_machine_error()
{
    compat_lock_guard lock(m_error_mutex);
    m_machine_error.clear();
    m_machine_fatal = false;
}

//--------------------- Calls from other threads ----------------------------//

void Emulator::invoke(const std::function<void()> &fn)
{
    PendingCall call(&fn);
    bool queued = false;
    {
        compat_lock_guard lock(m_calls_mutex);
        if (m_calls_open && !(m_calls_thread == compat_this_thread_id()))
        {
            m_calls.push_back(&call);
            m_calls_pending = true;
            queued = true;
        }
    }

    if (!queued)
    {
        //Nobody to race with, or this is the emulation thread asking itself
        fn();
        return;
    }

    //The emulation thread looks at the queue at least once a millisecond, so
    //this is normally over within one; a call that takes long (an image being
    //read) is not worth a core spinning for it
    unsigned int spins = 0;
    while (!call.done)
    {
        if (++spins < 5000) compat_yield(); else compat_sleep_ms(1);
    }

    if (call.failed) throw std::runtime_error(call.error);
}

void Emulator::run_pending_calls()
{
    std::vector<PendingCall*> calls;
    {
        compat_lock_guard lock(m_calls_mutex);
        calls.swap(m_calls);
        m_calls_pending = false;
    }

    for (size_t i = 0; i < calls.size(); i++)
    {
        PendingCall * c = calls[i];
        try {
            (*c->fn)();
        } catch (const std::exception &ex) {
            c->failed = true;
            c->error = ex.what();
        }
        //Its owner wakes up on this and the entry is gone with its stack frame
        c->done = true;
    }
}

void Emulator::close_calls()
{
    {
        compat_lock_guard lock(m_calls_mutex);
        m_calls_open = false;
    }
    //Whoever got in before the door closed is still waiting
    run_pending_calls();
}

void Emulator::reset(bool cold)
{
    if (!loaded || dm == nullptr) return;
    invoke([this, cold]() { dm->reset_devices(cold); });
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

bool Emulator::has_embedded_script() const
{
    return loaded && !m_source.script.empty();
}

emulator::Result Emulator::load_embedded_script()
{
    ScriptEngine * s = script_engine();

    //Named after the extension: the log goes next to it, and relative names
    //in the script resolve there first
    emulator::Result res = s->load_text(m_source.script, m_source.file, m_source.script_line);
    if (!res) return res;
    sd.script_path = s->get_path();
    m_embedded_loaded = true;
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

        //From here on the machine belongs to the emulation thread, and a call
        //from any other one waits for it - also while that thread is still
        //setting the machine up
        {
            compat_lock_guard lock(m_calls_mutex);
            m_calls_open = true;
            m_calls_thread = compat_thread_id();
        }

#if USE_QT_THREADING
        emulationThread = EmuThread::create([this]() {
#else
        emulationThread = std::thread([this]() {
#endif
            {
                compat_lock_guard lock(m_calls_mutex);
                m_calls_thread = compat_this_thread_id();
            }
            CallsGuard calls_guard(this);

            setThreadPriority(true);

            //One domain per processor, master first. load_devices_config() has
            //already put them in that order
            const std::vector<CPU*> &cpu_list = dm->get_cpus();
            if (cpu_list.empty()) {
                set_machine_error("No processor found in the configuration", true);
                m_running = false;
                return;
            }

            cpu = cpu_list[0];

            //LCM of every frequency, so that a domain's position can be counted
            //in whole units of a common time. For 8 MHz and 6.25 MHz the scales
            //come out 25 and 32 - the 32:25 interleave the УК-НЦ really has.
            //With one processor the LCM is its own clock and every scale is 1
            uint64_t lcm = cpu_list[0]->clock;
            for (size_t i = 1; i < cpu_list.size(); i++) {
                const uint64_t c = cpu_list[i]->clock;
                uint64_t a = lcm, b = c;
                while (b != 0) { const uint64_t t = a % b; a = b; b = t; }
                lcm = (a != 0) ? (lcm / a * c) : lcm;
            }

            domains.clear();
            for (size_t i = 0; i < cpu_list.size(); i++) {
                ClockDomain d;
                d.cpu = cpu_list[i];
                d.scale = (cpu_list[i]->clock != 0) ? (lcm / cpu_list[i]->clock) : 1;
                d.norm = 0;
                domains.push_back(d);
            }

            mm = cpu->mm;
            if (!mm) {
                set_machine_error("The processor has no memory mapper", true);
                m_running = false;
                return;
            }

            //Not required=true: that throws, and nothing catches here. A
            //machine without either loads fine and would end the process
            display = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display", false));
            if (!display) {
                set_machine_error("No display device named \"display\" in the configuration", true);
                m_running = false;
                return;
            }

            keyboard = dynamic_cast<Keyboard*>(dm->get_device_by_name("keyboard", false));
            if (!keyboard) {
                set_machine_error("No keyboard device named \"keyboard\" in the configuration", true);
                m_running = false;
                return;
            }

            joysticks.clear();
            std::vector<ComputerDevice*> joystick_devices = dm->find_devices_by_class("joystick");
            for (size_t i = 0; i < joystick_devices.size(); i++)
                joysticks.push_back(dynamic_cast<Joystick*>(joystick_devices[i]));

            mice.clear();
            std::vector<ComputerDevice*> mouse_devices = dm->find_devices_by_class("mouse");
            for (size_t i = 0; i < mouse_devices.size(); i++)
                mice.push_back(dynamic_cast<Mouse*>(mouse_devices[i]));

            clock_freq = this->cpu->clock;

            for (size_t i = 0; i < domains.size(); i++) domains[i].norm = 0;
            clock_counter = 0;

            //A machine opened from a .ecats: everything is built and reset,
            //nothing has been clocked yet, and this thread owns all of it -
            //which is the only moment a whole machine can be replaced at once.
            //Doing it here is also what makes every frontend support a saved
            //state without a line of its own: they all load and then run()
            //The reset and the restore run outside the loop below and its
            //catch, and an exception leaving this thread ends the process
            try {
                reset(true);

                const emulator::Result res = apply_state();
                if (!res)
                {
                    //The same way a device refusing what the guest asked is
                    //reported: the machine stands still and the message waits
                    set_machine_error(res.message, false);
                    for (size_t i = 0; i < domains.size(); i++)
                        domains[i].cpu->m_debug = DEBUG_STOPPED;
                }
            } catch (const std::exception &ex) {
                set_machine_error(ex.what(), true);
                m_running = false;
                return;
            }

            m_ready = true;

#if USE_QT_THREADING
            QElapsedTimer timer;
            timer.start();
            qint64 lastUsecs = timer.nsecsElapsed() / 1000;
            while (m_running) {
                if (m_calls_pending) run_pending_calls();

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
                //Between two slices: every instruction is finished, and what
                //another thread wants done to the machine is done here
                if (m_calls_pending) run_pending_calls();

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
                //The message is kept for the frontend: a window that simply froze,
                //a console run that waited forever for a script that could no
                //longer finish, is all this used to leave behind
                try {
                    timer_proc(time_ticks);
                } catch (const std::exception &ex) {
                    busy = false;
                    set_machine_error(ex.what(), true);
                    m_running = false;
                } catch (...) {
                    busy = false;
                    set_machine_error("unknown exception in the emulation thread", true);
                    m_running = false;
                }
            }
        });

#if USE_QT_THREADING
        renderThread = EmuThread::create([this]() {
#else
        renderThread = std::thread([this]() {
#endif
#ifdef _WIN32
            //The default timer of Windows ticks every 15.6 ms, and a sleep ends
            //on a tick: 20 ms became 31, and the screen ran at 31 frames a
            //second instead of 50 (measured). A 1 ms period for as long as the
            //render thread lives - the emulation thread spins anyway, so the
            //power this costs is already being spent
            timeBeginPeriod(1);
#endif
#if !USE_QT_THREADING
            //Frames are due at fixed moments, not 20 ms after the previous one
            //was done: the time a frame takes and the sleep overshoot no longer
            //add up into a slower rate
            auto next_frame = std::chrono::steady_clock::now();
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
                render_screen();

                //~50 FPS. A frame that came too late is not made up for with a
                //burst of quick ones: the schedule starts again from now
                next_frame += std::chrono::milliseconds(20);
                const auto now = std::chrono::steady_clock::now();
                if (next_frame < now) next_frame = now;
                std::this_thread::sleep_until(next_frame);
#endif
            }
#ifdef _WIN32
            timeEndPeriod(1);
#endif
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

    if (!busy && !domains.empty())
    {
        busy = true;

        //Before the slice: every domain has finished a whole one and its
        //position has already been rebased by the "-= target" at the end of
        //the previous pass, so the machine is as consistent as it ever gets
        store_state();

        // while (SDL_PollEvent(&ev))
        // {
        //     qDebug() << ev.type;
        // }

        //The slice arrives in the master's cycles; every domain is carried in
        //the common normalised time instead, so that processors of different
        //frequencies interleave by an exact integer ratio and a run stays
        //reproducible to the byte
        const uint64_t target = time_ticks * domains[0].scale;
        const uint64_t clock_before = clock_counter;

        while (true) {
            //Whichever processor is furthest behind goes next. With one domain
            //this is simply "has the slice been used up yet"
            size_t k = 0;
            for (size_t i = 1; i < domains.size(); i++)
                if (domains[i].norm < domains[k].norm) k = i;
            if (domains[k].norm >= target) break;

            ClockDomain &d = domains[k];
            unsigned int counter = d.cpu->execute();
            if (counter > 0) {
                d.norm += (uint64_t)counter * d.scale;

                //Emulated time of the machine is the master's time: a script
                //that waits 20 ms means 20 ms of the machine, whatever the
                //other processors are doing
                if (k == 0) clock_counter += counter;
                dm->clock((unsigned int)k, counter);

                //Scripts are advanced here, on the emulation thread, so they see
                //the devices in the same state the CPU does. Ticking inside the
                //loop rather than once per time slice makes a script resume at
                //the very same instruction on every run: the size of a slice
                //depends on the host clock, the cycle counter does not.
                if (script) script->tick(clock_counter);
            } else {
                //A stopped CPU produces no cycles, but its domain still has to
                //move or the loop would never end. The script engine still runs:
                //the commands that need no emulated time are how a debugging
                //session steps, inspects and starts the CPU again. The delays
                //stay frozen, which is the honest behaviour - they are counted
                //in emulated time and that is what has stopped
                d.norm += 10ull * d.scale;

                if (script) script->tick(clock_counter);
            }
        }
        //A device refused what the guest asked of it (an unsupported mode, a
        //command the emulator does not model). Throwing from here used to end
        //the process; the machine is stopped instead, and the message stays in
        //the device manager. Starting the CPU again resumes until the next one
        if (dm->error_pending) {
            dm->error_pending = false;
            set_machine_error((dm->error_device != nullptr ? dm->error_device->name + ": " : std::string())
                              + dm->error_message, false);
            for (size_t i = 0; i < domains.size(); i++)
                domains[i].cpu->m_debug = DEBUG_STOPPED;
        }

        //The master stood still for the whole slice: a WAITFOR or a SCREEN
        //would wait for an emulated millisecond that is not coming
        if (script && time_ticks > 0 && clock_counter == clock_before) script->tick_frozen(clock_counter);

        //What a domain overshot the slice by is owed to the next one
        for (size_t i = 0; i < domains.size(); i++) domains[i].norm -= target;

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
    //The ini is the user's own file: a value left empty or mistyped keeps the
    //default instead of throwing out of the window's slot at every start
    auto video_setting = [this](const char * ident, double def) -> double {
        try {
            return std::stod(read_setup("Video", ident, ""));
        } catch (const std::exception &) {
            return def;
        }
    };
    screen_scale = video_setting("scale", 2);
    screen_ratio = static_cast<int>(video_setting("ratio", SCREEN_RATIO_43));
    screen_filtering = static_cast<int>(video_setting("filtering", SCREEN_FILTERING_NONE));

    if (screen_ratio == SCREEN_RATIO_SQ)
        pixel_scale = 1;
    else
    if (screen_ratio == SCREEN_RATIO_43)
        pixel_scale = (4.0 / 3.0) / ((double)screen_sx / (double)screen_sy);
    else
        pixel_scale = ((double)screen_sy / (double)screen_sx);

    //Under the surface lock, exactly like the resize in render_screen(): this
    //runs on the interface thread, and with the MCP server the window is
    //created when a machine is asked for - by then the render thread may
    //already be drawing into the buffer this call is about to replace
    d->lock_surface();
    renderer->init_screen(p, screen_sx, screen_sy, screen_scale, pixel_scale);
    d->set_renderer(*renderer);
    d->unlock_surface();
    set_filtering(screen_filtering);
}

void Emulator::stop_video()
{
    //The renderer frees the surface here, so nothing may be drawing into it
    GenericDisplay * d = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display", false));
    if (d) d->lock_surface();
    renderer->stop();
    if (d) d->unlock_surface();
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

int Emulator::host_key(int key, unsigned int scan, int modifiers, bool press)
{
    {
        compat_lock_guard lock(m_host_keys_mutex);
        size_t i = 0;
        for (; i < m_host_keys.size(); i++)
            if ((scan != 0)?(m_host_keys[i].scan == scan):(m_host_keys[i].key == key)) break;

        if (press) {
            if (i == m_host_keys.size()) {
                HostKey h;
                h.scan = scan;
                h.key = key;
                m_host_keys.push_back(h);
            }
        } else if (i < m_host_keys.size()) {
            key = m_host_keys[i].key;
            m_host_keys.erase(m_host_keys.begin() + i);
        }
    }

    key_event(key, modifiers, press);
    return key;
}

void Emulator::release_host_keys()
{
    std::vector<HostKey> held;
    {
        compat_lock_guard lock(m_host_keys_mutex);
        held.swap(m_host_keys);
    }
    for (size_t i = 0; i < held.size(); i++) {
        key_event(held[i].key, 0, false);
        //A recording would otherwise keep the key down for the rest of it
        record_key(static_cast<unsigned int>(held[i].key), held[i].scan, false);
    }
}

// A key of the machine's own keyboard, pressed on the drawing of it. It carries
// an id instead of a host code, so nothing here goes through rus_translate():
// the picture already shows the machine's layout.
void Emulator::key_event_id(const std::string &id, bool press)
{
    if (!keyboard || !display) return;
    keyboard->key_event_id(id, press);
}

void Emulator::mouse_event(int dx, int dy, int buttons)
{
    if (!keyboard || !display) return;
    for (size_t i = 0; i < mice.size(); i++) mice[i]->move(dx, dy, buttons);
}

bool Emulator::has_mouse() const
{
    for (size_t i = 0; i < mice.size(); i++)
        if (mice[i]->is_plugged()) return true;
    return false;
}

void Emulator::set_volume(int value)
{
    //Every audio output of the machine, by class: the name "sound" used to be
    //reserved for it without being written down anywhere, and a device named
    //otherwise ignored the volume control
    if (loaded)
        for (ComputerDevice * d : dm->find_devices_by_class("sound")) {
            GenericSound * sound = dynamic_cast<GenericSound*>(d);
            if (sound != nullptr) sound->set_volume(value);
        }
}

void Emulator::set_muted(bool muted)
{
    if (loaded)
        for (ComputerDevice * d : dm->find_devices_by_class("sound")) {
            GenericSound * sound = dynamic_cast<GenericSound*>(d);
            if (sound != nullptr) sound->set_muted(muted);
        }
}

Emulator::~Emulator()
{
    //The devices first, as in drop_machine(): their interfaces are registered
    //in the manager
    delete dm;
    delete im;
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
    screen_filtering = filtering;
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
    register_all_devices(dm);
}

void register_all_devices(DeviceManager * dm)
{
    dm->register_device("ram", create_ram);
    dm->register_device("bus-timing", create_bus_timing);
    dm->register_device("rom", create_rom);
    dm->register_device("memory-mapper", create_memory_mapper);
    dm->register_device("port", create_port);
    dm->register_device("port-address", create_port_address);
    dm->register_device("speaker", create_speaker);
    dm->register_device("ay8910", create_ay8910);
    dm->register_device("covox", create_covox);
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
    dm->register_device("dl11", create_dl11);
    dm->register_device("i8253", create_i8253);
    dm->register_device("i8259", create_i8259);
    dm->register_device("register", create_register);
    dm->register_device("mux", create_mux);
    dm->register_device("z80", create_z80);
    dm->register_device("page-mapper", create_page_mapper);
    dm->register_device("plane-pair", create_plane_pair);
    dm->register_device("indirect-memory", create_indirect_memory);
    dm->register_device("generator", create_generator);
    dm->register_device("6502", create_mos6502);
    dm->register_device("65c02", create_wdc65c02);
    dm->register_device("1801vm1", create_k1801vm1);
    dm->register_device("1801vm2", create_k1801vm2);
    dm->register_device("i8086", create_i8086);
    dm->register_device("i8088", create_i8088);
    dm->register_device("agat-fdc140", create_agat_fdc140);
    dm->register_device("agat-fdc840", create_agat_fdc840);
    dm->register_device("agat-7-display", create_agat_7_display);
    dm->register_device("agat-9-display", create_agat_9_display);
    dm->register_device("map-keyboard", create_mapkeyboard);
    dm->register_device("joystick", create_joystick);
    dm->register_device("mouse", create_mouse);
    dm->register_device("connector", create_connector);
    dm->register_device("ram-address", create_ram_address);
    dm->register_device("irisha-display", create_irisha_display);
    dm->register_device("bk-display", create_bk_display);
    dm->register_device("poisk-display", create_poisk_display);
    dm->register_device("poisk-trap", create_poisk_trap);
    dm->register_device("poisk-vram", create_poisk_vram);
    dm->register_device("poisk-fdc-control", create_poisk_fdc_control);
    dm->register_device("bk-timer", create_bk_timer);
    dm->register_device("bk-fdc", create_bk_fdc);
    dm->register_device("agat-9-mapper", create_agat_9_mapper);
    dm->register_device("agat-yazs", create_agat_yazs);
    dm->register_device("uknc-channels", create_uknc_channels);
    dm->register_device("uknc-display", create_uknc_display);
    dm->register_device("uknc-graphics", create_uknc_graphics);
    dm->register_device("uknc-hdd", create_uknc_hdd);
    dm->register_device("smk512", create_smk512);
    dm->register_device("a16m", create_a16m);
    dm->register_device("uknc-timer", create_uknc_timer);
    dm->register_device("uknc-keyboard", create_uknc_keyboard);
    dm->register_device("uknc-sound", create_uknc_sound);
    dm->register_device("argo-keyboard", create_argo_keyboard);
    dm->register_device("argo-memory", create_argo_memory);
    dm->register_device("zx-keyboard", create_zx_keyboard);
    dm->register_device("unior-memory", create_unior_memory);
    //unior-tape - тот же самый магнитофон. Имя типа оставлено навсегда:
    //снимок состояния несет в себе разрешенную конфигурацию, и в
    //tests/files/state-unior.ecats.zip записано именно оно
    dm->register_device("unior-tape", create_tape_recorder);
}
