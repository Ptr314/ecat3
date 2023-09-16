#ifndef I8080_H
#define I8080_H

#include "libs/z80.h"
#include "emulator/core.h"

class I8080;

//Library wrapper
class i8080emulator : public z80::i8080_cpu<i8080emulator> {
private:
    I8080 * emulator_device;
public:
    typedef z80::i8080_cpu<i8080emulator> base;

    i8080emulator(I8080 * emulator_device):emulator_device(emulator_device) {};

    z80::fast_u8 on_read(z80::fast_u16 addr);
    void on_write(z80::fast_u16 addr, z80::fast_u8 n);
};

struct I8080Context {
    //TODO: I8080: Implement
};

class I8080: public CPU
{
private:
    Interface * i_nmi;
    Interface * i_int;
    Interface * i_inte;
    Interface * i_m1;

    i8080emulator * i8080_emulator;

protected:
    virtual unsigned int get_pc();
public:
    I8080Context context;

    I8080(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void reset(bool cold);
    virtual unsigned int execute();
    virtual unsigned int read_mem(unsigned int address);
    virtual void write_mem(unsigned int address, unsigned int data);

};

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8080_H
