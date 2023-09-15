#include "o128display.h"

O128Display::O128Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    Display(im, cd)
{
    //TODO: 128Display: Implement
}

void O128Display::get_screen(bool required)
{
    //TODO: O128Display: Implement
}

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new O128Display(im, cd);
}
