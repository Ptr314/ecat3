// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Renderer without a screen, for the console build

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/renderer.h"
#include "emulator/thread_compat.h"

// Keeps the frame in memory and shows it to nobody. Everything the emulator
// needs from a renderer is the buffer the display device draws into, so a
// console build gets working screenshots without a window.
class NullRenderer : public VideoRenderer
{
public:
    NullRenderer();
    ~NullRenderer() override;

    std::string get_name() override;
    void init_screen(void *p, int sx, int sy, double ss, double ps) override;
    void stop() override;
    void set_filtering(int value) override;
    uint8_t * get_buffer() override;
    int get_line_bytes() override;
    void fill(uint32_t c) override;
    void resize(int sx, int sy, double ss, double ps) override;
    void render() override;
    uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) override;
    std::vector<uint8_t> get_screenshot() override;

private:
    uint8_t *    m_buffer;
    int          m_line_bytes;
    compat_mutex m_mutex;

    void allocate(int sx, int sy);
};
