#ifndef I8255_H
#define I8255_H

#include "emulator/core.h"

#define PORT_A   1
#define PORT_B   2
#define PORT_CH  3
#define PORT_CL  4

class I8255:public AddressableDevice
{
private:
    Interface * i_address;
    Interface * i_data;
    Interface * i_port_a;
    Interface * i_port_b;
    Interface * i_port_ch;
    Interface * i_port_cl;

    uint8_t registers [4];

public:
    I8255(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value);
    virtual unsigned int get_value(unsigned int address);
    virtual void set_value(unsigned int address, unsigned int value);
    virtual void reset(bool cold);

};

ComputerDevice * create_i8255(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8255_H
