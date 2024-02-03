#ifndef REGISTER_H
#define REGISTER_H

#include "emulator/core.h"

class Register:public ComputerDevice
{
private:
    Interface * i_in;
    Interface * i_out;
    Interface * i_c;

    unsigned int register_value;
    unsigned int store_type;

public:
    Register(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void reset(bool cold) override;
    virtual void load_config(SystemData *sd) override;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
};

ComputerDevice * create_register(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // REGISTER_H
