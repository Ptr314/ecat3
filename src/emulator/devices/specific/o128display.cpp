#include "o128display.h"

O128Display::O128Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    Display(im, cd)
{
    //TODO: Implement
}

void O128Display::get_screen(bool required)
{
    //TODO: Implement
}

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new O128Display(im, cd);
}
