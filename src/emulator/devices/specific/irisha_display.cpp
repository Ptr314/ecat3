// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Irisha display controller device

#include <algorithm>
#include <cstring>

#include "emulator/utils.h"
#include "irisha_display.h"

uint8_t Irisha_Back_4[4][3] =  {
    {0, 0, 0},          // Black
    {0, 0, 255},        // Blue
    {0, 255, 0},        // Green
    {255, 0, 255}       // Purple
};

uint32_t Irisha_RGBA4[4];

uint8_t Irisha_Pallette1_4[4][3] =  {
    {  0,   0, 0},      // Background / Not used
    {  0, 128, 0},      // Green
    {128,   0, 0},      // Red
    {128, 128, 0}       // Brown
};

uint32_t Irisha_RGBA4_Pal1[4];

uint8_t Irisha_Pallette2_4[4][3] =  {
    {  0,   0,   0},     // Background / Not used
    {  0, 128, 128},     // Cyan
    {128,   0, 128},     // Purple
    {255, 255, 255}      // White
};

uint32_t Irisha_RGBA4_Pal2[4];

uint8_t Irisha_Color_8[8][3] =  {
    {255, 255, 255},    // White
    {  0,   0, 255},    // Blue
    {  0, 255,   0},    // Green
    {  0, 255, 255},    // Cyan
    {255,   0,   0},    // Red
    {255,   0, 255},    // Purple
    {255, 255,   0},    // Yellow
    {255, 255, 255}     // White
};

uint32_t Irisha_RGBA8[8];

uint8_t Irisha_Back_16[16][3] =  {
    {  0,   0,   0},    // Black
    {  0,   0, 128},    // Blue
    {  0, 128,   0},    // Green
    {  0, 128, 128},    // Cyan
    {128,   0,   0},    // Red
    {128,   0, 128},    // Purple
    {128, 128,   0},    // Brown
    {128, 128, 128},    // Gray
    {140, 140, 140},    // Light Gray // TODO: check if it's correct
    {  0,   0, 255},    // Light Blue
    {  0, 255,   0},    // Light Green
    {  0, 255, 255},    // Light Cyan
    {255,   0,   0},    // Light Red
    {255,   0, 255},    // Light Purple
    {255, 255,   0},    // Yellow
    {255, 255, 255}     // White

};

uint32_t Irisha_RGBA16[16];

IrishaDisplay::IrishaDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd),
    m_mode(0),
    m_color(0),
    m_page(0),
    m_mode_index(0),
    m_page_size(0)
{
    sx = 320;
    sy = 240;
}

emulator::Result IrishaDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    port_mode  = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    port_color = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("color").value));
    port_page  = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("page").value));
    vram =  dynamic_cast<RAM*> (im->dm->get_device_by_name(cd->get_parameter("vram").value));

    vram->set_memory_callback(this, 1, MODE_W);

    return emulator::Result::ok();
}

void IrishaDisplay::memory_callback(unsigned int callback_id, unsigned int address)
{
    // TODO: validate current page only
    screen_valid = false;
    was_updated = true;
}

void IrishaDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    // TODO: change to std::pair
    *sx = this->sx;
    *sy = this->sy;
}

void IrishaDisplay::clock(unsigned int counter)
{
    if ( (m_mode != port_mode->get_direct(0)) || (m_color != port_color->get_direct(0)) || (m_page != port_page->get_direct(0)))
    {
        m_mode = port_mode->get_direct(0);
        m_color = port_color->get_direct(0);
        m_page = port_page->get_direct(0);
        if ((m_mode & 0x0A) != 0x0A)
        {
            // Blanking
            m_mode_index = 0;
            m_back_color = Irisha_RGBA4[0];
            if ((m_mode & 0x80)==0) {
                // Mode 1 - mono low
                sx = 320;
                m_base_address = (m_page & 0x01) * 0x2000;
                m_page_size = 8000;
            } else
                if ((m_mode & 0x01)==0) {
                    // Mode 2 - color low
                    sx = 320;
                    m_base_address = 0;
                    m_page_size = 16000;
                } else {
                    // Mode 3 - mono high
                    m_mode_index = 3;
                    sx = 640;
                    m_base_address = 0;
                    m_page_size = 16000;
                }
        } else {
            if ((m_mode & 0x80)==0) {
                // Mode 1 - mono low
                sx = 320;
                m_mode_index = 1;
                m_base_address = (m_page & 0x01) * 0x2000;
                m_page_size = 8000;
                m_fore_color = Irisha_RGBA8[m_color & 0x07];
                m_back_color = Irisha_RGBA4[((m_color >> 3) & 1) | ((m_color >> 4) & 2)];
            } else
            if ((m_mode & 0x01)==1) {
                // Mode 2 - color low
                m_mode_index = 2;
                sx = 320;
                m_base_address = 0;
                m_page_size = 16000;
                m_back_color = Irisha_RGBA16[m_color & 0x0F];
            } else {
                // Mode 3 - mono high
                m_mode_index = 3;
                sx = 640;
                m_base_address = 0;
                m_page_size = 16000;
            }
        }
        screen_valid = false;
        was_updated = true;
    }
}

void IrishaDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(Irisha_Back_4, Irisha_RGBA4, 4);
    vr.FillRGB(Irisha_Color_8, Irisha_RGBA8, 8);
    vr.FillRGB(Irisha_Back_16, Irisha_RGBA16, 16);
    vr.FillRGB(Irisha_Pallette1_4, Irisha_RGBA4_Pal1, 4);
    vr.FillRGB(Irisha_Pallette2_4, Irisha_RGBA4_Pal2, 4);
}

void IrishaDisplay::render_all(const bool force_render)
{
    if (!screen_valid || force_render) {
        // Blank fields
        const auto buf = static_cast<uint32_t *>(render_pixels);
        const uint32_t black = Irisha_RGBA4[0];
        std::fill(buf, buf + 5 * line_bytes, black);
        std::fill(buf + sx * sy - 5 * line_bytes, buf + sx * sy, black);
        // Screen
        if (m_mode_index == 1) for (unsigned a=0; a < m_page_size; a++) render_mono(a);
        else
        if (m_mode_index == 2) for (unsigned a=0; a < m_page_size; a++) render_color(a);
        else
        if (m_mode_index == 3) for (unsigned a=0; a < m_page_size; a++) render_mono(a);
        else {
            render_blank();
        }
        screen_valid = true;
        was_updated = true;
    }
}

void IrishaDisplay::render_mono(const unsigned address) const
{
    const uint8_t b = vram->get_direct(m_base_address + address);
    const auto buf = static_cast<uint32_t *>(render_pixels);
    const unsigned offset = 5*line_bytes + address * 8; // 20 blank lines + Each byte gives 8 dots
    for (int k = 7; k >= 0; k--)
    {
        buf[offset + (7-k)] = ((b >> k) & 0x01) ? m_fore_color : m_back_color;
    }
}

void IrishaDisplay::render_color(const unsigned address) const
{
    const uint8_t b = vram->get_direct(m_base_address + address);
    const auto buf = static_cast<uint32_t *>(render_pixels);
    const unsigned offset = 5*line_bytes + address * 4; // 20 blank lines + Each byte gives 4 dots
    for (int k = 3; k >= 0; k--)
    {
        const unsigned c = (b >> k*2) & 0x03;
        uint32_t dot = m_back_color;
        if (c > 0) dot = ((m_color & 0x20) == 0) ? Irisha_RGBA4_Pal1[c] : Irisha_RGBA4_Pal2[c];
        buf[offset + (3-k)] = dot;
    }
}

void IrishaDisplay::render_blank() const
{
    auto * buf = static_cast<uint32_t *>(render_pixels);
    const unsigned offset = 5*line_bytes;       // 20 blank lines
    for (unsigned i = 0; i < sx * (sy - 40); i++)
        buf[offset + i] = m_back_color;
}

ComputerDevice * create_irisha_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IrishaDisplay(im, cd);
}
