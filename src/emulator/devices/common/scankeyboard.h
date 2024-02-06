#ifndef SCANKEYBOARD_H
#define SCANKEYBOARD_H

#include "emulator/devices/common/keyboard.h"

struct ScanData {
    unsigned int key_code;
    unsigned int scan_line;
    unsigned int out_line;
};

class ScanKeyboard: public Keyboard
{
private:
    Interface * i_scan;
    Interface * i_output;
    Interface * i_shift;
    Interface * i_ctrl;
    Interface * i_ruslat;
    Interface * i_ruslat_led;

    unsigned int scan_lines;
    unsigned int out_lines;

    unsigned int keys_count;
    ScanData scan_data[200];
    unsigned int key_array[15];

    unsigned int code_ctrl;
    unsigned int code_shift;
    unsigned int code_ruslat;

    void calculate_out();

protected:
    virtual void set_rus(bool new_rus) override;

public:
    ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    virtual void key_down(unsigned int key) override;
    virtual void key_up(unsigned int key) override;

    virtual void load_config(SystemData *sd) override;
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SCANKEYBOARD_H
