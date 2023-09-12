#include "keyboard.h"

Keyboard::Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{}

unsigned int Keyboard::translate_key(QString key)
{
    //TODO: Implement
}
