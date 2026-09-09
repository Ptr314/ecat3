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

EMSCRIPTEN_KEEPALIVE
int wasm_load_file(const char* device_name, const char* file_path)
{
    if (!g_emulator) return -1;

    // Find the FDD device and load the image from the virtual FS path
    ComputerDevice* dev = g_emulator->dm->get_device_by_name(std::string(device_name), false);
    if (!dev) {
        printf("eCat3 WASM: device '%s' not found\n", device_name);
        return -2;
    }

    FDD* fdd = dynamic_cast<FDD*>(dev);
    if (!fdd) {
        printf("eCat3 WASM: device '%s' is not an FDD\n", device_name);
        return -3;
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