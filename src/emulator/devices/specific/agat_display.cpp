#include "agat_display.h"

AgatDisplay::AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd)
{
    // TODO: implement
}

void AgatDisplay::render_all(bool force_render)
{
    // TODO: implement
}

void AgatDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    // TODO: implement
    *sx = 100;
    *sy = 200;
}

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AgatDisplay(im, cd);
}
