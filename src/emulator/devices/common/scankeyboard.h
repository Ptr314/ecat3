#ifndef SCANKEYBOARD_H
#define SCANKEYBOARD_H

#include "emulator/devices/common/keyboard.h"

#define SCAN_CALLBACK 1

class ScanKeyboard: public Keyboard
{
    //TODO: ScanKeyboard: Implement
private:
    Interface * i_scan;
    Interface * i_output;
    Interface * i_shift;
    Interface * i_ctrl;
    Interface * i_ruslat;

    void calculate_out();

public:
    ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void key_down(unsigned int key);
    virtual void key_up(unsigned int key);
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SCANKEYBOARD_H
