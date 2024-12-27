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
    sx = 384;
    sy = 256;
}

void O128Display::load_config(SystemData *sd)
{
    GenericDisplay::load_config(sd);

    port_mode =  dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    port_frame = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("screen").value));
    page_main =  dynamic_cast<RAM*> (im->dm->get_device_by_name(cd->get_parameter("rmain").value));
    page_color = dynamic_cast<RAM*> (im->dm->get_device_by_name(cd->get_parameter("color").value));

    page_main->set_memory_callback(this, 1, MODE_W);
    page_color->set_memory_callback(this, 2, MODE_W);

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

void O128Display::clock(unsigned int counter)
{
    if ( (mode != port_mode->get_value(0)) || (frame != port_frame->get_value(0)))
    {
        mode = port_mode->get_value(0);
        frame = port_frame->get_value(0);
        base_address = 0xC000 - frame * 0x4000;
        screen_valid = false;
        was_updated = true;
    }
}

void O128Display::render_all(bool force_render)
{
    if (!screen_valid || force_render)
    {
        for (unsigned int a=0; a < 0x3000; a++) render_byte(a);
        screen_valid = true;
        was_updated = true;
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
                base = static_cast<Uint8 *>(render_pixels) + line*line_bytes + p1;
                //base[0] = Orion128_MonoColors[c1][2];
                //base[1] = Orion128_MonoColors[c1][1];
                //base[2] = Orion128_MonoColors[c1][0];
                *(uint32_t*)base = SDL_MapRGB(&pixel_format, Orion128_MonoColors[c1][0], Orion128_MonoColors[c1][1], Orion128_MonoColors[c1][2]);
            }
        } else {
            //Blanking
            base = ((Uint8 *)render_pixels) + line*line_bytes + offset;
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
                base = ((Uint8 *)render_pixels) + line*line_bytes + p1;
                //base[0] = Orion128_16Colors[c3][2];
                //base[1] = Orion128_16Colors[c3][1];
                //base[2] = Orion128_16Colors[c3][0];
                *(uint32_t*)base = SDL_MapRGB(&pixel_format, Orion128_16Colors[c3][0], Orion128_16Colors[c3][1], Orion128_16Colors[c3][2]);
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
                base = ((Uint8 *)render_pixels) + line*line_bytes + p1;
                //base[0] = Orion128_4Colors[c4][2];
                //base[1] = Orion128_4Colors[c4][1];
                //base[2] = Orion128_4Colors[c4][0];
                *(uint32_t*)base = SDL_MapRGB(&pixel_format, Orion128_4Colors[c4][0], Orion128_4Colors[c4][1], Orion128_4Colors[c4][2]);
            }
        }
    }
}

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new O128Display(im, cd);
}
