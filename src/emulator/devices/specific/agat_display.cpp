#include "agat_display.h"

AgatDisplay::AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd)
{
    // TODO: implement
}

void render_all(bool force_render)
{
    // TODO: implement
}

void get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    // TODO: implement
}

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AgatDisplay(im, cd);
}
