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
    , m_a2_text(0)
    , m_a2_mixed(0)
    , m_a2_page(0)
{
    sx = 512; // Doubling 2*256 because of 64 chars mode;
    sy = 256;
}

void Agat9Display::load_config(SystemData *sd)
{
    RasterDisplay::load_config(sd);

    m_mode_agat =  dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode_agat").value));
    m_mode_apple = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("mode_apple").value));
    m_pa =         dynamic_cast<Register*>(im->dm->get_device_by_name(cd->get_parameter("pa").value));
    m_memory =     dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_font =       dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));
    m_pal =        dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("pallette").value));

    m_mode_apple->set_memory_callback(this, APPLE_MODE_CALLBACK, MODE_W);

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

void Agat9Display::memory_callback(unsigned int callback_id, unsigned int address)
{
    if (callback_id == APPLE_MODE_CALLBACK) {
        m_a2_text = m_mode_apple->get_buffer()[0] & 1;
        m_a2_mixed = m_mode_apple->get_buffer()[1] & 1;
        m_a2_page = m_mode_apple->get_buffer()[2] & 1;
        // std::cout << "0 text: " << m_a2_text << std::endl;
        // std::cout << "1 mix: " << m_a2_mixed << std::endl;
        // std::cout << "2 page: " << m_a2_page << std::endl;
    }
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

    uint8_t mode_value = m_mode_agat->get_direct(0);
    if (previous_mode != mode_value) set_mode(mode_value);

    clock_counter += counter;
    if (clock_counter >= blink_ticks) {
        clock_counter -= blink_ticks;
        blinker = !blinker;
    }
}

void Agat9Display::render_line(unsigned int screen_line)
{
    if (has_valid_renderer()) {
        // As we psysically have 256 doubled lines, we use a half of screen_line in a 512-line mode
        unsigned line = (m_512_mode==M_512_ON) ? (screen_line / 2) : screen_line;

        unsigned PA = m_pa->get_value() & 1;

        unsigned pallette = ((m_pal->get_buffer()[1] & 1) << 1) | (m_pal->get_buffer()[0] & 1);

        if (PA == 1) {
            // Agat mode
            if (sx != 512) {
                change_resolution(512, sy);
                return;
            }

            switch (mode) {
                case 0x00:
                case 0x80: {
                    // ЦГВР (HiRes Color Graphics, 256x256, 4 colors, 4 pallettes = 16k, 2 banks)
                    // 64 bytes per line
                    // even lines in bank 0, odd lines in bank 1
                    base_address = ((_memory_bank & 0xE) | (line & 1)) << 13;

                    for (unsigned i=0; i<64; i++) {
                        unsigned char_address = base_address + (line/2)*64 + i;
                        uint8_t v = m_memory->get_value(char_address);

                        for (unsigned j=0; j<4; j++) {
                            unsigned color = (v >> (3-j)*2) & 0x3;
                            unsigned p = i * 32 + j*8 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            uint32_t c = Agat_RGBA16[Agat_4_index[pallette][color]];
                            *(uint32_t*)pixel_address = c;
                            *(uint32_t*)(pixel_address+4) = c;
                        }
                    }
                    break;
                }
                case 0x01:
                case 0x81: {
                    // ЦГСР (MidRes Color Graphics, 128x128, 16 colors)
                    // 64 bytes per line
                    base_address = _memory_bank << 13;
                    for (unsigned i=0; i<64; i++) {
                        unsigned char_address = base_address + (line/2)*64 + i;
                        uint8_t v = m_memory->get_value(char_address);
                        for (unsigned j=0; j<2; j++) {
                            uint32_t color = Agat_RGBA16[(v >> (1-j)*4) & 0xF];
                            unsigned p = i * 32 + j*16 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            std::fill_n((uint32_t*)pixel_address, 4, color);
                        }
                    }
                    break;
                }
                case 0x02: {
                    // АЦР-32 (Alphanumeric 32 chars with attributes, 16 colors)
                    // Each position takes two bytes in memory - a character code and its attrubute value
                    // Each line is 64 bytes long
                    // Filling two pixels per bit because of doubling

                    // Blanking sides
                    uint32_t black = Agat_RGBA16[0];
                    uint8_t * pixel_address = ((uint8_t *)render_pixels) + line*line_bytes;
                    for (unsigned int j=0; j<32; j++) {
                        *(uint32_t *)(pixel_address + j*4) = black;
                        *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
                    }

                    unsigned font_line = line % 8;
                    for (unsigned int i=0; i<32; i++) {
                        unsigned char_address = base_address + (line/8)*64 + i*2;
                        uint8_t v1 = m_memory->get_value(char_address);       // Character
                        uint8_t v2 = m_memory->get_value(char_address+1);     // Attribute
                        unsigned int cl = (v2 & 0x07) | ((v2 & 0x10) >> 1);         // Color index (YBGR)
                        uint8_t font_val = m_font->get_value(v1*8 + font_line);

                        unsigned screen_offset = 32*4 + i * (7*4*2);

                        for (unsigned int k=0; k<7; k++) {              // Char is 7 pixels wide
                            unsigned int c = (font_val >> k) & 0x01;
                            unsigned int ccl;
                            if ( (((v2 & 0x20) != 0) || ((v2 & 0x08) != 0) && blinker) )
                                ccl = cl * c;
                            else
                                ccl = cl * (c ^ 0x01);
                            uint32_t color = Agat_RGBA16[ccl];
                            unsigned p = screen_offset + (6-k)*8;
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
                    uint8_t * pixel_address = ((uint8_t *)render_pixels) + line*line_bytes;
                    for (unsigned int j=0; j<32; j++) {
                        *(uint32_t *)(pixel_address + j*4) = black;
                        *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
                    }

                    unsigned font_line = line % 8;
                    for (unsigned int i=0; i<64; i++) {
                        unsigned char_address = base_address + (line/8)*64 + i;
                        uint8_t v1 = m_memory->get_value(char_address);       // Character
                        uint8_t font_val = m_font->get_value(v1*8 + font_line);
                        unsigned screen_offset = 32*4 + i * (7*4);
                        for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                            unsigned int c = (font_val >> k) & 1;
                            unsigned p = screen_offset + (6-k)*4;
                            pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            *(uint32_t*)pixel_address = Agat_RGBA16[Agat_2_index[pallette][c]];
                        }
                    }
                    break;
                }

                case 0x03: {
                    // МГВР (HiRes Mono Graphics, 256x256, 2 colors, 4 pallettes)
                    // 32 bytes per line
                    base_address = _memory_bank << 13;

                    for (unsigned i=0; i<32; i++) {
                        unsigned char_address = base_address + line*32 + i;
                        uint8_t v = m_memory->get_value(char_address);

                        for (unsigned j=0; j<8; j++) {
                            unsigned color = (v >> (7-j)) & 0x1;
                            unsigned p = i * 64 + j*8 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            uint32_t c = Agat_RGBA16[Agat_2_index[pallette][color]];
                            *(uint32_t*)pixel_address = c;
                            *(uint32_t*)(pixel_address+4) = c;
                        }
                    }
                    break;
                }
                case 0x83: {
                    // МГДП (Dbl-HiRes Mono Graphics, 512x256, 2 colors, 4 pallettes, 16k per screen)
                    // 64 bytes per line
                    // even lines in bank 0, odd lines in bank 1
                    base_address = ((_memory_bank & 0xE) | (line & 1)) << 13;

                    for (unsigned i=0; i<64; i++) {
                        unsigned char_address = base_address + (line/2)*64 + i;
                        uint8_t v = m_memory->get_value(char_address);

                        for (unsigned j=0; j<8; j++) {
                            unsigned color = (v >> (7-j)) & 0x1;
                            unsigned p = i * 32 + j*4 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            uint32_t c = Agat_RGBA16[Agat_2_index[pallette][color]];
                            *(uint32_t*)pixel_address = c;
                        }
                    }
                    break;
                }
            }
        } else {
            // Apple modes
            if (sx != 280) {
                change_resolution(280, sy);
                return;
            }
            if (line < 32 || line >=224) {
                uint32_t black = Agat_RGBA16[0];
                uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes;
                std::fill_n((uint32_t*)pixel_address, sx, black);
            } else {
                unsigned apple_line = line - 32;            // We use 192 lines of 256
                if (m_a2_text == 1 || (m_a2_mixed == 1 && apple_line >= (192-4*8))) {
                    // Text mode
                    base_address = (m_a2_page==0)?0x400:0x800;
                    unsigned font_line = apple_line % 8;        // Glyph line
                    unsigned y = apple_line / 8;                // Line on the screen
                    unsigned line_address = base_address + ((y & 7) << 7) + ((y >> 3) * 40);
                    for (unsigned int i=0; i<40; i++) {
                        uint8_t v = m_memory->get_value(line_address + i);
                        unsigned IP_ME = (v >> 6) & 3;
                        uint8_t chr;
                        uint8_t inv;
                        switch (IP_ME){
                            case 0b00:
                                // Inverse
                                chr = v & 0x3F + 0x80;
                                inv = 1;
                                break;
                            case 0b01:
                                // Blinking
                                chr = v & 0x3F + 0x80;
                                inv = blinker?1:0;
                                break;
                            default:
                                // Normal + Russian
                                chr = v;
                                inv = 0;
                                break;
                        }

                        uint8_t font_val = m_font->get_value(chr*8 + font_line);
                        unsigned screen_offset = i * (7*4);
                        for (unsigned int k=0; k<7; k++) {
                            unsigned int c = ((font_val >> k) & 1) ^ inv;
                            unsigned p = screen_offset + (6-k)*4;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            *(uint32_t*)pixel_address = Agat_RGBA16[Agat_2_index[pallette][c]];
                        }
                    }
                } else {
                    // Graphic mode
                    uint32_t color;
                    static uint32_t black = Agat_RGBA16[0];
                    static uint32_t white = Agat_RGBA16[15];

                    base_address = (m_a2_page==0)?0x2000:0x4000;
                    unsigned line_address = base_address + (apple_line & 7)*1024 + ((apple_line >> 3) & 7)*128 + ((apple_line >> 6) & 3)*40;

                    bool prev_on = false;
                    for (unsigned i=0; i<40; i++) {
                        uint8_t b = m_memory->get_value(line_address + i);
                        unsigned hi = (b >> 7) & 1;
                        unsigned group_offset = i * (7*4);
                        for (unsigned k=0; k<7; k++) {
                            unsigned p = group_offset + k*4;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            unsigned x = i*7 + k;
                            unsigned is_on = (b >> k) & 1;
                            unsigned is_odd = x & 1;
                            if (is_on != 0) {
                                if (!prev_on) {
                                    color = Agat_RGBA16[Agat_Apple_index[is_odd][hi]];
                                } else {
                                    color = white;
                                    *(uint32_t*)(pixel_address-4) = white;
                                }
                                prev_on = true;
                            } else {
                                color = black;
                                prev_on = false;
                            }
                            *(uint32_t*)pixel_address = color;
                        }
                    }
                }
            }
        }
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
    // In Agat-9 NMI follows inverted VSYNC
    // https://forum.agatcomp.ru//viewtopic.php?pid=3708#p3708
    unsigned nmi_val = ((i_ints_en.value & 1) == 0)?(sync_val ^ 1):1;
    if (nmi_val != m_nmi_val) {
        m_nmi_val = nmi_val;
        i_50hz.change(m_nmi_val);
    }
}

void Agat9Display::HSYNC(const unsigned line, const unsigned sync_val)
{
    if (sync_val == 0) {
        unsigned irq_val;

        // IRQ becomes low 39 times per every half-frame
        // https://forum.agatcomp.ru//viewtopic.php?pid=3708#p3708
        if ((i_ints_en.value & 1) == 0) {
            unsigned ll = line % 16;
            irq_val = (line<624 && ll<2)?0:1;
        } else
            irq_val = 1;

        if (irq_val != m_irq_val) {
            // std::cout << line << " : " << irq_val << std::endl;
            m_irq_val = irq_val;
            i_500hz.change(m_irq_val);
        }
    } else {
        // And rendering a line _after_ HSYNC
        constexpr int top_pos = 8;  // Experimentally set
        int screen_line;
        if (m_512_mode == M_512_ON) {
            // Render all 512 interlaced lines
            screen_line = static_cast<int>(line - m_top_blank*2 + top_pos);
            if (screen_line >= 0 && screen_line < 512)
                render_line(screen_line);
        } else {
            // Render 256 lines, using even or odd lines of the full 625-lines frame only
            screen_line = static_cast<int>(line - m_top_blank*2 + top_pos) / 2;
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
