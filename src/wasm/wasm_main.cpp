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

#include "emulator/emulator.h"
#include "emulator/devices/common/fdd.h"
#include "renderer_wasm.h"

static Emulator* emulator = nullptr;
static WasmRenderer* renderer = nullptr;

static const std::string WORK_PATH = "/computers/";
static const std::string DATA_PATH = "/data/";
static const std::string SOFTWARE_PATH = "/software/";
static const std::string INI_FILE = "/ecat.ini";

int main()
{
    printf("eCat3 WASM: initializing...\n");

    renderer = new WasmRenderer();
    emulator = new Emulator(WORK_PATH, DATA_PATH, SOFTWARE_PATH, INI_FILE, renderer);

    printf("eCat3 WASM: ready. Waiting for machine selection.\n");

    // With pthreads, main() returns and the browser event loop continues.
    // Emulation/render threads are spawned when wasm_load_machine() is called.
    return 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int wasm_load_machine(const char* cfg_path)
{
    if (!emulator || !renderer) return -1;

    printf("eCat3 WASM: loading machine: %s\n", cfg_path);

    // Stop current emulation if running
    emulator->stop_emulation();

    // Load new configuration
    emulator::Result res = emulator->load_config(std::string(cfg_path));
    if (!res) {
        printf("eCat3 WASM: load_config failed: %s\n", res.message.c_str());
        return -2;
    }

    // Initialize video (nullptr for widget pointer - not used in WASM renderer)
    emulator->init_video(nullptr);

    // Start emulation
    emulator->run();

    printf("eCat3 WASM: machine loaded and running.\n");
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void wasm_key_event(int key, int modifiers, int press)
{
    if (emulator) {
        emulator->key_event(key, modifiers, press != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_reset(int cold)
{
    if (emulator) {
        emulator->reset(cold != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_volume(int value)
{
    if (emulator) {
        emulator->set_volume(value);
    }
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_muted(int muted)
{
    if (emulator) {
        emulator->set_muted(muted != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_screen_width()
{
    unsigned int sx = 0, sy = 0;
    if (emulator) {
        emulator->get_screen_constraints(&sx, &sy);
    }
    return static_cast<int>(sx);
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_screen_height()
{
    unsigned int sx = 0, sy = 0;
    if (emulator) {
        emulator->get_screen_constraints(&sx, &sy);
    }
    return static_cast<int>(sy);
}

EMSCRIPTEN_KEEPALIVE
int wasm_load_file(const char* device_name, const uint8_t* data, int size, const char* filename)
{
    if (!emulator) return -1;

    // Write the file data to the Emscripten virtual filesystem
    std::string temp_dir = "/tmp/";
    std::string temp_path = temp_dir + filename;

    FILE* f = fopen(temp_path.c_str(), "wb");
    if (!f) {
        printf("eCat3 WASM: failed to write temp file: %s\n", temp_path.c_str());
        return -1;
    }
    fwrite(data, 1, size, f);
    fclose(f);

    // Find the FDD device and load the image
    ComputerDevice* dev = emulator->dm->get_device_by_name(std::string(device_name), false);
    if (!dev) {
        printf("eCat3 WASM: device '%s' not found\n", device_name);
        return -2;
    }

    FDD* fdd = dynamic_cast<FDD*>(dev);
    if (!fdd) {
        printf("eCat3 WASM: device '%s' is not an FDD\n", device_name);
        return -3;
    }

    emulator::Result res = fdd->load_image(temp_path);
    if (!res) {
        printf("eCat3 WASM: load_image failed: %s\n", res.message.c_str());
        return -4;
    }

    printf("eCat3 WASM: loaded disk image '%s' into %s\n", filename, device_name);
    return 0;
}

} // extern "C"
