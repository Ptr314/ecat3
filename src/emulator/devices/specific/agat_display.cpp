#include "agat_display.h"
#include "emulator/utils.h"

uint8_t Agat_2Colors[2][3] =  {
                    {0, 0, 0},
                    {255, 255, 255}
        };

uint32_t Agat_RGBA2[2];

uint8_t Agat_16Colors[16][3]  = {
                    {  0,   0,   0}, {127,   0,   0}, {  0, 127,   0}, {127, 127,   0},
                    {  0,   0, 127}, {127,   0, 127}, {  0, 127, 127}, {255, 255, 255},
                    {  0,   0, 	 0}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
                    {  0,   0, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255}
        };

uint32_t Agat_RGBA16[16];

AgatDisplay::AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd),
    previous_mode(_FFFF),
    blinker(false),
    clock_counter(0)
{
    sx = 512; // Doubling 2*256 because of 64 chars mode;
    sy = 256;
}

void AgatDisplay::load_config(SystemData *sd)
{
    port_mode = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("mode").value));
    memory[0] = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram1").value));
    memory[1] = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram2").value));
    font =      dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));

    memory[0]->set_memory_callback(this, 1, MODE_W);
    memory[1]->set_memory_callback(this, 2, MODE_W);

    system_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;
    blink_ticks = system_clock / (5*2);     // 5 Hz

    set_mode(0x02);
}

void AgatDisplay::set_surface(SDL_Surface * surface)
{
    GenericDisplay::set_surface(surface);
    fill_SDL_rgba(Agat_2Colors, Agat_RGBA2, 2, surface->format);
    fill_SDL_rgba(Agat_16Colors, Agat_RGBA16, 16, surface->format);
}

void AgatDisplay::set_mode(unsigned int new_mode)
{
    previous_mode = new_mode;
    mode = new_mode & 0x83;
    module = (new_mode & 0x80) >> 7;
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
    uint8_t mode_value = port_mode->get_value(0);
    if (previous_mode != mode_value) set_mode(mode_value);

    clock_counter += counter;
    if (clock_counter >= blink_ticks) {
        clock_counter -= blink_ticks;
        blinker = !blinker;
        if (mode == 0x02) screen_valid = false;
    }

    //screen_valid = false;
}

void AgatDisplay::memory_callback(unsigned int callback_id, unsigned int address)
{
    if ( (address >= base_address) && (address < base_address + page_size) )
    {
        //TODO: Find out why local update doesn't work
        //render_byte(address - base_address);
        screen_valid = false;
        was_updated = true;
    }
}

void AgatDisplay::render_byte(unsigned int address)
{
    //TODO: finish
    unsigned int line, offset, p, read_address, cl;
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
                for (unsigned int k = 0; k < 7; k++) {
                    p = offset + j*32 + k*4;
                    for (unsigned int i = line; i <= line+3; i++) {
                        unsigned int c = color[j];
                        pixel_address = static_cast<Uint8 *>(render_pixels) + i*line_bytes + p;
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
                for (unsigned int k = 0; k < 7; k++) {
                    p = offset + j*16 + k*4;
                    for (unsigned int i = line; i <= line+1; i++) {
                        unsigned int c = color[j];
                        pixel_address = static_cast<Uint8 *>(render_pixels) + i*line_bytes + p;
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
                    pixel_address = static_cast<Uint8 *>(render_pixels) + (line + i)*line_bytes + p;
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
            for (unsigned int i=0; i<8; i++) {
                uint8_t font_val = font->get_value(v1*8+i);
                for (unsigned int k=0; k<7; k++) {                      // Char is 7x8 pixels
                    unsigned int c = ((font_val >> k) & 1)
                                     ^ ((previous_mode & 0x04) >> 2);   // odd pages are inverted
                    p = offset + (6-k)*4;
                    pixel_address = static_cast<Uint8 *>(render_pixels) + (line + i)*line_bytes + p;
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
                pixel_address = static_cast<Uint8 *>(render_pixels) + line*line_bytes + p;
                *(uint32_t*)pixel_address = color;
                *(uint32_t*)(pixel_address+4) = color;
            }
            break;
        default:
            break;
    }

}

void AgatDisplay::render_all(bool force_render)
{
    if (!screen_valid || force_render)
    {
        if ((mode & 0x03) == 2) {
            // Blanking fields in text modes
            uint8_t * pixel_address;
            uint32_t black = SDL_MapRGB(surface->format, 0, 0, 0);
            for (unsigned int i=0; i<256; i++) {
                pixel_address = ((Uint8 *)render_pixels) + i*line_bytes;
                //memset(pixel_address, 10, 32*4);         // Left
                //memset(pixel_address + 480*4, 10, 32*4); // Right
                for (unsigned int j=0; j<32; j++) {
                    *(uint32_t *)(pixel_address + j*4) = black;
                    *(uint32_t *)(pixel_address + 480*4 + j*4) = black;
                }
            }
        }
        for (unsigned int i=0; i<page_size; i++) render_byte(i);
        screen_valid = true;
        was_updated = true;
    }
}

void AgatDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AgatDisplay(im, cd);
}
