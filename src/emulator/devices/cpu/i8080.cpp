#include "i8080.h"

//----------------------- Library wrapper -------------------------------------
z80::fast_u8 i8080emulator::on_read(z80::fast_u16 addr)
{
    return emulator_device->read_mem(addr);
}

void i8080emulator::on_write(z80::fast_u16 addr, z80::fast_u8 n)
{
    emulator_device->write_mem(addr, n);
}

//----------------------- I8080 -------------------------------------

I8080::I8080(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    this->i_address = this->create_interface(16, "address", MODE_R, 1);
    this->i_data =    this->create_interface(8, "data", MODE_RW);
    this->i_nmi =     this->create_interface(1, "nmi", MODE_R);
    this->i_int =     this->create_interface(1, "int", MODE_R);
    this->i_inte =    this->create_interface(1, "inte", MODE_W);
    this->i_m1 =      this->create_interface(1, "m1", MODE_W);

    i8080_emulator = new i8080emulator(this);
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
    i8080_emulator->set_pc(0);
}

unsigned int I8080::execute()
{
    //TODO: 8080 Implement
    //return 10;
    i8080_emulator->on_step();

}

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8080(im, cd);
}
