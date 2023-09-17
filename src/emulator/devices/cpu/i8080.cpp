#include "i8080.h"

//----------------------- Library wrapper -----------------------------------
I8080Core::I8080Core(I8080 * emulator_device):
    i8080core()
{
    this->emulator_device = emulator_device;
}

uint8_t I8080Core::read_mem(uint16_t address)
{
    return emulator_device->read_mem(address);
}

void I8080Core::write_mem(uint16_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
}

//----------------------- Emulator device -----------------------------------

I8080::I8080(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    this->i_address = this->create_interface(16, "address", MODE_R, 1);
    this->i_data =    this->create_interface(8, "data", MODE_RW);
    this->i_nmi =     this->create_interface(1, "nmi", MODE_R);
    this->i_int =     this->create_interface(1, "int", MODE_R);
    this->i_inte =    this->create_interface(1, "inte", MODE_W);
    this->i_m1 =      this->create_interface(1, "m1", MODE_W);

    core = new I8080Core(this);
}

unsigned int I8080::get_pc()
{
    return 0;
}

unsigned int I8080::read_mem(unsigned int address)
{
    return mm->read(address);
}

void I8080::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

void I8080::reset(bool cold)
{
    CPU::reset(cold);
}

unsigned int I8080::execute()
{
    //TODO: 8080 Implement
    //TODO: 8080 debugging stuff
    //return 10;

}

unsigned int read_mem(unsigned int address)
{

}

void write_mem(unsigned int address, unsigned int data)
{

}

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8080(im, cd);
}
