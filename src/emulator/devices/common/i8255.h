#ifndef I8255_H
#define I8255_H

#include "emulator/core.h"

class I8255:public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_port_a;
    Interface i_port_b;
    Interface i_port_ch;
    Interface i_port_cl;

    uint8_t registers [4];

public:
    I8255(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
    virtual void reset(bool cold) override;

};

ComputerDevice * create_i8255(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8255_H
