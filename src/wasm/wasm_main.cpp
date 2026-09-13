// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: WASM entry point and C API bridge

#include <emscripten.h>
#include <emscripten/threading.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <stdexcept>

#include "emulator/emulator.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/tape.h"
#include "dsk_tools/dsk_tools.h"

#include <iterator>

// The same markdown renderer and settings the desktop chooser uses, see
// md2html() in qt_utils.cpp
#define MD4C_USE_UTF8
#include "libs/md4c/md4c-html.h"
#include "renderer_wasm.h"

static Emulator* g_emulator = nullptr;
static WasmRenderer* g_renderer = nullptr;

static const std::string WORK_PATH = "/computers/";
static const std::string DATA_PATH = "/data/";
static const std::string SOFTWARE_PATH = "/software/";
static const std::string INI_FILE = "/ecat.ini";

int main()
{
    printf("eCat3 WASM: initializing...\n");

    g_renderer = new WasmRenderer();
    g_emulator = new Emulator(WORK_PATH, DATA_PATH, SOFTWARE_PATH, INI_FILE, g_renderer);

    printf("eCat3 WASM: ready. Waiting for machine selection.\n");

    // With pthreads, main() returns and the browser event loop continues.
    // Emulation/render threads are spawned when wasm_load_machine() is called.
    return 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int wasm_load_machine(const char* cfg_path)
{
    if (!g_emulator || !g_renderer) return -1;

    printf("eCat3 WASM: loading machine: %s\n", cfg_path);

    try {
        // Stop current emulation if running
        g_emulator->stop_emulation();

        // Load new configuration
        emulator::Result res = g_emulator->load_config(std::string(cfg_path));
        if (!res) {
            printf("eCat3 WASM: load_config failed: %s\n", res.message.c_str());
            return -2;
        }

        // A tape follows the motor line of the machine the way it does while
        // the desktop recorder window is open - on the page the recorder is
        // always in view. Its own sound gets the level that window gives it.
        // The callback runs on the emulation thread, the one that clocks the tape.
        for (ComputerDevice * dev : g_emulator->dm->find_devices_by_class("tape")) {
            TapeRecorder * tape = dynamic_cast<TapeRecorder*>(dev);
            if (tape == nullptr) continue;
            tape->on_mode_changed = [tape](unsigned int mode) {
                if (mode == TAPE_READ) tape->play(); else tape->stop();
            };
            tape->volume(10);
        }

        // Initialize video (nullptr for widget pointer - not used in WASM renderer)
        g_emulator->init_video(nullptr);

        // Start emulation
        g_emulator->run();

        printf("eCat3 WASM: machine loaded and running.\n");
        return 0;
    } catch (const std::exception& e) {
        printf("eCat3 WASM: C++ exception: %s\n", e.what());
        return -10;
    } catch (...) {
        printf("eCat3 WASM: unknown C++ exception\n");
        return -11;
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_key_event(int key, int modifiers, int press)
{
    if (g_emulator && g_emulator->loaded) {
        g_emulator->key_event(key, modifiers, press != 0);
    }
}

// The on-screen keyboard speaks the machine's own key names, not host codes,
// so this path skips the ЙЦУКЕН -> ЯВЕРТЬ remap entirely.
EMSCRIPTEN_KEEPALIVE
void wasm_key_event_id(const char* id, int press)
{
    if (g_emulator && g_emulator->loaded && id != nullptr) {
        g_emulator->key_event_id(std::string(id), press != 0);
    }
}

// ccall(..., "string") copies the result at once, so one static buffer is safe
static Keyboard * wasm_keyboard()
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return nullptr;
    return dynamic_cast<Keyboard*>(g_emulator->dm->get_device_by_name("keyboard", false));
}

EMSCRIPTEN_KEEPALIVE
const char* wasm_keyboard_picture()
{
    static std::string result;
    Keyboard * k = wasm_keyboard();
    result = (k != nullptr) ? k->picture_file() : "";
    return result.c_str();
}

// 0 hold, 1 toggle, 2 tap -- see Keyboard::ClickMode. The policy belongs to the
// core so that the page and the desktop window treat a modifier the same way.
EMSCRIPTEN_KEEPALIVE
int wasm_key_click_mode(const char* id)
{
    Keyboard * k = wasm_keyboard();
    if (k == nullptr || id == nullptr) return 0;
    return static_cast<int>(k->click_mode(std::string(id)));
}

// The keys this machine actually has, so the page can light up and wire only
// those, the way the desktop window does.
EMSCRIPTEN_KEEPALIVE
const char* wasm_key_ids()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<std::string> &ids = k->key_ids();
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) result += ",";
            result += ids[i];
        }
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
const char* wasm_keys_pressed()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<std::string> held = k->ids_held();
        for (size_t i = 0; i < held.size(); i++) {
            if (i > 0) result += ",";
            result += held[i];
        }
    }
    return result.c_str();
}

// Indicator lamps that are not lit, so the page can black them out. The lit one
// is left exactly as the drawing has it, which is the whole point of a lamp.
EMSCRIPTEN_KEEPALIVE
const char* wasm_leds_dark()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<Keyboard::Indicator> leds = k->indicators();
        for (size_t i = 0; i < leds.size(); i++)
            if (!leds[i].lit) {
                if (!result.empty()) result += ",";
                result += leds[i].id;
            }
    }
    return result.c_str();
}

// Grows by one on every reset of the keyboard. The panel latches modifiers on
// its own, so it watches this and starts over: after a reset the machine holds
// nothing, and a panel still showing УПР latched would send control codes.
EMSCRIPTEN_KEEPALIVE
int wasm_kbd_reset_count()
{
    Keyboard * k = wasm_keyboard();
    return (k != nullptr) ? static_cast<int>(k->reset_count()) : 0;
}

// "led_rus=key_rus,led_lat=key_lat": which key each lamp makes redundant. Read
// once, when the drawing is put into the page: a key whose lamp is there stops
// being highlighted, so the alphabet is shown in one place, not two.
EMSCRIPTEN_KEEPALIVE
const char* wasm_led_keys()
{
    static std::string result;
    result.clear();
    Keyboard * k = wasm_keyboard();
    if (k != nullptr) {
        const std::vector<Keyboard::Indicator> leds = k->indicators();
        for (size_t i = 0; i < leds.size(); i++) {
            if (leds[i].key.empty()) continue;
            if (!result.empty()) result += ",";
            result += leds[i].id + "=" + leds[i].key;
        }
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
void wasm_reset(int cold)
{
    if (g_emulator) {
        g_emulator->reset(cold != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_volume(int value)
{
    if (g_emulator) {
        g_emulator->set_volume(value);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_muted(int muted)
{
    if (g_emulator) {
        g_emulator->set_muted(muted != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_screen_width()
{
    unsigned int sx = 0, sy = 0;
    if (g_emulator) {
        g_emulator->get_screen_constraints(&sx, &sy);
    }
    return static_cast<int>(sx);
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_screen_height()
{
    unsigned int sx = 0, sy = 0;
    if (g_emulator) {
        g_emulator->get_screen_constraints(&sx, &sy);
    }
    return static_cast<int>(sy);
}

static void md_append(const MD_CHAR* text, MD_SIZE size, void* result)
{
    static_cast<std::string*>(result)->append(text, size);
}

// A markdown file of the virtual FS as HTML, rendered the way the desktop
// chooser renders a machine description: md4c with tables. Empty when the
// file is not there.
EMSCRIPTEN_KEEPALIVE
const char* wasm_md2html(const char* file_path)
{
    static std::string result;
    result.clear();
    if (file_path == nullptr) return result.c_str();

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return result.c_str();
    const std::string md((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    md_html(md.c_str(), static_cast<MD_SIZE>(md.size()), md_append, &result, MD_FLAG_TABLES, 0);
    return result.c_str();
}

// The device options of the machine, the ones the desktop puts on its toolbar:
// one line per dropdown, the fields separated by tabs:
//   device  option_id  title  value_id  value_title  [value_id  value_title ...]
// The titles are the untranslated source strings; the page translates them.
EMSCRIPTEN_KEEPALIVE
const char* wasm_device_options()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    for (unsigned int i = 0; i < g_emulator->dm->device_count; i++) {
        ComputerDevice * dev = g_emulator->dm->get_device(i)->device.get();
        DeviceOptions options = dev->get_device_options();
        for (size_t j = 0; j < options.size(); j++) {
            const DeviceOption & opt = options[j];
            if (opt.type != DEVICE_OPTION_DROPDOWN || opt.values.empty()) continue;
            result += dev->name + "\t" + std::to_string(opt.id) + "\t" + opt.title;
            for (size_t v = 0; v < opt.values.size(); v++)
                result += "\t" + std::to_string(opt.values[v].id) + "\t" + opt.values[v].title;
            result += "\n";
        }
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
int wasm_set_device_option(const char* device_name, int option_id, int value_id)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || device_name == nullptr) return -1;
    ComputerDevice * dev = g_emulator->dm->get_device_by_name(std::string(device_name), false);
    if (dev == nullptr) return -2;
    dev->set_device_option(static_cast<unsigned>(option_id), static_cast<unsigned>(value_id));
    return 0;
}

// A tape recorder by its device name, or nullptr when there is no such device
static TapeRecorder * wasm_tape(const char * name)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || name == nullptr) return nullptr;
    return dynamic_cast<TapeRecorder*>(g_emulator->dm->get_device_by_name(std::string(name), false));
}

// One line per tape recorder, the fields separated by tabs:
//   name  mode  position  total  recorded  record_name  files
// mode is TAPE_STOPPED or TAPE_READ, position and total are in seconds,
// recorded is the size of what the machine has written so far.
EMSCRIPTEN_KEEPALIVE
const char* wasm_tape_info()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    for (ComputerDevice * dev : g_emulator->dm->find_devices_by_class("tape")) {
        TapeRecorder * tape = dynamic_cast<TapeRecorder*>(dev);
        if (tape == nullptr) continue;
        result += tape->name + "\t"
                + std::to_string(tape->get_mode()) + "\t"
                + std::to_string(tape->get_position()) + "\t"
                + std::to_string(tape->get_total()) + "\t"
                + std::to_string(tape->get_record_size()) + "\t"
                + tape->get_record_name() + "\t"
                + tape->files + "\n";
    }
    return result.c_str();
}

// Puts a file of the virtual FS on the tape. How it goes there is the
// [TapeFiles] entry for its extension, a machine specific one winning over the
// generic one, exactly as in the desktop recorder window.
EMSCRIPTEN_KEEPALIVE
int wasm_tape_load(const char* device_name, const char* file_path)
{
    TapeRecorder * tape = wasm_tape(device_name);
    if (tape == nullptr || file_path == nullptr) return -2;

    std::string ext = dsk_tools::get_file_ext(file_path);
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    if (ext.empty()) return -3;

    SystemData * sd = g_emulator->get_system_data();
    std::string fmt = g_emulator->read_setup("TapeFiles", sd->system_type + "." + ext, "");
    if (fmt.empty()) fmt = g_emulator->read_setup("TapeFiles", ext, "");
    if (fmt.empty()) return -3;

    emulator::Result res = tape->load_file(std::string(file_path), fmt);
    if (!res) {
        printf("eCat3 WASM: tape load_file failed: %s\n", res.message.c_str());
        return -4;
    }
    return 0;
}

// 0 play, 1 stop, 2 rewind, 3 recording (value 0/1), 4 mute (value 0/1)
EMSCRIPTEN_KEEPALIVE
int wasm_tape_control(const char* device_name, int action, int value)
{
    TapeRecorder * tape = wasm_tape(device_name);
    if (tape == nullptr) return -2;
    switch (action) {
        case 0: tape->play(); break;
        case 1: tape->stop(); break;
        case 2: tape->rewind(); break;
        case 3: tape->set_recording(value != 0); break;
        case 4: tape->mute(value != 0); break;
        default: return -3;
    }
    return 0;
}

// Writes what the machine has recorded to a file of the virtual FS, for the
// page to hand to the browser. Answers the size, 0 when nothing was recorded.
EMSCRIPTEN_KEEPALIVE
int wasm_tape_save(const char* device_name, const char* file_path)
{
    TapeRecorder * tape = wasm_tape(device_name);
    if (tape == nullptr || file_path == nullptr) return -2;

    const std::vector<uint8_t> * data = tape->get_record_data();
    if (data->empty()) return 0;

    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) return -4;
    file.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
    return static_cast<int>(data->size());
}

// A drive by its device name, or nullptr when there is no such drive
static FDD * wasm_fdd(const char * name)
{
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr || name == nullptr) return nullptr;
    return dynamic_cast<FDD*>(g_emulator->dm->get_device_by_name(std::string(name), false));
}

// One line per drive of the machine, the fields separated by tabs, because the
// file filters themselves carry '|', ';' and spaces:
//   name  loaded  protected  led  file_name  files  files_save
// The page builds a block per line and polls this for the lamp and the disk.
EMSCRIPTEN_KEEPALIVE
const char* wasm_fdd_info()
{
    static std::string result;
    result.clear();
    if (!g_emulator || !g_emulator->loaded || g_emulator->dm == nullptr) return result.c_str();

    std::vector<ComputerDevice*> devices = g_emulator->dm->find_devices_by_class("fdd");
    for (size_t i = 0; i < devices.size(); i++) {
        FDD * fdd = dynamic_cast<FDD*>(devices[i]);
        if (fdd == nullptr) continue;
        std::string file = fdd->get_loaded() ? fdd->file_name : "";
        for (size_t j = 0; j < file.size(); j++)
            if (file[j] == '\t' || file[j] == '\n') file[j] = ' ';
        result += fdd->name + "\t"
                + (fdd->get_loaded() ? "1" : "0") + "\t"
                + (fdd->is_protected() ? "1" : "0") + "\t"
                + (fdd->is_led_on() ? "1" : "0") + "\t"
                + file + "\t"
                + fdd->files + "\t"
                + fdd->files_save + "\n";
    }
    return result.c_str();
}

EMSCRIPTEN_KEEPALIVE
int wasm_fdd_eject(const char* device_name)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr) return -2;
    fdd->unload();
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int wasm_fdd_protect(const char* device_name, int on)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr) return -2;
    if (fdd->is_protected() != (on != 0)) fdd->change_protection();
    return 0;
}

// The image goes to a file of the virtual FS; its extension picks the format,
// exactly as on the desktop. The page reads it back and hands it to the browser.
EMSCRIPTEN_KEEPALIVE
int wasm_fdd_save(const char* device_name, const char* file_path)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr || file_path == nullptr) return -2;
    if (!fdd->get_loaded()) return -3;

    emulator::Result res = fdd->save_image(std::string(file_path));
    if (!res) {
        printf("eCat3 WASM: save_image failed: %s\n", res.message.c_str());
        return -4;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int wasm_load_file(const char* device_name, const char* file_path)
{
    FDD * fdd = wasm_fdd(device_name);
    if (fdd == nullptr || file_path == nullptr) {
        printf("eCat3 WASM: drive '%s' not found\n", device_name ? device_name : "");
        return -2;
    }

    emulator::Result res = fdd->load_image(std::string(file_path));
    if (!res) {
        printf("eCat3 WASM: load_image failed: %s\n", res.message.c_str());
        return -4;
    }

    printf("eCat3 WASM: loaded disk image '%s' into %s\n", file_path, device_name);
    return 0;
}

} // extern "C"