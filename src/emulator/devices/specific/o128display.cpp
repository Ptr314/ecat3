// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Orion-128 display controller device

#include <cstring>

#include "emulator/utils.h"
#include "o128display.h"

//TODO: move to DWORD
uint8_t Orion128_MonoColors[4][3] = {
                                        {  0,   0,   0}, {  0, 255,   0}, { 40, 180, 200}, {250, 250,  50}
                                    };
uint8_t Orion128_4Colors[8][3] =    {
                                        {  0,   0,   0}, {127,   0,   0}, {  0, 127,   0}, {  0,   0, 127},
                                        {127, 127, 127}, {  0, 127, 127}, {127,   0, 127}, {127, 127,   0}
                                    };
uint8_t Orion128_16Colors[16][3] = {
                                        {  0,   0,   0}, {  0,   0, 127}, {  0, 127,   0}, {  0, 127, 127},
                                        {127,   0,   0}, {127,   0, 127}, {127, 127,   0}, {127, 127, 127},
                                        {127, 127, 127}, {  0,   0, 255}, {  0, 255,   0}, {  0, 255, 255},
                                        {255,   0,   0}, {255,   0, 255}, {255, 255,   0}, {255, 255, 255}
                                    };

O128Display::O128Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd),
    mode(_FFFF),
    frame(_FFFF)
{
    m_clocked = true;   //clock() is overridden here
    sx = 384;
    sy = 256;
}

emulator::Result O128Display::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    port_mode =  dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    port_frame = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("screen").value));
    page_main =  dynamic_cast<RAM*> (im->dm->get_device_by_name(cd->get_parameter("rmain").value));
    page_color = dynamic_cast<RAM*> (im->dm->get_device_by_name(cd->get_parameter("color").value));

    page_main->set_memory_callback(this, 1, MODE_W);
    page_color->set_memory_callback(this, 2, MODE_W);

    return emulator::Result::ok();
}

void O128Display::memory_callback(unsigned int callback_id, unsigned int address)
{
    if ( (address >= base_address) && (address < base_address + 0x3000) )
    {
        //TODO: Find out why local update doesn't work
        //render_byte(address - base_address);
        screen_valid = false;
        was_updated = true;
    }
}

void O128Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

//The renderer decides the pixel format, so the palettes are converted once
//it is known - not per pixel through the virtual MapRGB()
void O128Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(Orion128_MonoColors, rgba_mono, 4);
    vr.FillRGB(Orion128_4Colors, rgba_4colors, 8);
    vr.FillRGB(Orion128_16Colors, rgba_16colors, 16);
}

void O128Display::clock(unsigned int counter)
{
    //get_direct(), not get_value(): this runs on every instruction, and reading
    //a port through get_value() pulses its access line. Irisha and BK read the
    //same way here
    const unsigned int new_mode = port_mode->get_direct(0);
    const unsigned int new_frame = port_frame->get_direct(0);
    if ( (mode != new_mode) || (frame != new_frame))
    {
        mode = new_mode;
        frame = new_frame;
        base_address = 0xC000 - frame * 0x4000;
        screen_valid = false;
        was_updated = true;
    }
}

void O128Display::render_all(bool force_render)
{
    if (!screen_valid || force_render)
    {
        //Marked valid before the repaint, not after: the emulation thread
        //clears the flag on every write to the video memory, and a write
        //arriving while this loop runs would be swallowed by an assignment
        //at the end - that byte would stay unpainted until something else
        //touched the screen
        screen_valid = true;
        was_updated = true;
        for (unsigned int a=0; a < 0x3000; a++) render_byte(a);
    }
}

void O128Display::render_byte(unsigned int address)
{
    unsigned int line = address & 0xFF;
    unsigned int offset = (address >> 8) * 32; //Each screen bit takes 4 bytes (ARGB)

    unsigned int p1;
    uint8_t mode0, c, c1, c2, c3, c4;
    uint8_t mode1 = mode & 0x02;
    uint8_t mode2 = mode & 0x04;
    uint8_t * base;

    //qDebug() << Qt::hex << address;


    if (mode2 == 0)
    {
        if (mode1 == 0)
        {
            //Mono
            mode0 = (mode & 0x01) << 1;
            c = page_main->get_value(base_address + address);
            for (int k = 0; k < 8; k++)
            {
                c1 = ((c >> k) & 0x01) | mode0;
                p1 = offset + (7-k)*4;
                base = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p1;
                //base[0] = Orion128_MonoColors[c1][2];
                //base[1] = Orion128_MonoColors[c1][1];
                //base[2] = Orion128_MonoColors[c1][0];
                *(uint32_t*)base = rgba_mono[c1];
            }
        } else {
            //Blanking
            base = ((uint8_t *)render_pixels) + line*line_bytes + offset;
            memset(base, 0, 32);
        }
    } else {
        if (mode1 == 2 )
        {
            //16 colors
            c = page_main->get_value(base_address + address);
            c2 = page_color->get_value(base_address + address);
            for (int k = 0; k < 8; k++)
            {
                c1 = (~(c >> k)) & 1;
                c3 = (c2 >> (4*c1)) & 0x0F; //main color - lower 4 bits, background - higher
                p1 = offset + (7-k)*4;
                base = ((uint8_t *)render_pixels) + line*line_bytes + p1;
                //base[0] = Orion128_16Colors[c3][2];
                //base[1] = Orion128_16Colors[c3][1];
                //base[2] = Orion128_16Colors[c3][0];
                *(uint32_t*)base = rgba_16colors[c3];
            }
        } else {
            //4 colors
            mode0 = (mode & 0x01) << 2;
            c = page_main->get_value(base_address + address);
            c1 = page_color->get_value(base_address + address);
            for (int k = 0; k < 8; k++)
            {
                c2 = (c >> k) & 0x01;
                c3 = (c1 >> k) & 0x01;
                c4 = ((c2 << 1) | c3) | mode0;
                p1 = offset + (7-k)*4;
                base = ((uint8_t *)render_pixels) + line*line_bytes + p1;
                //base[0] = Orion128_4Colors[c4][2];
                //base[1] = Orion128_4Colors[c4][1];
                //base[2] = Orion128_4Colors[c4][0];
                *(uint32_t*)base = rgba_4colors[c4];
            }
        }
    }
}

std::vector<DeviceFieldInfo> O128Display::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"mode",  "Value of the mode port: colour and width",  false});
    r.push_back({"frame", "Video page currently shown",                false});
    r.push_back({"base",  "Address the shown page starts at",          false});
    return r;
}

bool O128Display::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "mode" || field == "frame")
    {
        out.numeric = true;
        out.values.push_back((field == "mode") ? mode : frame);
        return true;
    }

    if (field == "base")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(base_address);
        return true;
    }

    return GenericDisplay::get_field(field, from, to, out);
}

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new O128Display(im, cd);
}
