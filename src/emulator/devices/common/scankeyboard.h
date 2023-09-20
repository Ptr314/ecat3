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

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    virtual void key_down(unsigned int key) override;
    virtual void key_up(unsigned int key) override;
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SCANKEYBOARD_H
