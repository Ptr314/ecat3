// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Renderer without a screen, for the console build

#include <cstring>

#include "headless/renderer_null.h"

NullRenderer::NullRenderer():
      m_buffer(nullptr)
    , m_line_bytes(0)
{
}

NullRenderer::~NullRenderer()
{
    stop();
}

std::string NullRenderer::get_name()
{
    return "Null";
}

void NullRenderer::allocate(int sx, int sy)
{
    delete[] m_buffer;
    m_buffer = nullptr;
    m_line_bytes = 0;
    if (sx <= 0 || sy <= 0) return;

    //GenericDisplay::validate() checks sx * 4 <= line_bytes and silently draws
    //nothing when it does not hold, so this has to be exact
    m_line_bytes = sx * 4;
    m_buffer = new uint8_t[static_cast<size_t>(sy) * m_line_bytes];
    memset(m_buffer, 0, static_cast<size_t>(sy) * m_line_bytes);
}

void NullRenderer::init_screen(void *p, int sx, int sy, double ss, double ps)
{
    VideoRenderer::init_screen(p, sx, sy, ss, ps);
    compat_lock_guard lock(m_mutex);
    allocate(sx, sy);
}

void NullRenderer::stop()
{
    compat_lock_guard lock(m_mutex);
    delete[] m_buffer;
    m_buffer = nullptr;
    m_line_bytes = 0;
}

void NullRenderer::set_filtering(int /*value*/)
{
}

uint8_t * NullRenderer::get_buffer()
{
    return m_buffer;
}

int NullRenderer::get_line_bytes()
{
    return m_line_bytes;
}

void NullRenderer::fill(uint32_t c)
{
    compat_lock_guard lock(m_mutex);
    if (m_buffer == nullptr) return;
    uint32_t * pixels = reinterpret_cast<uint32_t *>(m_buffer);
    const int count = screen_x * screen_y;
    for (int i = 0; i < count; i++) pixels[i] = c;
}

void NullRenderer::resize(int sx, int sy, double ss, double ps)
{
    compat_lock_guard lock(m_mutex);
    screen_x = sx;
    screen_y = sy;
    screen_ss = ss;
    screen_ps = ps;
    allocate(sx, sy);
}

void NullRenderer::render()
{
    //Nothing to show it on
}

uint32_t NullRenderer::MapRGB(uint8_t R, uint8_t G, uint8_t B)
{
    //Stored so that the bytes in memory are already R, G, B, A on a little
    //endian host. The Qt renderers keep the reversed order of QImage and undo
    //it in get_screenshot(); the result of both is the same byte stream, which
    //is what keeps the screenshots of a console run comparable with the
    //references recorded by a windowed one
    return static_cast<uint32_t>(R)
         | (static_cast<uint32_t>(G) << 8)
         | (static_cast<uint32_t>(B) << 16)
         | 0xFF000000u;
}

std::vector<uint8_t> NullRenderer::get_screenshot()
{
    compat_lock_guard lock(m_mutex);
    std::vector<uint8_t> image;
    if (m_buffer == nullptr || screen_x <= 0 || screen_y <= 0) return image;

    //Raw RGBA, not PNG: Emulator::store_screenshot() expects sx * sy * 4 bytes
    //and encodes them itself
    const size_t size = static_cast<size_t>(screen_x) * screen_y * 4;
    image.insert(image.end(), m_buffer, m_buffer + size);
    return image;
}
