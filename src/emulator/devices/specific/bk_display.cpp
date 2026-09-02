// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК display controller device

#include <cstring>

#include "emulator/utils.h"
#include "bk_display.h"

// The БК0010-01 has a fixed four colour palette. A pixel value of 0 is the
// background, the rest select one of three fixed colours.
uint8_t BK_Colors_4[4][3] = {
    {  0,   0,   0},    // Black
    {  0,   0, 255},    // Blue
    {  0, 255,   0},    // Green
    {255,   0,   0}     // Red
};

uint32_t BK_RGBA4[4];

// Monochrome output is the same memory read as one bit per pixel
uint8_t BK_Mono_2[2][3] = {
    {  0,   0,   0},    // Black
    {255, 255, 255}     // White
};

uint32_t BK_RGBA2[2];

BKDisplay::BKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd)
    , m_scroll_base(0330)
    , m_scroll(0330)
    , m_offset(0)
    , m_line_bytes(64)
    , m_lines(256)
    , m_color(true)
{
    // Video memory holds 256 lines of 64 bytes. Read as one bit per pixel that
    // is 512 dots across, read as two bits per pixel it is 256.
    sy = m_lines;
    sx = m_color? 256 : 512;
}

emulator::Result BKDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    vram = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("vram").value));

    // The scroll register is optional; without it the screen never scrolls
    std::string scroll_name = read_confg_value(cd, "scroll", false, std::string(""));
    if (!scroll_name.empty())
        port_scroll = dynamic_cast<Port*>(im->dm->get_device_by_name(scroll_name));

    m_scroll_base = read_confg_value(cd, "scroll_base", false, (unsigned int)0330);
    m_line_bytes = read_confg_value(cd, "line_bytes", false, (unsigned int)64);
    m_lines = read_confg_value(cd, "lines", false, (unsigned int)256);

    // Colour or monochrome is a switch on the case of a real БК rather than a
    // register, so it starts from the configuration and is changed from the
    // toolbar afterwards.
    m_color = str_tolower(read_confg_value(cd, "mode", false, std::string("color"))) != "mono";

    sy = m_lines;
    sx = m_color? (m_line_bytes * 4) : (m_line_bytes * 8);

    vram->set_memory_callback(this, 1, MODE_W);

    return emulator::Result::ok();
}

void BKDisplay::memory_callback(MAYBE_UNUSED unsigned int callback_id, MAYBE_UNUSED unsigned int address)
{
    screen_valid = false;
    was_updated = true;
}

void BKDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

DeviceOptions BKDisplay::get_device_options()
{
    return {
        {
            BK_OPTION_COLORS, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Output type"), "kscreensaver.png",
            {
                {BK_COLOR_ON,  QT_TRANSLATE_NOOP("DeviceOptions", "Color")},
                {BK_COLOR_OFF, QT_TRANSLATE_NOOP("DeviceOptions", "Monochrome")}
            }
        }
    };
}

void BKDisplay::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id != BK_OPTION_COLORS) return;

    bool color = (value_id == BK_COLOR_ON);
    if (color == m_color) return;

    m_color = color;
    // The render thread notices the new width and resizes the surface itself
    sx = m_color? (m_line_bytes * 4) : (m_line_bytes * 8);
    screen_valid = false;
    was_updated = true;
}

void BKDisplay::clock(MAYBE_UNUSED unsigned int counter)
{
    if (port_scroll == nullptr) return;

    unsigned scroll = port_scroll->get_direct(0);
    if (scroll == m_scroll) return;

    m_scroll = scroll;
    // Only the low byte of the register matters: it names the video line shown
    // at the top of the screen, counted from the base value. The firmware keeps
    // adding to the register without masking it, so the upper bits are just an
    // unused carry and the subtraction below has to come before the wrap.
    m_offset = (scroll - m_scroll_base) % m_lines;
    screen_valid = false;
    was_updated = true;
}

void BKDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(BK_Colors_4, BK_RGBA4, 4);
    vr.FillRGB(BK_Mono_2, BK_RGBA2, 2);
}

void BKDisplay::render_all(const bool force_render)
{
    if (screen_valid && !force_render) return;

    for (unsigned line = 0; line < m_lines; line++) {
        if (m_color) render_line_color(line);
        else render_line_mono(line);
    }

    screen_valid = true;
    was_updated = true;
}

void BKDisplay::render_line_mono(const unsigned line) const
{
    const unsigned src = ((line + m_offset) % m_lines) * m_line_bytes;
    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;

    for (unsigned i = 0; i < m_line_bytes; i++) {
        const uint8_t b = vram->get_direct(src + i);
        // Bit 0 is the leftmost dot
        for (unsigned k = 0; k < 8; k++)
            *reinterpret_cast<uint32_t*>(base + (i * 8 + k) * 4) = BK_RGBA2[(b >> k) & 1];
    }
}

void BKDisplay::render_line_color(const unsigned line) const
{
    const unsigned src = ((line + m_offset) % m_lines) * m_line_bytes;
    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;

    for (unsigned i = 0; i < m_line_bytes; i++) {
        const uint8_t b = vram->get_direct(src + i);
        // The lowest pair of bits is the leftmost dot
        for (unsigned k = 0; k < 4; k++)
            *reinterpret_cast<uint32_t*>(base + (i * 4 + k) * 4) = BK_RGBA4[(b >> (k * 2)) & 3];
    }
}

ComputerDevice * create_bk_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new BKDisplay(im, cd);
}
