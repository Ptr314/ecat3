// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: WASM/Canvas renderer

#include "renderer_wasm.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include "libs/lodepng/lodepng.h"

// DOM access must happen on the main browser thread.
// init_screen() and set_filtering() are called from the main thread (via ccall).
// render() and resize() can be called from the render thread (pthread),
// so they use MAIN_THREAD_EM_ASM which proxies to the main thread.

WasmRenderer::WasmRenderer()
    : buffer(nullptr)
    , line_bytes(0)
{
}

WasmRenderer::~WasmRenderer()
{
    stop();
}

std::string WasmRenderer::get_name()
{
    return "WASM Canvas";
}

void WasmRenderer::init_screen(void *p, int sx, int sy, double ss, double ps)
{
    VideoRenderer::init_screen(p, sx, sy, ss, ps);

    std::lock_guard<std::mutex> lock(render_mutex);
    delete[] buffer;
    line_bytes = sx * 4;
    buffer = new uint8_t[sy * line_bytes];
    memset(buffer, 0, sy * line_bytes);

    // Called from main thread (via ccall), so EM_ASM is safe here
    MAIN_THREAD_EM_ASM({
        var canvas = document.getElementById('canvas');
        if (canvas) {
            canvas.width = $0;
            canvas.height = $1;
        }
    }, sx, sy);
}

void WasmRenderer::stop()
{
    std::lock_guard<std::mutex> lock(render_mutex);
    delete[] buffer;
    buffer = nullptr;
}

void WasmRenderer::set_filtering(int value)
{
    MAIN_THREAD_EM_ASM({
        var canvas = document.getElementById('canvas');
        if (canvas) {
            var ctx = canvas.getContext('2d');
            ctx.imageSmoothingEnabled = ($0 > 0);
        }
    }, value);
}

uint8_t* WasmRenderer::get_buffer()
{
    return buffer;
}

int WasmRenderer::get_line_bytes()
{
    return line_bytes;
}

void WasmRenderer::fill(uint32_t c)
{
    std::lock_guard<std::mutex> lock(render_mutex);
    if (!buffer) return;
    uint32_t* pixels = reinterpret_cast<uint32_t*>(buffer);
    int count = screen_x * screen_y;
    for (int i = 0; i < count; i++)
        pixels[i] = c;
}

void WasmRenderer::resize(int sx, int sy, double ss, double ps)
{
    std::lock_guard<std::mutex> lock(render_mutex);

    screen_x = sx;
    screen_y = sy;
    screen_ss = ss;
    screen_ps = ps;

    delete[] buffer;
    line_bytes = sx * 4;
    buffer = new uint8_t[sy * line_bytes];
    memset(buffer, 0, sy * line_bytes);

    // May be called from render thread, so use MAIN_THREAD_EM_ASM
    MAIN_THREAD_EM_ASM({
        var canvas = document.getElementById('canvas');
        if (canvas) {
            canvas.width = $0;
            canvas.height = $1;
        }
    }, sx, sy);
}

void WasmRenderer::render()
{
    std::lock_guard<std::mutex> lock(render_mutex);
    if (!buffer || screen_x <= 0 || screen_y <= 0) return;

    // Proxy canvas update to main thread
    // The buffer is RGBA (R at lowest byte on little-endian), matching Canvas ImageData
    MAIN_THREAD_EM_ASM({
        var canvas = document.getElementById('canvas');
        if (!canvas) return;
        var ctx = canvas.getContext('2d');
        var w = $0;
        var h = $1;
        var ptr = $2;
        var len = w * h * 4;
        var imgData = ctx.createImageData(w, h);
        imgData.data.set(HEAPU8.subarray(ptr, ptr + len));
        ctx.putImageData(imgData, 0, 0);
    }, screen_x, screen_y, buffer);
}

// On little-endian WASM: uint32 bytes in memory are [byte0, byte1, byte2, byte3]
// Canvas ImageData expects [R, G, B, A] byte order
// So we store R in byte0, G in byte1, B in byte2, A=0xFF in byte3
uint32_t WasmRenderer::MapRGB(uint8_t R, uint8_t G, uint8_t B)
{
    return static_cast<uint32_t>(R)
         | (static_cast<uint32_t>(G) << 8)
         | (static_cast<uint32_t>(B) << 16)
         | 0xFF000000u;
}

std::vector<uint8_t> WasmRenderer::get_screenshot()
{
    std::lock_guard<std::mutex> lock(render_mutex);
    std::vector<uint8_t> png;
    if (buffer && screen_x > 0 && screen_y > 0) {
        lodepng::encode(png, buffer, screen_x, screen_y);
    }
    return png;
}
