// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 display controller device

#include <iostream>
#include "agat_9_display.h"
#include "emulator/utils.h"
#include "agat_common.h"


#define     M_256_EVEN  0       // Render even lines only
#define     M_256_ODD   1       // Render odd lines only
#define     M_512_ON    2       // Render all 512 interlaced lines

#define     A9_OPTION_COLORS    0
#define     A9_OPTION_PALCARD   1

#define     A9_COLOR_16         0
#define     A9_COLOR_16i        1
#define     A9_COLOR_8          2
#define     A9_COLOR_BW         3
#define     A9_COLOR_EX         4

#define     A9_PALCARD_ON       0
#define     A9_PALCARD_OFF      1

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
    , m_color_mode(A9_COLOR_16)
{
    sx = 512; // Doubling 2*256 because of 64 chars mode;
    sy = 256;
}

dsk_tools::Result Agat9Display::load_config(SystemData *sd)
{
    dsk_tools::Result res = RasterDisplay::load_config(sd);
    if (!res) return res;

    m_mode_agat =  dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode_agat").value));
    m_mode_apple = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("mode_apple").value));
    m_pa =         dynamic_cast<Register*>(im->dm->get_device_by_name(cd->get_parameter("pa").value));
    m_memory =     dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_font =       dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));
    m_pal =        dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("pallette").value));

    m_color_options = read_confg_value(cd, "color_options", false, true);

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
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError, "{Agat9Display|" + std::string(QT_TRANSLATE_NOOP("Agat9Display", "Incorrect display config - palette card")) + "}");
        }
    }

    m_mode_apple->set_memory_callback(this, APPLE_MODE_CALLBACK, MODE_W);

    blink_ticks = m_system_clock / (5*2);     // 5 Hz

    i_50hz.change(m_nmi_val);
    i_500hz.change(m_irq_val);

    // m_512_mode = M_512_ON;
    m_512_mode = M_256_EVEN;

    if (m_512_mode == M_512_ON) sy = 512;

    set_mode(0x02);

    return dsk_tools::Result::ok();
}

void Agat9Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(Agat_9_base_colors, Agat_RGBA16, 16);
    vr.FillRGB(Agat_9_gray_colors, Agat_RGBA16gray, 16);
    vr.FillRGB(Agat_9_inverted_colors, Agat_RGBA16i, 16);
    vr.FillRGB(Agat_9_8_colors, Agat_RGBA16_8, 16);
    vr.FillRGB(Agat_9_ex_colors, Agat_RGBA16ex, 16);

    for (int i=0; i<8; i++) vr.FillRGB(Agat_Palcard_std_pal[i], Agat_RGBA16_palcard_std[i], 16);
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

DeviceOptions Agat9Display::get_device_options()
{
    if (m_pal_card)
        return {
                {
                    A9_OPTION_PALCARD, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Output type"), "kscreensaver.png",
                    {
                        {A9_PALCARD_ON, QT_TRANSLATE_NOOP("DeviceOptions", "Palette card")},
                        {A9_PALCARD_OFF, QT_TRANSLATE_NOOP("DeviceOptions", "Standard")}
                    }
                }
        };
    if (m_color_options)
        return {
            {
                A9_OPTION_COLORS, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Output type"), "kscreensaver.png",
                {
                    {A9_COLOR_16, QT_TRANSLATE_NOOP("DeviceOptions", "16 colors")},
                    {A9_COLOR_16i, QT_TRANSLATE_NOOP("DeviceOptions", "16 inverted")},
                    {A9_COLOR_8, QT_TRANSLATE_NOOP("DeviceOptions", "8 colors")},
                    {A9_COLOR_BW, QT_TRANSLATE_NOOP("DeviceOptions", "Grayscale")},
                    {A9_COLOR_EX, QT_TRANSLATE_NOOP("DeviceOptions", "Experimental")}
                }
            }
        };
    return {};
};

void Agat9Display::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id == A9_OPTION_COLORS) m_color_mode = value_id;
    if (option_id == A9_OPTION_PALCARD) m_pal_card_out = value_id == A9_PALCARD_ON;
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

uint32_t Agat9Display::convert_rgba(const unsigned c, const uint32_t rgba[]) const
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

uint8_t Agat9Display::convert_font(unsigned chr, unsigned line) const
{
    const unsigned a = chr*8 + line;
    if (!m_pal_card) return m_font->get_direct(a);
    if ((m_pal_mode->get_direct(0) & 0x02) != 0) return m_pal_font->get_direct(a) >> 1;
    return m_font->get_direct(a);

}

void Agat9Display::render_line(unsigned int screen_line)
{
    compat_lock_guard guard(m_surface_mutex);
    if (has_valid_renderer()) {
        // As we physically have 256 doubled lines, we use a half of screen_line in a 512-line mode
        unsigned line = (m_512_mode==M_512_ON) ? (screen_line / 2) : screen_line;

        unsigned PA = m_pa->get_value() & 1;

        unsigned pallette = ((m_pal->get_buffer()[1] & 1) << 1) | (m_pal->get_buffer()[0] & 1);

        const uint32_t * rgba_;
        switch (m_color_mode) {
            case A9_COLOR_16:  rgba_ = Agat_RGBA16;    break;
            case A9_COLOR_BW:  rgba_ = Agat_RGBA16gray; break;
            case A9_COLOR_16i: rgba_ = Agat_RGBA16i;    break;
            case A9_COLOR_EX:  rgba_ = Agat_RGBA16ex;    break;
            default:           rgba_ = Agat_RGBA16_8;   break;
        }

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
                        uint8_t v = m_memory->get_direct(char_address);

                        for (unsigned j=0; j<4; j++) {
                            unsigned color = (v >> (3-j)*2) & 0x3;
                            unsigned p = i * 32 + j*8 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            uint32_t c = convert_rgba(Agat_4_index[pallette][color], rgba_);
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
                        uint8_t v = m_memory->get_direct(char_address);
                        for (unsigned j=0; j<2; j++) {
                            uint32_t color = convert_rgba((v >> (1-j)*4) & 0xF, rgba_);
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
                    const auto buf = static_cast<uint32_t *>(render_pixels);
                    uint32_t black = convert_rgba(0, rgba_);
                    unsigned screen_offset = screen_line * (64+32*7*2);
                    for (unsigned int j=0; j<32; j++) {
                        buf[screen_offset + j] = black;
                        buf[screen_offset + 480 + j] = black;
                    }

                    unsigned font_line = line % 8;
                    for (unsigned int i=0; i<32; i++) {
                        unsigned char_address = base_address + (line/8)*64 + i*2;
                        uint8_t v1 = m_memory->get_direct(char_address);       // Character
                        uint8_t v2 = m_memory->get_direct(char_address+1);     // Attribute
                        unsigned cl = (v2 & 0x07) | ((v2 & 0x10) >> 1);       // Color index (YBGR)
                        uint8_t font_val = convert_font(v1, font_line);

                        unsigned buf_offset = screen_line * (64+32*7*2) + 32 + i * (7*2);

                        for (unsigned k=0; k<7; k++) {              // Char is 7 pixels wide
                            unsigned c = (font_val >> k) & 0x01;
                            unsigned ccl;
                            if ( (((v2 & 0x20) != 0) || ((v2 & 0x08) != 0) && blinker) )
                                ccl = cl * c;
                            else
                                ccl = cl * (c ^ 0x01);
                            uint32_t color = convert_rgba(ccl, rgba_);
                            unsigned pixel_index = buf_offset + (6-k)*2;
                            buf[pixel_index] = color;
                            buf[pixel_index + 1] = color;
                        }

                    }
                    break;
                }

                case 0x82: {
                    // АЦР-64 (Alphanumeric 64 chars, monochrome)
                    // Each line is 64 bytes long

                    // Blanking sides
                    uint32_t black = convert_rgba(0, rgba_);;
                    uint8_t * pixel_address = ((uint8_t *)render_pixels) + line*line_bytes;
                    for (unsigned int j=0; j<32; j++) {
                        *(uint32_t *)(pixel_address + j*4) = black;
                        *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
                    }

                    unsigned font_line = line % 8;
                    for (unsigned int i=0; i<64; i++) {
                        unsigned char_address = base_address + (line/8)*64 + i;
                        uint8_t v1 = m_memory->get_direct(char_address);       // Character
                        uint8_t font_val = convert_font(v1, font_line);
                        unsigned screen_offset = 32*4 + i * (7*4);
                        for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                            unsigned int c = (font_val >> k) & 1;
                            unsigned p = screen_offset + (6-k)*4;
                            pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            *(uint32_t*)pixel_address = convert_rgba(Agat_2_index[pallette][c], rgba_);
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
                        uint8_t v = m_memory->get_direct(char_address);

                        for (unsigned j=0; j<8; j++) {
                            unsigned color = (v >> (7-j)) & 0x1;
                            unsigned p = i * 64 + j*8 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            uint32_t c = convert_rgba(Agat_2_index[pallette][color], rgba_);
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
                        uint8_t v = m_memory->get_direct(char_address);

                        for (unsigned j=0; j<8; j++) {
                            unsigned color = (v >> (7-j)) & 0x1;
                            unsigned p = i * 32 + j*4 ;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            uint32_t c = convert_rgba(Agat_2_index[pallette][color], rgba_);
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
                uint32_t black = convert_rgba(0, rgba_);;
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
                        uint8_t v = m_memory->get_direct(line_address + i);
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

                        uint8_t font_val = convert_font(chr, font_line);
                        unsigned screen_offset = i * (7*4);
                        for (unsigned int k=0; k<7; k++) {
                            unsigned int c = ((font_val >> k) & 1) ^ inv;
                            unsigned p = screen_offset + (6-k)*4;
                            uint8_t * pixel_address = static_cast<uint8_t *>(render_pixels) + screen_line*line_bytes + p;
                            *(uint32_t*)pixel_address = convert_rgba(Agat_2_index[pallette][c], rgba_);
                        }
                    }
                } else {
                    // Graphic mode
                    uint32_t color;
                    // uint32_t black = convert_rgba(0, rgba_);
                    // uint32_t white = convert_rgba(15, rgba_);
                    uint32_t black = rgba_[0];
                    uint32_t white = rgba_[15];

                    base_address = (m_a2_page==0)?0x2000:0x4000;
                    unsigned line_address = base_address + (apple_line & 7)*1024 + ((apple_line >> 3) & 7)*128 + ((apple_line >> 6) & 3)*40;

                    bool prev_on = false;
                    for (unsigned i=0; i<40; i++) {
                        uint8_t b = m_memory->get_direct(line_address + i);
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
                                    color = convert_rgba(Agat_Apple_index[is_odd][hi], rgba_);
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
