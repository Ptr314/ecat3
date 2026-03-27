// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: WASM/Canvas renderer

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>

#include "emulator/renderer.h"

class WasmRenderer : public VideoRenderer
{
private:
    uint8_t* buffer;
    int line_bytes;
    std::mutex render_mutex;

public:
    WasmRenderer();
    ~WasmRenderer();

    std::string get_name() override;
    void init_screen(void *p, int sx, int sy, double ss, double ps) override;
    void stop() override;
    void set_filtering(int value) override;
    uint8_t* get_buffer() override;
    int get_line_bytes() override;
    void fill(uint32_t c) override;
    void resize(int sx, int sy, double ss, double ps) override;
    void render() override;
    uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) override;
    std::vector<uint8_t> get_screenshot() override;
};
