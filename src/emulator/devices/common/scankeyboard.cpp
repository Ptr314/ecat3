#include "scankeyboard.h"

ScanKeyboard::ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    Keyboard(im, cd)
{
    //TODO: Impelement
}

void ScanKeyboard::key_down(unsigned int key)
{
    //TODO: Impelement
}

void ScanKeyboard::key_up(unsigned int key)
{
    //TODO: Impelement
}


ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new ScanKeyboard(im, cd);
}
