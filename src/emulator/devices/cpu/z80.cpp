// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Zilog Z80 emulator interface class

#include "z80.h"
#include "emulator/utils.h"

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
    return data;
}

void z80::write_port(unsigned int address, unsigned int data)
{
    mm->write_port(address, data);
}

void z80::reset(bool cold)
{
    CPU::reset(cold);
}

std::vector<std::pair<std::string, std::string>> z80::get_registers()
{
    std::vector<std::pair<std::string, std::string>> l;
#ifndef EXTERNAL_Z80
    z80context * c = core->get_context();

    l = {
        {"AF",  hex_str((c->registers.regs.A << 8) + c->registers.regs.F, 4)},
        {"BC",  hex_str(c->registers.reg_pairs.BC, 4)},
        {"DE",  hex_str(c->registers.reg_pairs.DE, 4)},
        {"HL",  hex_str(c->registers.reg_pairs.HL, 4)},
        {"IX",  hex_str(c->registers.reg_pairs.IX, 4)},
        {"IY",  hex_str(c->registers.reg_pairs.IY, 4)},
        {"-1",  ""},
        {"AF'", hex_str(c->registers.regs.AF_, 4)},
        {"BC'", hex_str(c->registers.regs.BC_, 4)},
        {"DE'", hex_str(c->registers.regs.DE_, 4)},
        {"HL'", hex_str(c->registers.regs.HL_, 4)},
        {"-2",  ""},
        {"SP",  hex_str(c->registers.regs.SP, 4)},
        {"PC",  hex_str(c->registers.regs.PC, 4)},
        {"-3",  ""},
        {"I",    hex_str(c->registers.regs.I, 2)},
        {"R",    hex_str(c->registers.regs.R, 2)},
        {"IFF1", hex_str(c->IFF1, 1)},
        {"IFF2", hex_str(c->IFF2, 1)}
    };
#else
#endif

    return l;
}

std::vector<std::pair<std::string, std::string>> z80::get_flags()
{
    std::vector<std::pair<std::string, std::string>> l;
#ifndef EXTERNAL_Z80
    z80context * c = core->get_context();

    l = {
        {"S",  std::to_string(((c->registers.regs.F & F_SIGN) != 0) ? 1 : 0)},
        {"Z",  std::to_string(((c->registers.regs.F & F_ZERO) != 0) ? 1 : 0)},
        {"5",  std::to_string(((c->registers.regs.F & F_B5) != 0) ? 1 : 0)},
        {"H",  std::to_string(((c->registers.regs.F & F_HALF_CARRY) != 0) ? 1 : 0)},
        {"3",  std::to_string(((c->registers.regs.F & F_B3) != 0) ? 1 : 0)},
        {"PV", std::to_string(((c->registers.regs.F & F_PARITY) != 0) ? 1 : 0)},
        {"N",  std::to_string(((c->registers.regs.F & F_SUB) != 0) ? 1 : 0)},
        {"C",  std::to_string(((c->registers.regs.F & F_CARRY) != 0) ? 1 : 0)}
    };
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
    if (m_debug == DEBUG_STOPPED)
        return 10;

#ifndef EXTERNAL_Z80
    unsigned int cycles = core->execute();
#else
    unsigned int cycles = core_ext->execute(1);
#endif


    switch (m_debug) {
    case DEBUG_STEP:
        m_debug = DEBUG_STOPPED;
        break;
    case DEBUG_BRAKES:
        if (check_breakpoint(get_pc())) m_debug = DEBUG_STOPPED;
        break;
    default:
        break;
    }

    return cycles;
}

void z80::set_context_value(const std::string &name, unsigned int value)
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


ComputerDevice * create_z80(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new z80(im, cd);
}
