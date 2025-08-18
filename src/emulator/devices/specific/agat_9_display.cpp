// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 display controller device

#include "agat_9_display.h"
#include "emulator/utils.h"
#include <iostream>


#define     M_256_EVEN  0       // Render even lines only
#define     M_256_ODD   1       // Render odd lines only
#define     M_512_ON    2       // Render all 512 interlaced lines

Agat9Display::Agat9Display(InterfaceManager *im, EmulatorConfigDevice *cd):
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

void Agat9Display::load_config(SystemData *sd)
{
    RasterDisplay::load_config(sd);

    m_port_mode = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode_agat").value));
    m_memory =    dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_font =      dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));
    m_pal1 =      dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("pal1").value));
    m_pal2 =      dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("pal2").value));

    blink_ticks = m_system_clock / (5*2);     // 5 Hz

    i_50hz.change(m_nmi_val);
    i_500hz.change(m_irq_val);

    // m_512_mode = M_512_ON;
    m_512_mode = M_256_EVEN;

    if (m_512_mode == M_512_ON) sy = 512;

    set_mode(0x02);
}

void Agat9Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(Agat_9_base_colors, Agat_RGBA16, 16);
}

void Agat9Display::set_mode(unsigned int new_mode)
{
    previous_mode = new_mode;
    mode = new_mode & 0x83;
    unsigned base_address_txt = ((new_mode & 0x70) >> 4) * 8192;
    unsigned base_address_gr =  (((new_mode & 0x70) >> 4) + (new_mode & 0x08)) * 8192;
    switch (mode) {
        case 0x02:
        case 0x82:
            // АЦР (Alphanumeric T32 & T64)
            base_address = base_address_txt + ((new_mode & 0x0C) >> 2) * 2048;
            break;
        default:
            // Graphic modes
            base_address = base_address_gr;
            _memory_bank = ((new_mode & 0x70) >> 4) + (new_mode & 0x08);
            break;
    }
    screen_valid = false;
    was_updated = true;
}

void Agat9Display::interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value)
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

void Agat9Display::clock(unsigned int counter)
{
    RasterDisplay::clock(counter);

    uint8_t mode_value = m_port_mode->get_direct();
    if (previous_mode != mode_value) set_mode(mode_value);

    clock_counter += counter;
    if (clock_counter >= blink_ticks) {
        clock_counter -= blink_ticks;
        blinker = !blinker;
    }
}

void Agat9Display::render_line(unsigned int screen_line)
{
    uint8_t * pixel_address;
    unsigned int p, screen_offset, font_line, char_address, inv;
    uint8_t v, v1, v2, font_val;
    uint8_t color[2];

    unsigned pallette = ((m_pal2->get_value(0) & 1) << 1) | (m_pal1->get_value(0) & 1);

    // As we psysically have 256 doubled lines, we use a half of screen_line in a 512-line mode
    unsigned line = (m_512_mode==M_512_ON) ? (screen_line / 2) : screen_line;

    switch (mode) {
    case 0x00:
    case 0x80:
        // ЦГВР (HiRes Color Graphics, 256x256, 4 colors, 4 pallettes = 16k, 2 banks)
        // 64 bytes per line
        // even lines in bank 0, odd lines in bank 1
        base_address = ((_memory_bank & 0xE) | (line & 1)) << 13;

        for (unsigned i=0; i<64; i++) {
            char_address = base_address + (line/2)*64 + i;
            v = m_memory->get_value(char_address);

            for (unsigned j=0; j<4; j++) {
                unsigned color = (v >> (3-j)*2) & 0x3;
                p = i * 32 + j*8 ;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                uint32_t c = Agat_RGBA16[Agat_4_index[pallette][color]];
                *(uint32_t*)pixel_address = c;
                *(uint32_t*)(pixel_address+4) = c;
            }
        }
        break;

    case 0x01:
    case 0x81:
        // ЦГСР (MidRes Color Graphics, 128x128, 16 colors)
        // 64 bytes per line
        base_address = _memory_bank << 13;
        for (unsigned i=0; i<64; i++) {
            char_address = base_address + (line/2)*64 + i;
            v = m_memory->get_value(char_address);
            for (unsigned j=0; j<2; j++) {
                uint32_t color = Agat_RGBA16[(v >> (1-j)*4) & 0xF];
                p = i * 32 + j*16 ;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                std::fill_n((uint32_t*)pixel_address, 4, color);
            }
        }
        break;

    case 0x02: {
        // АЦР-32 (Alphanumeric 32 chars with attributes, 16 colors)
        // Each position takes two bytes in memory - a character code and its attrubute value
        // Each line is 64 bytes long
        // Filling two pixels per bit because of doubling

        // Blanking sides
        uint32_t black = Agat_RGBA16[0];
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
            font_val = m_font->get_value(v1*8 + font_line);

            screen_offset = 32*4 + i * (7*4*2);

            for (unsigned int k=0; k<7; k++) {              // Char is 7 pixels wide
                unsigned int c = (font_val >> k) & 0x01;
                unsigned int ccl;
                if ( (((v2 & 0x20) != 0) || ((v2 & 0x08) != 0) && blinker) )
                    ccl = cl * c;
                else
                    ccl = cl * (c ^ 0x01);
                uint32_t color = Agat_RGBA16[ccl];
                p = screen_offset + (6-k)*8;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                *(uint32_t*)pixel_address = color;
                *(uint32_t*)(pixel_address+4) = color;
            }

        }
        break;
    }

    case 0x82: {
        // АЦР-64 (Alphanumeric 64 chars, monochrome)
        // Each line is 64 bytes long

        // Blanking sides
        uint32_t black = Agat_RGBA16[0];
        pixel_address = ((uint8_t *)render_pixels) + line*line_bytes;
        for (unsigned int j=0; j<32; j++) {
            *(uint32_t *)(pixel_address + j*4) = black;
            *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
        }

        // inv = ((~previous_mode & 0x04) >> 2);                       // inverted mode
        inv = 0;
        font_line = line % 8;
        for (unsigned int i=0; i<64; i++) {
            char_address = base_address + (line/8)*64 + i;
            v1 = m_memory->get_value(char_address);       // Character
            font_val = m_font->get_value(v1*8 + font_line);
            screen_offset = 32*4 + i * (7*4);
            for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                unsigned int c = ((font_val >> k) & 1) ^ inv;
                p = screen_offset + (6-k)*4;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                *(uint32_t*)pixel_address = Agat_RGBA16[Agat_2_index[pallette][c]];
            }
        }
        break;
    }

    case 0x03:
        // МГВР (HiRes Mono Graphics, 256x256, 2 colors, 4 pallettes)
        // 32 bytes per line
        base_address = _memory_bank << 13;

        for (unsigned i=0; i<32; i++) {
            char_address = base_address + line*32 + i;
            v = m_memory->get_value(char_address);

            for (unsigned j=0; j<8; j++) {
                unsigned color = (v >> (7-j)) & 0x1;
                p = i * 64 + j*8 ;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                uint32_t c = Agat_RGBA16[Agat_2_index[pallette][color]];
                *(uint32_t*)pixel_address = c;
                *(uint32_t*)(pixel_address+4) = c;
            }
        }
        break;

    case 0x83:
        // МГДП (Dbl-HiRes Mono Graphics, 512x256, 2 colors, 4 pallettes, 16k per screen)
        // 64 bytes per line
        // even lines in bank 0, odd lines in bank 1
        base_address = ((_memory_bank & 0xE) | (line & 1)) << 13;

        for (unsigned i=0; i<64; i++) {
            char_address = base_address + (line/2)*64 + i;
            v = m_memory->get_value(char_address);

            for (unsigned j=0; j<8; j++) {
                unsigned color = (v >> (7-j)) & 0x1;
                p = i * 32 + j*4 ;
                pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                uint32_t c = Agat_RGBA16[Agat_2_index[pallette][color]];
                *(uint32_t*)pixel_address = c;
            }
        }
        break;
    }
}


void Agat9Display::render_all(bool force_render)
{
    screen_valid = true;
    was_updated = true;
}

void Agat9Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void Agat9Display::VSYNC(const unsigned sync_val)
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

void Agat9Display::HSYNC(const unsigned line, const unsigned sync_val)
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

ComputerDevice * create_agat_9_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat9Display(im, cd);
}
