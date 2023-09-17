#ifndef I8080_H
#define I8080_H

#include "emulator/core.h"
#include "emulator/devices/cpu/i8080core.h"

class I8080;

//Library wrapper
class I8080Core: public i8080core
{
private:
    I8080 * emulator_device;

public:
    I8080Core(I8080 * emulator_device);
    virtual uint8_t read_mem(uint16_t address);
    virtual void write_mem(uint16_t address, uint8_t value);
};

//Emulator class
class I8080: public CPU
{
private:
    Interface * i_nmi;
    Interface * i_int;
    Interface * i_inte;
    Interface * i_m1;

    i8080core * core;

protected:
    virtual unsigned int get_pc();

public:
    I8080(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void reset(bool cold);
    virtual unsigned int execute();
    virtual unsigned int read_mem(unsigned int address);
    virtual void write_mem(unsigned int address, unsigned int data);

};

unsigned int read_mem(unsigned int address);
void write_mem(unsigned int address, unsigned int data);

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8080_H
