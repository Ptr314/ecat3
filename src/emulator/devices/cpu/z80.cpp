// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Zilog Z80 emulator interface class

#include "z80.h"

#define CALLBACK_NMI    1
#define CALLBACK_INT    2

#ifndef EXTERNAL_Z80

//----------------------- Library wrapper -----------------------------------
z80Core::z80Core(z80 * emulator_device):
    z80core()
{
    this->emulator_device = emulator_device;
}

uint8_t z80Core::read_mem(uint16_t address)
{
    return emulator_device->read_mem(address);
}

void z80Core::write_mem(uint16_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
}

uint8_t z80Core::read_port(uint16_t address)
{
    return emulator_device->read_port(address);
}

void z80Core::write_port(uint16_t address, uint8_t value)
{
    emulator_device->write_port(address, value);
}

#endif

#ifdef EXTERNAL_Z80
//external funcs
unsigned char readByte(void* arg, unsigned short addr)
{
    return ((z80*)arg)->read_mem(addr);
}

// memory write request per 1 byte from CPU
void writeByte(void* arg, unsigned short addr, unsigned char value)
{
    ((z80*)arg)->write_mem(addr, value);
}

unsigned char inPort(void* arg, unsigned short port)
{
    return ((z80*)arg)->read_port(port);
}

void outPort(void* arg, unsigned short port, unsigned char value)
{
    ((z80*)arg)->write_port(port, value);
}
#endif

//----------------------- Emulator device -----------------------------------

z80::z80(InterfaceManager *im, EmulatorConfigDevice *cd):
      CPU(im, cd)
    , i_nmi(this, im, 1, "nmi", MODE_R, CALLBACK_NMI)
    , i_int(this, im, 1, "int", MODE_R, CALLBACK_INT)
    , i_m1(this, im, 1, "m1", MODE_W)
{
#ifndef EXTERNAL_Z80
    core = new z80Core(this);
#else
    core_ext = new Z80(readByte, writeByte, inPort, outPort, this);
    core_ext->reg.pair.A = core_ext->reg.pair.F = 0;
    core_ext->reg.SP = 0;
#endif
    //TODO: add new codes if expected
    over_commands.push_back(0xCD);
    over_commands.push_back(0xDD);
    over_commands.push_back(0xED);
    over_commands.push_back(0xFD);

}

z80::~z80()
{}

unsigned int z80::get_pc()
{
#ifndef EXTERNAL_Z80
    return core->get_context()->registers.regs.PC;
#else
    return core_ext->reg.PC;
#endif

}

unsigned int z80::read_mem(unsigned int address)
{
    return mm->read(address);
}

void z80::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

unsigned int z80::read_port(unsigned int address)
{
    unsigned int data = mm->read_port(address);
#ifdef LOG_CPU
    logs(QString("PORT(%1)=%2").arg(address, 4, 16, QChar('0')).arg(data, 2, 16, QChar('0')));
#endif
    return data;
}

void z80::write_port(unsigned int address, unsigned int data)
{
    mm->write_port(address, data);
#ifdef LOG_CPU
    logs(QString("PORT(%1)=%2").arg(address, 4, 16, QChar('0')).arg(data, 2, 16, QChar('0')));
#endif
}

void z80::reset(bool cold)
{
    CPU::reset(cold);
}

QList<QPair<QString, QString>> z80::get_registers()
{
    QList<QPair<QString, QString>> l;
#ifndef EXTERNAL_Z80
    z80context * c = core->get_context();

    l << QPair<QString, QString>("AF", QString("%1").arg((c->registers.regs.A << 8) + c->registers.regs.F, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("BC", QString("%1").arg(c->registers.reg_pairs.BC, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("DE", QString("%1").arg(c->registers.reg_pairs.DE, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("HL", QString("%1").arg(c->registers.reg_pairs.HL, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("IX", QString("%1").arg(c->registers.reg_pairs.IX, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("IY", QString("%1").arg(c->registers.reg_pairs.IY, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("-1", "")
      << QPair<QString, QString>("AF'", QString("%1").arg(c->registers.regs.AF_, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("BC'", QString("%1").arg(c->registers.regs.BC_, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("DE'", QString("%1").arg(c->registers.regs.DE_, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("HL'", QString("%1").arg(c->registers.regs.HL_, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("-2", "")
      << QPair<QString, QString>("SP", QString("%1").arg(c->registers.regs.SP, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("PC", QString("%1").arg(c->registers.regs.PC, 4, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("-3", "")
      << QPair<QString, QString>("I", QString("%1").arg(c->registers.regs.I, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("R", QString("%1").arg(c->registers.regs.R, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("IFF1", QString("%1").arg(c->IFF1, 1, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("IFF2", QString("%1").arg(c->IFF2, 1, 16, QChar('0')).toUpper())
    ;
#else
#endif

    return l;
}

QList<QPair<QString, QString>> z80::get_flags()
{
    QList<QPair<QString, QString>> l;
#ifndef EXTERNAL_Z80
    z80context * c = core->get_context();

    l << QPair<QString, QString>("S",  QString("%1").arg( ((c->registers.regs.F & F_SIGN) != 0)?1:0))
      << QPair<QString, QString>("Z",  QString("%1").arg( ((c->registers.regs.F & F_ZERO) != 0)?1:0))
      << QPair<QString, QString>("5",  QString("%1").arg( ((c->registers.regs.F & F_B5) != 0)?1:0))
      << QPair<QString, QString>("H",  QString("%1").arg( ((c->registers.regs.F & F_HALF_CARRY) != 0)?1:0))
      << QPair<QString, QString>("3",  QString("%1").arg( ((c->registers.regs.F & F_B3) != 0)?1:0))
      << QPair<QString, QString>("PV", QString("%1").arg( ((c->registers.regs.F & F_PARITY) != 0)?1:0))
      << QPair<QString, QString>("N",  QString("%1").arg( ((c->registers.regs.F & F_SUB) != 0)?1:0))
      << QPair<QString, QString>("C",  QString("%1").arg( ((c->registers.regs.F & F_CARRY) != 0)?1:0))
        ;
#else
#endif
    return l;
}


unsigned int z80::execute()
{
    if (reset_mode)
    {
#ifndef EXTERNAL_Z80
        core->reset();
#else
        core_ext->reg.PC=0;
#endif
        reset_mode = false;
    }

    //TODO: use HALT imitation
    if (debug == DEBUG_STOPPED)
        return 10;

#ifdef LOG_CPU
    uint8_t log_cmd = get_command();
    bool do_log = true;
    //uint16_t address = get_pc();

    //do_log = (address < 0xF800) && (log_count++ < 2000000);
    //do_log = log_count++ < 100000;
    if (do_log) log_state(log_cmd, true);
#endif

#ifndef EXTERNAL_Z80
    unsigned int cycles = core->execute();
#else
    unsigned int cycles = core_ext->execute(1);
#endif

#ifdef LOG_CPU
    if (do_log) log_state(log_cmd, false, cycles);
#endif


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

void z80::set_context_value(QString name, unsigned int value)
{
#ifndef EXTERNAL_Z80
    if (name == "PC")
    {
        core->get_context()->registers.regs.PC = value;
    }
#else
    if (name == "PC")
    {
        core_ext->reg.PC = value;
    }
#endif

}

unsigned int z80::get_command()
{
#ifndef EXTERNAL_Z80
    return core->get_command();
#else
    return read_mem(core_ext->reg.PC);
#endif
}

void z80::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_NMI:
#ifndef EXTERNAL_Z80
        core->set_nmi(new_value & 1);
#else
#endif
        break;
    case CALLBACK_INT:
#ifndef EXTERNAL_Z80
        core->set_int(new_value & 1);
#else
#endif
        break;
    }
}

#ifdef LOG_CPU
void z80::log_state(uint8_t command, bool before, unsigned int cycles)
{
    if (log_available())
    {
#ifndef EXTERNAL_Z80
        z80context * c = static_cast<z80context*>(core->get_context());
        logs(
            QString(" %1").arg(command, 2, 16, QChar('0')) + ((before)?"+":"-")
            + QString(" AF:%1").arg(c->registers.reg_pairs.AF, 4, 16, QChar('0'))
            + QString(" BC:%1").arg(c->registers.reg_pairs.BC, 4, 16, QChar('0'))
            + QString(" DE:%1").arg(c->registers.reg_pairs.DE, 4, 16, QChar('0'))
            + QString(" HL:%1").arg(c->registers.reg_pairs.HL, 4, 16, QChar('0'))
            + QString(" SP:%1").arg(c->registers.regs.SP, 4, 16, QChar('0'))
            + QString(" IX:%1").arg(c->registers.reg_pairs.IX, 4, 16, QChar('0'))
            + QString(" IY:%1").arg(c->registers.reg_pairs.IY, 4, 16, QChar('0'))
        );
#else
        Z80 * c = static_cast<Z80*>(core_ext);
        logs(
            QString(" %1").arg(command, 2, 16, QChar('0')) + ((before)?"+":"-")
            + QString(" AF:%1%2").arg(c->reg.pair.A, 2, 16, QChar('0')).arg(c->reg.pair.F, 2, 16, QChar('0'))
            + QString(" BC:%1%2").arg(c->reg.pair.B, 2, 16, QChar('0')).arg(c->reg.pair.C, 2, 16, QChar('0'))
            + QString(" DE:%1%2").arg(c->reg.pair.D, 2, 16, QChar('0')).arg(c->reg.pair.E, 2, 16, QChar('0'))
            + QString(" HL:%1%2").arg(c->reg.pair.H, 2, 16, QChar('0')).arg(c->reg.pair.L, 2, 16, QChar('0'))
            + QString(" SP:%1").arg(c->reg.SP, 4, 16, QChar('0'))
            + QString(" IX:%1").arg(c->reg.IX, 4, 16, QChar('0'))
            + QString(" IY:%1").arg(c->reg.IY, 4, 16, QChar('0'))
            );
#endif
    }
}
#endif


ComputerDevice * create_z80(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new z80(im, cd);
}
