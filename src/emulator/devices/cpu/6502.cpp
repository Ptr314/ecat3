// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 6502 & 65c02 emulator interface class

#include "6502.h"
#include "emulator/utils.h"

#define CALLBACK_NMI    1
#define CALLBACK_INT    2


// ---------------------------  Library wrapper --------------------------------

mos6502Core::mos6502Core(mos6502 * emulator_device, int family_type):
    mos6502core(family_type)
{
    this->emulator_device = emulator_device;
}

uint8_t mos6502Core::read_mem(uint16_t address)
{
    return emulator_device->read_mem(address);
}

void mos6502Core::write_mem(uint16_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
}

// ---------------------------  Emulator device --------------------------------

mos6502::mos6502(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type):
    CPU(im, cd)
    , i_nmi(this, im, 1, "nmi", MODE_R, CALLBACK_NMI)
    , i_irq(this, im, 1, "irq", MODE_R, CALLBACK_INT)
    , i_so(this, im, 1, "so", MODE_R)
{
    core = new mos6502Core(this, family_type);

    over_commands.push_back(0x20);
}

void mos6502::reset(bool cold)
{
    CPU::reset(cold);
}

unsigned int mos6502::read_mem(unsigned int address)
{
    i_address.change(address);
    return mm->read(address);
}

void mos6502::write_mem(unsigned int address, unsigned int data)
{
    i_address.change(address);
    mm->write(address, data);
}

std::vector<std::pair<std::string, std::string>> mos6502::get_registers()
{
    mos6502context * c = core->get_context();
    return {
        {"A",  hex_str(c->A, 2)},
        {"X",  hex_str(c->X, 2)},
        {"Y",  hex_str(c->Y, 2)},
        {"S",  hex_str(c->S, 2)},
        {"P",  hex_str(c->P, 2)},
        {"-1", ""},
        {"PC", hex_str(c->r16.PC, 4)}
    };
}

std::vector<std::pair<std::string, std::string>> mos6502::get_flags()
{
    mos6502context * c = core->get_context();
    return {
        {"C", std::to_string(((c->P & MOS6502::F_C) != 0) ? 1 : 0)},
        {"Z", std::to_string(((c->P & MOS6502::F_Z) != 0) ? 1 : 0)},
        {"I", std::to_string(((c->P & MOS6502::F_I) != 0) ? 1 : 0)},
        {"D", std::to_string(((c->P & MOS6502::F_D) != 0) ? 1 : 0)},
        {"B", std::to_string(((c->P & MOS6502::F_B) != 0) ? 1 : 0)},
        {"5", std::to_string(((c->P & MOS6502::F_P5) != 0) ? 1 : 0)},
        {"V", std::to_string(((c->P & MOS6502::F_V) != 0) ? 1 : 0)},
        {"N", std::to_string(((c->P & MOS6502::F_N) != 0) ? 1 : 0)}
    };
}

unsigned int mos6502::get_command()
{
    return core->get_command();
}

unsigned int mos6502::get_pc()
{
    return core->get_pc();
}

void mos6502::set_context_value(const std::string &name, unsigned int value)
{
    mos6502context * c = core->get_context();

    if (name == "PC")
    {
        c->r16.PC = value;
    }
}

void mos6502::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_NMI:
        core->set_nmi((new_value & 1) == 0); //NMI has active 0
        break;
    case CALLBACK_INT:
        core->set_irq((new_value & 1) == 0); //INT has active 0
        break;
    }
}

unsigned int mos6502::execute()
{
    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    if (m_debug == DEBUG_STOPPED)
        return 0;

    unsigned int cycles = core->execute();


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

ComputerDevice * create_mos6502(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new mos6502(im, cd, MOS_6502_FAMILY_BASIC);
}

ComputerDevice * create_wdc65c02(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new mos6502(im, cd, MOS_6502_FAMILY_65C02);
}
