#ifndef SCANKEYBOARD_H
#define SCANKEYBOARD_H

#include "emulator/devices/common/keyboard.h"

class ScanKeyboard: public Keyboard
{
public:
    ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void key_down(unsigned int key);
    virtual void key_up(unsigned int key);
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SCANKEYBOARD_H
