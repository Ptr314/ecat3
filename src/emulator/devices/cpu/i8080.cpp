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

uint8_t I8080Core::read_port(uint16_t address)
{
    return emulator_device->read_port(address);
}

void I8080Core::write_port(uint16_t address, uint8_t value)
{
    emulator_device->write_port(address, value);
}

//----------------------- Emulator device -----------------------------------

I8080::I8080(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    i_address = this->create_interface(16, "address", MODE_R, 1);
    i_data =    this->create_interface(8, "data", MODE_RW);
    i_nmi =     this->create_interface(1, "nmi", MODE_R);
    i_int =     this->create_interface(1, "int", MODE_R);
    i_inte =    this->create_interface(1, "inte", MODE_W);
    i_m1 =      this->create_interface(1, "m1", MODE_W);

    core = new I8080Core(this);
}

unsigned int I8080::get_pc()
{
    return core->get_context()->registers.regs.PC;
}

unsigned int I8080::read_mem(unsigned int address)
{
    return mm->read(address);
}

void I8080::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

unsigned int I8080::read_port(unsigned int address)
{
    return mm->read_port(address);
}

void I8080::write_port(unsigned int address, unsigned int data)
{
    mm->write_port(address, data);
}

void I8080::reset(bool cold)
{
    CPU::reset(cold);
}

void I8080::inte_changed(unsigned int inte)
{
    i_inte->change(inte);
}

QList<QString> I8080::get_registers()
{
    QList<QString> l;
    i8080context * c = core->get_context();

    l << QString("A=%1").arg(c->registers.regs.A, 2, 16, QChar('0')).toUpper()
      << QString("BC=%1").arg(c->registers.reg_pairs.BC, 4, 16, QChar('0')).toUpper()
      << QString("DE=%1").arg(c->registers.reg_pairs.DE, 4, 16, QChar('0')).toUpper()
      << QString("HL=%1").arg(c->registers.reg_pairs.HL, 4, 16, QChar('0')).toUpper()
      << ""
      << QString("SP=%1").arg(c->registers.regs.SP, 4, 16, QChar('0')).toUpper()
      << QString("PC=%1").arg(c->registers.regs.PC, 4, 16, QChar('0')).toUpper()
    ;
    return l;
}

QList<QString> I8080::get_flags()
{
    QList<QString> l;
    i8080context * c = core->get_context();

    l << QString("C=%1").arg( ((c->registers.regs.F & F_CARRY) != 0)?1:0)
      << QString("P=%1").arg( ((c->registers.regs.F & F_PARITY) != 0)?1:0)
      << QString("HC=%1").arg( ((c->registers.regs.F & F_HALF_CARRY) != 0)?1:0)
      << QString("Z=%1").arg( ((c->registers.regs.F & F_ZERO) != 0)?1:0)
      << QString("S=%1").arg( ((c->registers.regs.F & F_SIGN) != 0)?1:0)
    ;
    return l;
}


unsigned int I8080::execute()
{
    //TODO: use HALT imitation
    if (debug == DEBUG_STOPPED)
        return 10;

    unsigned int cycles = core->execute();

    switch (debug) {
    case DEBUG_STEP:
        debug = DEBUG_STOPPED;
        break;
    case DEBUG_BRAKES:
        if (check_breakpoint(get_pc())) debug = DEBUG_STOPPED;
        break;
    default:
        break;
    }

    return cycles;
}

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8080(im, cd);
}
