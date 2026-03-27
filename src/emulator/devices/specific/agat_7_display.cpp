// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-7 display controller device

#include <iostream>
#include "agat_7_display.h"
#include "emulator/utils.h"
#include "agat_common.h"

uint8_t Agat_16Colors[16][3]  = {
                    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
                    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217},
                    { 38,  38, 	38}, {255,  38,  38}, { 38, 255,  38}, {255, 255,  38},
                    { 38,  38, 255}, {255,  38, 255}, { 38, 255, 255}, {255, 255, 255}
        };

uint32_t Agat_RGBA16[16];

#define     M_256_EVEN  0       // Render even lines only
#define     M_256_ODD   1       // Render odd lines only
#define     M_512_ON    2       // Render all 512 interlaced lines

#define     A7_OPTION_PALCARD   1
#define     A7_PALCARD_ON       0
#define     A7_PALCARD_OFF      1

Agat7Display::Agat7Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    RasterDisplay(im, cd)
    , previous_mode(_FFFF)
    , blinker(false)
    , clock_counter(0)
    , m_irq_val(1)
    , m_nmi_val(1)
    , i_50hz(this, im, 1, "50hz", MODE_W)
    , i_500hz(this, im, 1, "500hz", MODE_W)
    , i_ints_en(this, im, 1, "ints_en", MODE_R, 1)
{
    sx = 512; // Doubling 2*256 because of 64 chars mode;
    sy = 256;
}

emulator::Result Agat7Display::load_config(SystemData *sd)
{
    emulator::Result res = RasterDisplay::load_config(sd);
    if (!res) return res;

    m_port_mode = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    m_memory =    dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_font =      dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));

    blink_ticks = m_system_clock / (5*2);     // 5 Hz

    i_50hz.change(m_nmi_val);
    i_500hz.change(m_irq_val);

    // m_512_mode = M_512_ON;
    m_512_mode = M_256_EVEN;

    if (m_512_mode == M_512_ON) sy = 512;

    const std::string pal_mem = read_confg_value(cd, "pal_mem", false, std::string(""));
    const std::string pal_switch = read_confg_value(cd, "pal_switch", false, std::string(""));
    const std::string pal_mode = read_confg_value(cd, "pal_mode", false, std::string(""));
    const std::string pal_font = read_confg_value(cd, "pal_font", false, std::string(""));

    if (!pal_mem.empty() || !pal_switch.empty() || !pal_mode.empty() || !pal_font.empty()) {
        try {
            m_pal_mem = dynamic_cast<RAM*>(im->dm->get_device_by_name(pal_mem));
            m_pal_switch = dynamic_cast<PortAddress*>(im->dm->get_device_by_name(pal_switch));
            m_pal_mode = dynamic_cast<PortAddress*>(im->dm->get_device_by_name(pal_mode));
            m_pal_font = dynamic_cast<RAM*>(im->dm->get_device_by_name(pal_font));
            m_pal_card = true;
            m_pal_card_out = true;
            m_pal_builtin = read_confg_value(cd, "pal_builtin", false, false);
        } catch (std::exception &e) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{Agat7Display|" + std::string(QT_TRANSLATE_NOOP("Agat7Display", "Incorrect display config - palette card")) + "}");
        }
    }

    set_mode(0x02);

    return emulator::Result::ok();
}

void Agat7Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(Agat_16Colors, Agat_RGBA16, 16);
    for (int i=0; i<8; i++) vr.FillRGB(Agat_Palcard_std_pal[i], Agat_RGBA16_palcard_std[i], 16);
}

void Agat7Display::set_mode(unsigned int new_mode)
{
    previous_mode = new_mode;
    mode = new_mode & 0x83;
    base_address = ((new_mode & 0x70) >> 4) * 8192;
    switch (mode) {
    case 0x00:
        // ГНР (LoRes Graphics)
        page_size = 2048;
        base_address += ((new_mode & 0x0C) >> 2) * 2048;
        break;
    case 0x01:
        // ГCР (MidRes Graphics)
        page_size = 8192;
        break;
    case 0x02:
    case 0x82:
        // АЦР (Alphanumeric)
        page_size = 2048;
        base_address += ((new_mode & 0x0C) >> 2) * 2048;
        break;
    case 0x03:
        // ГВР (HiRes Graphics)
        page_size = 8192;
        break;
    default:
        break;
    }
    screen_valid = false;
    was_updated = true;
}

void Agat7Display::interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value)
{
    if (callback_id == 1) {
        if ((new_value & 1) != 0) {
            // Return both the signals to 1 after disabling interrupts
            m_irq_val = m_nmi_val = 1;
            i_50hz.change(m_nmi_val);
            i_500hz.change(m_irq_val);
        }
    }
}

void Agat7Display::clock(unsigned int counter)
{
    RasterDisplay::clock(counter);

    uint8_t mode_value = m_port_mode->get_direct(0);
    if (previous_mode != mode_value) set_mode(mode_value);

    clock_counter += counter;
    if (clock_counter >= blink_ticks) {
        clock_counter -= blink_ticks;
        blinker = !blinker;
    }
}

DeviceOptions Agat7Display::get_device_options()
{
    if (m_pal_card)
        return {
                    {
                        A7_OPTION_PALCARD, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Output type"), "kscreensaver.png",
                        {
                            {A7_PALCARD_ON, QT_TRANSLATE_NOOP("DeviceOptions", "Palette card")},
                            {A7_PALCARD_OFF, QT_TRANSLATE_NOOP("DeviceOptions", "Standard")}
                        }
                    }
        };
    return {};
};

void Agat7Display::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id == A7_OPTION_PALCARD) m_pal_card_out = value_id == A7_PALCARD_ON;
}

uint32_t Agat7Display::convert_rgba(const unsigned c, const uint32_t rgba[]) const
{
    if (!m_pal_card || !m_pal_card_out) return rgba[c];
    const auto pal = m_pal_switch->get_direct(0);
    if (pal < 8) {
        if (m_pal_builtin) return Agat_RGBA16_palcard_std[pal][c];
        return rgba[c];
    }
    const uint8_t R = m_pal_mem->get_direct(0x00 + c) * 17;
    const uint8_t G = m_pal_mem->get_direct(0x10 + c) * 17;
    const uint8_t B = m_pal_mem->get_direct(0x20 + c) * 17;
    return renderer->MapRGB(R, G, B);
}

uint8_t Agat7Display::convert_font(unsigned chr, unsigned line) const
{
    const unsigned a = chr*8 + line;
    if (!m_pal_card) return m_font->get_direct(a);
    if ((m_pal_mode->get_direct(0) & 0x02) != 0) {
        uint8_t v = m_pal_font->get_direct(a);
        v = ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
        v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
        v = (v >> 4) | (v << 4);
        return ~v;
    }
    return m_font->get_direct(a);

}

void Agat7Display::render_line(unsigned int screen_line)
{
    compat_lock_guard guard(m_surface_mutex);
    if (!has_valid_renderer()) return;
    uint8_t * pixel_address;
    unsigned int p, screen_offset, font_line, char_address, inv;
    uint8_t v, v1, v2, font_val;
    uint8_t color[2];

    // As we psysically have 256 doubled lines, we use a half of screen_line in a 512-line mode
    unsigned line = (m_512_mode==M_512_ON) ? (screen_line / 2) : screen_line;

    const uint32_t black = Agat_RGBA16[0];

    switch (mode) {
    case 0x00:
        // ГНР (LoRes Graphics, 64x64, 16 colors)
        // 32 bytes per line
        for (unsigned int i=0; i<32; i++) {
            char_address = base_address + (line/4)*32 + i;
            v = m_memory->get_value(char_address);
            color[0] = v >> 4;
            color[1] = v & 0x0F;
            screen_offset = i * 64;
            for (unsigned int j = 0; j < 2; j++) {
                unsigned int c = color[j];
                for (unsigned int k = 0; k < 8; k++) {
                    p = screen_offset + j*32 + k*4;

                    pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                    // *(uint32_t*)pixel_address = Agat_RGBA16[c];
                    *(uint32_t*)pixel_address = convert_rgba(c, Agat_RGBA16);
                }
            }
        }
        break;

    case 0x01:
        // ГCР (MidRes Graphics, 128x128, 16 colors)
        // 64 bytes per line
        for (unsigned int i=0; i<64; i++) {
            char_address = base_address + (line/2)*64 + i;
            v = m_memory->get_value(char_address);
            color[0] = v >> 4;
            color[1] = v & 0x0F;
            screen_offset = i * 32;
            for (unsigned int j = 0; j < 2; j++)
                for (unsigned int k = 0; k < 4; k++) {
                    p = screen_offset + j*16 + k*4;
                    unsigned int c = color[j];
                    pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                    // *(uint32_t*)pixel_address = Agat_RGBA16[c];
                    *(uint32_t*)pixel_address = convert_rgba(c, Agat_RGBA16);
                }
        }
        break;

    case 0x02:
        // АЦР-32 (Alphanumeric 32 chars with attributes, 16 colors)
        // Each position takes two bytes in memory - a character code and its attrubute value
        // Each line is 64 bytes long
        // Filling two pixels per bit because of doubling

        // Blanking sides
        pixel_address = ((uint8_t *)render_pixels) + line*line_bytes;
        for (unsigned int j=0; j<32; j++) {
            *(uint32_t *)(pixel_address + j*4) = black;
            *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
        }

        font_line = line % 8;
        for (unsigned int i=0; i<32; i++) {
            char_address = base_address + (line/8)*64 + i*2;
            v1 = m_memory->get_value(char_address);       // Character
            v2 = m_memory->get_value(char_address+1);     // Attribute
            unsigned int cl = (v2 & 0x07) | ((v2 & 0x10) >> 1);         // Color index (YBGR)
            // font_val = m_font->get_value(v1*8 + font_line);
            font_val = convert_font(v1, font_line);

            screen_offset = 32*4 + i * (7*4*2);

            for (unsigned int k=1; k<=7; k++) {              // Char is 7 pixels wide
                unsigned int c = (font_val >> k) & 0x01;
                unsigned int ccl;
                if ( (((v2 & 0x20) != 0) || ((v2 & 0x08) != 0) && blinker) )
                    ccl = cl * c;
                else
                    ccl = cl * (c ^ 0x01);
                // uint32_t color = Agat_RGBA16[ccl];
                uint32_t color = convert_rgba(ccl, Agat_RGBA16);
                p = screen_offset + (7-k)*8;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                *(uint32_t*)pixel_address = color;
                *(uint32_t*)(pixel_address+4) = color;
            }

        }
        break;

    case 0x82:
        // АЦР-64 (Alphanumeric 64 chars, monochrome)
        // Each line is 64 bytes long

        // Blanking sides
        pixel_address = ((uint8_t *)render_pixels) + line*line_bytes;
        for (unsigned int j=0; j<32; j++) {
            *(uint32_t *)(pixel_address + j*4) = black;
            *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
        }

        inv = ((~previous_mode & 0x04) >> 2);                       // inverted mode
        font_line = line % 8;
        for (unsigned int i=0; i<64; i++) {
            char_address = base_address + (line/8)*64 + i;
            v1 = m_memory->get_value(char_address);       // Character
            // font_val = m_font->get_value(v1*8 + font_line);
            font_val = convert_font(v1, font_line);
            screen_offset = 32*4 + i * (7*4);
            for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                unsigned int c = ((font_val >> k) & 1) ^ inv;
                p = screen_offset + (6-k)*4;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                // *(uint32_t*)pixel_address = Agat_RGBA2[c];
                *(uint32_t*)pixel_address = convert_rgba(c*15, Agat_RGBA16);
            }
        }
        break;

    case 0x03:
        // ГВР (HiRes Graphics, monochrome 256x256)
        // Filling two pixels per bit because of doubling

        for (unsigned int i=0; i<32; i++) {
            char_address = base_address + line*32 + i;
            v = m_memory->get_value(char_address);
            screen_offset = i * 64;

            for (unsigned int k = 0; k < 8; k++) {
                unsigned int c = (v >> k) & 0x01;
                // uint32_t color = Agat_RGBA2[c];
                uint32_t color = convert_rgba(c*15, Agat_RGBA16);
                p = screen_offset + (7-k)*8;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                *(uint32_t*)pixel_address = color;
                *(uint32_t*)(pixel_address+4) = color;
            }
        }
        break;
    }
}


void Agat7Display::render_all(bool force_render)
{
    screen_valid = true;
    was_updated = true;
}

void Agat7Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void Agat7Display::VSYNC(const unsigned sync_val)
{
    // if ((i_ints_en.value & 1) == 0) {
    //     std::cout << "- VSYNC " << sync_val << std::endl;
    // }

    // In Agat-7 NMI follows VBLANC if enabled
    unsigned nmi_val = ((i_ints_en.value & 1) == 0)?sync_val:1;
    if (nmi_val != m_nmi_val) {
        m_nmi_val = nmi_val;
        i_50hz.change(m_nmi_val);
    }
}

void Agat7Display::HSYNC(const unsigned line, const unsigned sync_val)
{
    if (sync_val == 0) {
        // We fire irqs _before_ HSYNC
        // In Agat-7 IRQ is a 1:1 meander with a frequency of 32+32 lines (for a 625-lines frame)
        // Except a last period which is shorter (625 < 64*10)
        // https://forum.agatcomp.ru//viewtopic.php?id=244
        if ((i_ints_en.value & 1) == 0) {
            // Align with VSYNC(1)
            unsigned shifted_line = (line >= 2*m_top_blank) ? (line - 2*m_top_blank) : (line + m_lines - 2*m_top_blank);
            unsigned irq_val = (shifted_line >> 5) & 1;
            // std::cout << line << " : " << irq_val << std::endl;
            if (irq_val != m_irq_val) {
                m_irq_val = irq_val;
                i_500hz.change(m_irq_val);
            }
        }
    } else {
        // And rendering a line _after_ HSYNC
        // 56 is an experimentally found first visible line (512 < 576, so we need to forcefully move the frame lower)
        int screen_line;
        if (m_512_mode == M_512_ON) {
            // Render all 512 interlaced lines
            screen_line = static_cast<int>(line - m_top_blank*2 - 56);
            if (screen_line >= 0 && screen_line < 512)
                render_line(screen_line);
        } else {
            // Render 256 lines, using even or odd lines of the full 625-lines frame only
            screen_line = static_cast<int>(line - m_top_blank*2 - 56) / 2;
            if (screen_line >= 0 && screen_line < 256 && ((line & 1) == m_512_mode)) {
                render_line(screen_line);
                // uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes;
                // *(uint32_t*)pixel_address = Agat_RGBA16[mode & 0x3];
            }
        }
    }
}

ComputerDevice * create_agat_7_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat7Display(im, cd);
}
