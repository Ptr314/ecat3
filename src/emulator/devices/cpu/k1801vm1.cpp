// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВМ1 / К1801ВМ2 emulator interface class

#include "k1801vm1.h"
#include "emulator/utils.h"

#define CALLBACK_VIRQ   1
#define CALLBACK_IRQ2   2
#define CALLBACK_IRQ3   3
#define CALLBACK_HALT   4

// ---------------------------  Library wrapper --------------------------------

K1801VM1Core::K1801VM1Core(k1801vm1 * emulator_device, int family_type):
    pdp11core(family_type)
{
    this->emulator_device = emulator_device;
}

// An address nobody answers ends the cycle with a timeout on the bus, which
// aborts the current instruction and traps through vector 4. The firmware of
// the БК relies on it to find out which ROM blocks are actually installed.
uint16_t K1801VM1Core::read_word(uint16_t address)
{
    uint16_t v = (uint16_t)emulator_device->read_mem_word(address);
    if (emulator_device->bus_timeout()) m_abort = true;
    return v;
}

void K1801VM1Core::write_word(uint16_t address, uint16_t value)
{
    emulator_device->write_mem_word(address, value);
    if (emulator_device->bus_timeout()) m_abort = true;
}

uint8_t K1801VM1Core::read_byte(uint16_t address)
{
    uint8_t v = (uint8_t)emulator_device->read_mem(address);
    if (emulator_device->bus_timeout()) m_abort = true;
    return v;
}

void K1801VM1Core::write_byte(uint16_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
    if (emulator_device->bus_timeout()) m_abort = true;
}

// ---------------------------  Emulator device --------------------------------

k1801vm1::k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type):
    CPU(im, cd)
    , i_virq(this, im, 1, "virq", MODE_R, CALLBACK_VIRQ)
    , i_vector(this, im, 16, "vector", MODE_R)
    , i_irq2(this, im, 1, "irq2", MODE_R, CALLBACK_IRQ2)
    , i_irq3(this, im, 1, "irq3", MODE_R, CALLBACK_IRQ3)
    , i_halt(this, im, 1, "halt", MODE_R, CALLBACK_HALT)
{
    core = new K1801VM1Core(this, family_type);

    // JSR, EMT and TRAP are the instructions worth stepping over. They occupy
    // whole opcode ranges rather than single codes, so the list is filled in.
    for (unsigned int c = 0004000; c <= 0004777; c++) over_commands.push_back(c);
    for (unsigned int c = 0104000; c <= 0104777; c++) over_commands.push_back(c);
}

k1801vm1::~k1801vm1()
{
    delete core;
}

emulator::Result k1801vm1::load_config(SystemData *sd)
{
    emulator::Result res = CPU::load_config(sd);
    if (!res) return res;

    // The startup address is wired outside the chip. The БК begins executing
    // straight from it; machines that keep a PC/PSW pair there set start_vector.
    core->start_address = (uint16_t)read_confg_value(cd, "start_address", false, (unsigned int)0100000);
    core->start_from_vector = read_confg_value(cd, "start_vector", false, false);
    core->halt_vector = (uint16_t)read_confg_value(cd, "halt_vector", false, (unsigned int)PDP11::V_BUS_ERROR);

    return emulator::Result::ok();
}

void k1801vm1::reset(bool cold)
{
    CPU::reset(cold);
}

unsigned int k1801vm1::read_mem(unsigned int address)
{
    return mm->read(address) & 0xFF;
}

void k1801vm1::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data & 0xFF);
}

unsigned int k1801vm1::read_mem_word(unsigned int address)
{
    return mm->read_word(address) & 0xFFFF;
}

void k1801vm1::write_mem_word(unsigned int address, unsigned int data)
{
    mm->write_word(address, data & 0xFFFF);
}

bool k1801vm1::bus_timeout()
{
    return mm->no_device;
}

std::vector<std::pair<std::string, std::string>> k1801vm1::get_registers()
{
    pdp11context * c = core->get_context();
    return {
        {"R0", oct_str(c->R[0], 6)},
        {"R1", oct_str(c->R[1], 6)},
        {"R2", oct_str(c->R[2], 6)},
        {"R3", oct_str(c->R[3], 6)},
        {"R4", oct_str(c->R[4], 6)},
        {"R5", oct_str(c->R[5], 6)},
        {"-1", ""},
        {"SP", oct_str(c->R[6], 6)},
        {"PC", oct_str(c->R[7], 6)},
        {"-2", ""},
        {"PSW", oct_str(c->PSW, 6)}
    };
}

std::vector<std::pair<std::string, std::string>> k1801vm1::get_flags()
{
    pdp11context * c = core->get_context();
    return {
        {"C", std::to_string(((c->PSW & PDP11::F_C) != 0) ? 1 : 0)},
        {"V", std::to_string(((c->PSW & PDP11::F_V) != 0) ? 1 : 0)},
        {"Z", std::to_string(((c->PSW & PDP11::F_Z) != 0) ? 1 : 0)},
        {"N", std::to_string(((c->PSW & PDP11::F_N) != 0) ? 1 : 0)},
        {"T", std::to_string(((c->PSW & PDP11::F_T) != 0) ? 1 : 0)},
        {"I", std::to_string(((c->PSW & PDP11::F_MASK) != 0) ? 1 : 0)}
    };
}

unsigned int k1801vm1::get_command()
{
    return core->get_command();
}

unsigned int k1801vm1::get_pc()
{
    return core->get_pc();
}

void k1801vm1::set_context_value(const std::string &name, unsigned int value)
{
    pdp11context * c = core->get_context();

    if (name == "PC")
    {
        c->R[PDP11::REG_PC] = (uint16_t)value;
    }
}

void k1801vm1::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    // The bus signals of the 1801 series are active low
    bool active = (new_value & 1) == 0;

    switch (callback_id) {
    case CALLBACK_VIRQ:
        core->set_virq(active, (uint16_t)i_vector.value);
        break;
    case CALLBACK_IRQ2:
        core->set_irq2(active);
        break;
    case CALLBACK_IRQ3:
        core->set_irq3(active);
        break;
    case CALLBACK_HALT:
        core->set_halt(active);
        break;
    }
}

unsigned int k1801vm1::execute()
{
    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    if (debug == DEBUG_STOPPED)
        return 0;

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

ComputerDevice * create_k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM1);
}

ComputerDevice * create_k1801vm2(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM2);
}
