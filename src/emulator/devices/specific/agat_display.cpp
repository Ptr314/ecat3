#include "agat_display.h"

AgatDisplay::AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd)
{
    // TODO: implement
    sx = 512;
    sy = 256;

}

void AgatDisplay::render_all(bool force_render)
{
    // TODO: implement
    if (!screen_valid || force_render)
    {
        //for (unsigned int a=0; a < 0x3000; a++) render_byte(a);
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
