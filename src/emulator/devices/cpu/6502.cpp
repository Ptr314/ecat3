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
    , m_family(family_type)
{
    core = new mos6502Core(this, family_type);

}

std::string mos6502::disasm_table() const
{
    return (m_family == MOS_6502_FAMILY_65C02) ? "65c02.dis" : "6502.dis";
}

mos6502::~mos6502()
{
    delete core;
}

//dummy_reads = 1: the bus cycles of the NMOS part that carry no data (see
//mos6502core::dummy_access). The 65C02 has its own and is left without them
emulator::Result mos6502::load_config(SystemData *sd)
{
    emulator::Result res = CPU::load_config(sd);
    if (!res) return res;
    core->set_dummy_access(read_confg_value(cd, "dummy_reads", false, false));
    return emulator::Result::ok();
}

void mos6502::reset(bool cold)

{
    CPU::reset(cold);
}

//The address line is driven for the few configurations that wire something
//to it (the Агат-7 ROM card mapper takes A12-A13). A change of it cascades to
//every consumer and its callbacks, and it came on every memory access - three
//per instruction - whether or not the wired bits had moved, which they mostly
//have not. Only then is it driven; otherwise the value is simply kept, as the
//consumers would see nothing different anyway
inline void mos6502::drive_address(unsigned int address)
{
    if (((address ^ i_address.value) & i_address.linked_bits) != 0)
        i_address.change(address);
    else
        i_address.value = address;
}

unsigned int mos6502::read_mem(unsigned int address)
{
    m_bus_cycles++;
    drive_address(address);
    return mm->read(address);
}

void mos6502::write_mem(unsigned int address, unsigned int data)
{
    m_bus_cycles++;
    drive_address(address);

    mm->write(address, data);
}

void mos6502::save_state(StateWriter &w)
{
    CPU::save_state(w);
    mos6502context * c = core->get_context();
    w.u("A", c->A, 8);
    w.u("X", c->X, 8);
    w.u("Y", c->Y, 8);
    w.u("P", c->P, 8);
    w.u("S", c->S, 8);
    w.u("PC", c->r16.PC);
    w.b("is_nmi", c->is_nmi);
    w.b("is_irq", c->is_irq);
    w.b("stop", c->stop);
    w.b("wait", c->wait);
    //commands[] is a table of function pointers built from type, which comes
    //from the configuration
}

emulator::Result mos6502::load_state(const StateReader &r)
{
    emulator::Result res = CPU::load_state(r);
    if (!res) return res;
    mos6502context * c = core->get_context();
    r.u("A", c->A);
    r.u("X", c->X);
    r.u("Y", c->Y);
    r.u("P", c->P);
    r.u("S", c->S);
    r.u("PC", c->r16.PC);
    r.b("is_nmi", c->is_nmi);
    r.b("is_irq", c->is_irq);
    r.b("stop", c->stop);
    r.b("wait", c->wait);
    return emulator::Result::ok();
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
    //Asked by the debugger and by LOG cpu.command, not by the processor
    return peek_mem(get_pc());
}

unsigned int mos6502::get_pc()
{
    return core->get_pc();
}

void mos6502::set_context_value(const std::string &name, unsigned int value)
{
    mos6502context * c = core->get_context();

    //Every register get_registers() shows, by the same name
    if (name == "A")       c->A = (uint8_t)value;
    else if (name == "X")  c->X = (uint8_t)value;
    else if (name == "Y")  c->Y = (uint8_t)value;
    else if (name == "S")  c->S = (uint8_t)value;
    else if (name == "P")  c->P = (uint8_t)value;
    else if (name == "PC") c->r16.PC = (uint16_t)value;
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

    //Cycles a DMA controller has taken off the bus, see CPU::hold()
    unsigned int held = take_hold();
    if (held > 0) { m_total += held; return held; }

    if (m_debug != DEBUG_OFF) debug_before();
    m_bus_cycles = 0;
    unsigned int cycles = core->execute();
    m_total += cycles;
    m_bus_cycles = 0;


    if (m_debug != DEBUG_OFF) debug_after();

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

bool mos6502::set_flag(const std::string &name, bool value)
{
    static const char * names[] = {"C", "Z", "I", "D", "B", "5", "V", "N"};
    static const uint8_t bits[] = {MOS6502::F_C, MOS6502::F_Z, MOS6502::F_I, MOS6502::F_D,
                                   MOS6502::F_B, MOS6502::F_P5, MOS6502::F_V, MOS6502::F_N};
    mos6502context * c = core->get_context();
    for (unsigned int i = 0; i < 8; i++)
        if (name == names[i]) {
            c->P = value ? (uint8_t)(c->P | bits[i]) : (uint8_t)(c->P & ~bits[i]);
            return true;
        }
    return false;
}

bool mos6502::get_sp(unsigned int &sp)
{
    sp = core->get_context()->S;
    return true;
}

//S points at the free byte below the top, in page 1
bool mos6502::stack_address(int i, unsigned int &address)
{
    address = 0x100 + ((core->get_context()->S + 1 + i) & 0xFF);
    return true;
}

bool mos6502::waiting()
{
    return core->get_context()->wait;
}
