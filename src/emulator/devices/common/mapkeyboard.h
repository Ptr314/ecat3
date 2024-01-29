#ifndef MAPKEYBOARD_H
#define MAPKEYBOARD_H

#include "emulator/devices/common/keyboard.h"

struct KeyMapData {
    unsigned int key_code;
    unsigned int value;
    bool shift;
    bool ctrl;
};


class MapKeyboard: public Keyboard
{
private:
    Interface * i_ruslat;

protected:
    bool shift_pressed;
    bool ctrl_pressed;
    unsigned int ruslat_state;
    unsigned int code_ruslat;
    unsigned int ruslat_bit;

    Port * port_value;
    Port * port_ruslat;

    KeyMapData key_map[1000];
    unsigned int key_map_len;

    void set_ruslat(unsigned int value);

public:
    MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void key_down(unsigned int key) override;
    virtual void key_up(unsigned int key) override;

    virtual void load_config(SystemData *sd) override;
};

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // MAPKEYBOARD_H
