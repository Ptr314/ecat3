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
    unsigned int code_ruslat;
    unsigned int ruslat_bit;
    unsigned int rus_value;

    Port * port_value;
    Port * port_ruslat;

    KeyMapData key_map[1000];
    unsigned int key_map_len;

    virtual void set_rus(bool new_rus) override;

public:
    MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void key_down(unsigned int key) override;
    virtual void key_up(unsigned int key) override;

    virtual void load_config(SystemData *sd) override;

    virtual void reset(bool cool) override;
};

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // MAPKEYBOARD_H
