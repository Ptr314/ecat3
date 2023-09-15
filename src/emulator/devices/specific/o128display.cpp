#include "o128display.h"

O128Display::O128Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    Display(im, cd)
{
    //TODO: 128Display: Implement
    this->sx = 384;
    this->sy = 256;
}

void O128Display::get_screen(bool required)
{
    //TODO: O128Display: Implement
}

void O128Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new O128Display(im, cd);
}
