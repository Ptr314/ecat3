// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat display controller device

#include "agat_display.h"
#include "emulator/utils.h"
#include <iostream>

uint8_t Agat_2Colors[2][3] =  {
                    {0, 0, 0},
                    {255, 255, 255}
        };

uint32_t Agat_RGBA2[2];

uint8_t Agat_16Colors[16][3]  = {
                    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
                    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217},
                    { 38,  38, 	38}, {255,  38,  38}, { 38, 255,  38}, {255, 255,  38},
                    { 38,  38, 255}, {255,  38, 255}, { 38, 255, 255}, {255, 255, 255}
        };

uint32_t Agat_RGBA16[16];

AgatDisplay::AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    RasterDisplay(im, cd)
    , previous_mode(_FFFF)
    , blinker(false)
    , clock_counter(0)
    , m_irq_val(1)
    , i_50hz(this, im, 1, "50hz", MODE_W)
    , i_500hz(this, im, 1, "500hz", MODE_W)
    , i_ints_en(this, im, 1, "ints_en", MODE_R)
{
    sx = 512; // Doubling 2*256 because of 64 chars mode;
    sy = 256;
}

void AgatDisplay::load_config(SystemData *sd)
{
    RasterDisplay::load_config(sd);

    port_mode = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    memory[0] = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    font =      dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));

    // memory[0]->set_memory_callback(this, 1, MODE_W);

    system_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;
    blink_ticks = system_clock / (5*2);     // 5 Hz

    i_50hz.change(1);
    i_500hz.change(1);

    set_mode(0x02);
}

void AgatDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(Agat_2Colors, Agat_RGBA2, 2);
    vr.FillRGB(Agat_16Colors, Agat_RGBA16, 16);
}

void AgatDisplay::set_mode(unsigned int new_mode)
{
    previous_mode = new_mode;
    mode = new_mode & 0x83;
    module = 0; //(new_mode & 0x80) >> 7;
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

void AgatDisplay::clock(unsigned int counter)
{
    RasterDisplay::clock(counter);

    uint8_t mode_value = port_mode->get_direct();
    if (previous_mode != mode_value) set_mode(mode_value);

    clock_counter += counter;
    if (clock_counter >= blink_ticks) {
        clock_counter -= blink_ticks;
        blinker = !blinker;
        if (mode == 0x02) screen_valid = false;
    }

    //screen_valid = false;
}

// void AgatDisplay::memory_callback(unsigned int callback_id, unsigned int address)
// {
//     if ( (address >= base_address) && (address < base_address + page_size) )
//     {
//         //TODO: Find out why local update doesn't work
//         //render_byte(address - base_address);
//         screen_valid = false;
//         was_updated = true;
//     }
// }

void AgatDisplay::render_byte(unsigned int address)
{
    //TODO: finish
    unsigned int line, offset, p, read_address, cl, inv;
    uint8_t color[2];
    uint8_t * pixel_address;
    uint8_t v1, v2;

    uint8_t v = memory[module]->get_value(base_address + address);
    switch (mode) {
        case 0x00:
            // ГНР (LoRes Graphics, 64x64, 16 colors)
            line = (address & ~0x1F) >> 3;
            offset = (address & 0x1F) * 64; // 8*4*2: 8 pixels, 4 bytes per pixel, doubling
            color[0] = v >> 4;
            color[1] = v & 0x0F;
            for (unsigned int j = 0; j < 2; j++)
                for (unsigned int k = 0; k < 8; k++) {
                    p = offset + j*32 + k*4;
                    for (unsigned int i = line; i <= line+3; i++) {
                        unsigned int c = color[j];
                        pixel_address = static_cast<uint8_t *>(render_pixels) + i*line_bytes + p;
                        *(uint32_t*)pixel_address = Agat_RGBA16[c]; //SDL_MapRGB(surface->format, Agat_16Colors[c][0], Agat_16Colors[c][1], Agat_16Colors[c][2]);
                    }
                }
            break;
        case 0x01:
            // ГCР (MidRes Graphics, 128x128, 16 colors)
            line = (address & ~0x3F) >> 5;
            offset = (address & 0x3F) * 32; // 8*4: 8 pixels, 4 bytes per pixel
            color[0] = v >> 4;
            color[1] = v & 0x0F;
            for (unsigned int j = 0; j < 2; j++)
                for (unsigned int k = 0; k < 4; k++) {
                    p = offset + j*16 + k*4;
                    for (unsigned int i = line; i <= line+1; i++) {
                        unsigned int c = color[j];
                        pixel_address = static_cast<uint8_t *>(render_pixels) + i*line_bytes + p;
                        *(uint32_t*)pixel_address = Agat_RGBA16[c]; //SDL_MapRGB(surface->format, Agat_16Colors[c][0], Agat_16Colors[c][1], Agat_16Colors[c][2]);
                    }
                }
            break;
        case 0x02:
            // АЦР-32 (Alphanumeric 32 chars with attributes, 16 colors)
            // Each position takes two bytes in memory - a character code and its attrubute value
            line = (address & ~0x3F) >> 3;
            offset = 32*4 + (address & 0x3E) * (7*4);           // 32 pixels blanking, 7 pixels per char, 4 bytes per pixel
            read_address = base_address + (address & ~1);
            v1 = memory[module]->get_value(read_address);       // Character
            v2 = memory[module]->get_value(read_address+1);     // Attribute
            cl = (v2 & 0x07) | ((v2 & 0x10) >> 1);              // Color index (YBGR)
            for (unsigned int i=0; i<8; i++) {
                uint8_t font_val = font->get_value(v1*8+i);
                for (unsigned int k=0; k<7; k++) {              // Char is 7x8 pixels
                    unsigned int c = (font_val >> k) & 0x01;
                    unsigned int ccl;
                    if ( (((v2 & 0x20) != 0) || ((v2 & 0x08) != 0) && blinker) )
                        ccl = cl * c;
                    else
                        ccl = cl * (c ^ 0x01);
                    uint32_t color = Agat_RGBA16[ccl]; //SDL_MapRGB(surface->format, Agat_16Colors[ccl][0], Agat_16Colors[ccl][1], Agat_16Colors[ccl][2]);
                    p = offset + (6-k)*8;
                    pixel_address = static_cast<uint8_t *>(render_pixels) + (line + i)*line_bytes + p;
                    *(uint32_t*)pixel_address = color;
                    *(uint32_t*)(pixel_address+4) = color;
                }
            }
            break;
        case 0x82:
            // АЦР-64 (Alphanumeric 64 chars, monochrome)
            //TODO: АЦР-64
            line = (address & ~0x3F) >> 3;
            offset = 32*4 + (address & 0x3F) * (7*4);                   // 32 pixels blanking, 7 pixels per char, 4 bytes per pixel
            read_address = base_address + address;
            v1 = memory[0]->get_value(read_address);                    // Character only, getting from the main memory
            inv = ((~previous_mode & 0x04) >> 2);                       // inverted mode
            for (unsigned int i=0; i<8; i++) {
                uint8_t font_val = font->get_value(v1*8+i);
                for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                    unsigned int c = ((font_val >> k) & 1) ^ inv;
                    p = offset + (6-k)*4;
                    pixel_address = static_cast<uint8_t *>(render_pixels) + (line + i)*line_bytes + p;
                    *(uint32_t*)pixel_address = Agat_RGBA2[c]; //SDL_MapRGB(surface->format, Agat_2Colors[c][0], Agat_2Colors[c][1], Agat_2Colors[c][2]);
                }
            }
            break;
        case 0x03:
            // ГВР (HiRes Graphics, monochrome 256x256)
            // Filling two pixels per bit because of doubling
            line = address >> 5;
            offset = (address & 0x1F) * 64; // 8*4*2: 8 pixels, 4 bytes per pixel, doubling
            for (unsigned int k = 0; k < 8; k++) {
                unsigned int c = (v >> k) & 0x01;
                uint32_t color = Agat_RGBA2[c]; //SDL_MapRGB(surface->format, Agat_2Colors[c][0], Agat_2Colors[c][1], Agat_2Colors[c][2]);
                p = offset + (7-k)*8;
                pixel_address = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p;
                *(uint32_t*)pixel_address = color;
                *(uint32_t*)(pixel_address+4) = color;
            }
            break;
        default:
            break;
    }

}

void AgatDisplay::render_line(unsigned int line)
{
    uint8_t * pixel_address;
    unsigned int p, screen_offset, font_line, char_address, inv;
    uint8_t v, v1, v2, font_val;
    uint8_t color[2];

    switch (mode) {
    case 0x00:
        // ГНР (LoRes Graphics, 64x64, 16 colors)
        // 32 bytes per line
        for (unsigned int i=0; i<32; i++) {
            char_address = base_address + (line/4)*32 + i;
            v = memory[module]->get_value(char_address);
            color[0] = v >> 4;
            color[1] = v & 0x0F;
            screen_offset = i * 64;
            for (unsigned int j = 0; j < 2; j++) {
                unsigned int c = color[j];
                for (unsigned int k = 0; k < 8; k++) {
                    p = screen_offset + j*32 + k*4;

                    pixel_address = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p;
                    *(uint32_t*)pixel_address = Agat_RGBA16[c];
                }
            }
        }
        break;

    case 0x01:
        // ГCР (MidRes Graphics, 128x128, 16 colors)
        // 64 bytes per line
        for (unsigned int i=0; i<64; i++) {
            char_address = base_address + (line/2)*64 + i;
            v = memory[module]->get_value(char_address);
            color[0] = v >> 4;
            color[1] = v & 0x0F;
            screen_offset = i * 32;
            for (unsigned int j = 0; j < 2; j++)
                for (unsigned int k = 0; k < 4; k++) {
                    p = screen_offset + j*16 + k*4;
                    unsigned int c = color[j];
                    pixel_address = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p;
                    *(uint32_t*)pixel_address = Agat_RGBA16[c];
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
            *(uint32_t *)(pixel_address + j*4) = Agat_RGBA2[0];
            *(uint32_t *)(pixel_address + 480*4 + j*4) = Agat_RGBA2[0];
        }

        font_line = line % 8;
        for (unsigned int i=0; i<32; i++) {
            char_address = base_address + (line/8)*64 + i*2;
            v1 = memory[module]->get_value(char_address);       // Character
            v2 = memory[module]->get_value(char_address+1);     // Attribute
            unsigned int cl = (v2 & 0x07) | ((v2 & 0x10) >> 1);         // Color index (YBGR)
            font_val = font->get_value(v1*8 + font_line);

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
                pixel_address = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p;
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
            *(uint32_t *)(pixel_address + j*4) = Agat_RGBA2[0];
            *(uint32_t *)(pixel_address + 480*4 + j*4) = Agat_RGBA2[0];
        }

        inv = ((~previous_mode & 0x04) >> 2);                       // inverted mode
        font_line = line % 8;
        for (unsigned int i=0; i<64; i++) {
            char_address = base_address + (line/8)*64 + i;
            v1 = memory[module]->get_value(char_address);       // Character
            font_val = font->get_value(v1*8 + font_line);
            screen_offset = 32*4 + i * (7*4);
            for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                unsigned int c = ((font_val >> k) & 1) ^ inv;
                p = screen_offset + (6-k)*4;
                pixel_address = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p;
                *(uint32_t*)pixel_address = Agat_RGBA2[c];
            }
        }
        break;

    case 0x03:
        // ГВР (HiRes Graphics, monochrome 256x256)
        // Filling two pixels per bit because of doubling

        for (unsigned int i=0; i<32; i++) {
            char_address = base_address + line*32 + i;
            v = memory[module]->get_value(char_address);
            screen_offset = i * 64;

            for (unsigned int k = 0; k < 8; k++) {
                unsigned int c = (v >> k) & 0x01;
                uint32_t color = Agat_RGBA2[c];
                p = screen_offset + (7-k)*8;
                pixel_address = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p;
                *(uint32_t*)pixel_address = color;
                *(uint32_t*)(pixel_address+4) = color;
            }
        }
        break;
    }
}


void AgatDisplay::render_all(bool force_render)
{
    if (!screen_valid || force_render)
    {
        // if ((mode & 0x03) == 2) {
        //     // Blanking fields in text modes
        //     uint8_t * pixel_address;
        //     uint32_t black = renderer->MapRGB(0, 0, 0);
        //     for (unsigned int i=0; i<256; i++) {
        //         pixel_address = ((uint8_t *)render_pixels) + i*line_bytes;
        //         //memset(pixel_address, 10, 32*4);         // Left
        //         //memset(pixel_address + 480*4, 10, 32*4); // Right
        //         for (unsigned int j=0; j<32; j++) {
        //             *(uint32_t *)(pixel_address + j*4) = black;
        //             *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
        //         }
        //     }
        // }
        // for (unsigned int i=0; i<page_size; i++) render_byte(i);
        screen_valid = true;
        was_updated = true;
    }
}

void AgatDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void AgatDisplay::VSYNC(unsigned sync_val)
{
    if ((i_ints_en.value & 1) == 0) i_50hz.change(sync_val);
}

void AgatDisplay::HSYNC(unsigned line, unsigned sync_val)
{
    if ((i_ints_en.value & 1) == 0) {
        if (sync_val == 0) {
            unsigned irq_val = (line >> 5) & 1;
            if (irq_val != m_irq_val) {
                i_500hz.change(irq_val);
                m_irq_val = irq_val;
            }
        }
    }
    int screen_line = static_cast<int>(line - m_top_blank) / 2;
    if (screen_line >= 0 && screen_line < 256)
        render_line(screen_line);
}

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AgatDisplay(im, cd);
}
