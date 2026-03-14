// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Irisha display controller device

#include "emulator/utils.h"
#include "irisha_display.h"

IrishaDisplay::IrishaDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd),
    m_mode(0)
{
    sx = 320;
    sy = 200;
}

void IrishaDisplay::load_config(SystemData *sd)
{
    GenericDisplay::load_config(sd);

    port_mode  = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    port_color = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("color").value));
    port_page  = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("page").value));
    vram =  dynamic_cast<RAM*> (im->dm->get_device_by_name(cd->get_parameter("vram").value));

    vram->set_memory_callback(this, 1, MODE_W);

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
    // TODO: check all other stuff
    if ( (m_mode != port_mode->get_direct(0)))
    {
        m_mode = port_mode->get_direct(0);
        m_base_address = 0x0000;
        screen_valid = false;
        was_updated = true;
    }
}

void IrishaDisplay::render_all(bool force_render)
{
    if (!screen_valid || force_render)
    {
        // TODO: render all here
        // for (unsigned int a=0; a < 0x3000; a++) render_byte(a);
        screen_valid = true;
        was_updated = true;
    }
}

void IrishaDisplay::render_byte(unsigned int address)
{
    // unsigned int line = address & 0xFF;
    // unsigned int offset = (address >> 8) * 32; //Each screen bit takes 4 bytes (ARGB)
    //
    // unsigned int p1;
    // uint8_t mode0, c, c1, c2, c3, c4;
    // uint8_t mode1 = mode & 0x02;
    // uint8_t mode2 = mode & 0x04;
    // uint8_t * base;
    //
    // //qDebug() << Qt::hex << address;
    //
    //
    // if (mode2 == 0)
    // {
    //     if (mode1 == 0)
    //     {
    //         //Mono
    //         mode0 = (mode & 0x01) << 1;
    //         c = page_main->get_value(base_address + address);
    //         for (int k = 0; k < 8; k++)
    //         {
    //             c1 = ((c >> k) & 0x01) | mode0;
    //             p1 = offset + (7-k)*4;
    //             base = static_cast<uint8_t *>(render_pixels) + line*line_bytes + p1;
    //             //base[0] = Orion128_MonoColors[c1][2];
    //             //base[1] = Orion128_MonoColors[c1][1];
    //             //base[2] = Orion128_MonoColors[c1][0];
    //             *(uint32_t*)base = renderer->MapRGB(Orion128_MonoColors[c1][0], Orion128_MonoColors[c1][1], Orion128_MonoColors[c1][2]);
    //         }
    //     } else {
    //         //Blanking
    //         base = ((uint8_t *)render_pixels) + line*line_bytes + offset;
    //         memset(base, 0, 32);
    //     }
    // } else {
    //     if (mode1 == 2 )
    //     {
    //         //16 colors
    //         c = page_main->get_value(base_address + address);
    //         c2 = page_color->get_value(base_address + address);
    //         for (int k = 0; k < 8; k++)
    //         {
    //             c1 = (~(c >> k)) & 1;
    //             c3 = (c2 >> (4*c1)) & 0x0F; //main color - lower 4 bits, background - higher
    //             p1 = offset + (7-k)*4;
    //             base = ((uint8_t *)render_pixels) + line*line_bytes + p1;
    //             //base[0] = Orion128_16Colors[c3][2];
    //             //base[1] = Orion128_16Colors[c3][1];
    //             //base[2] = Orion128_16Colors[c3][0];
    //             *(uint32_t*)base = renderer->MapRGB(Orion128_16Colors[c3][0], Orion128_16Colors[c3][1], Orion128_16Colors[c3][2]);
    //         }
    //     } else {
    //         //4 colors
    //         mode0 = (mode & 0x01) << 2;
    //         c = page_main->get_value(base_address + address);
    //         c1 = page_color->get_value(base_address + address);
    //         for (int k = 0; k < 8; k++)
    //         {
    //             c2 = (c >> k) & 0x01;
    //             c3 = (c1 >> k) & 0x01;
    //             c4 = ((c2 << 1) | c3) | mode0;
    //             p1 = offset + (7-k)*4;
    //             base = ((uint8_t *)render_pixels) + line*line_bytes + p1;
    //             //base[0] = Orion128_4Colors[c4][2];
    //             //base[1] = Orion128_4Colors[c4][1];
    //             //base[2] = Orion128_4Colors[c4][0];
    //             *(uint32_t*)base = renderer->MapRGB(Orion128_4Colors[c4][0], Orion128_4Colors[c4][1], Orion128_4Colors[c4][2]);
    //         }
    //     }
    // }
}

ComputerDevice * create_irisha_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IrishaDisplay(im, cd);
}
