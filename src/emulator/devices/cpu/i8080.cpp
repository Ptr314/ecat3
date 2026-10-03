// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8080 emulator interface class

#include "i8080.h"
#include "emulator/utils.h"

using namespace I8080;

//----------------------- Library wrapper -----------------------------------
I8080Core::I8080Core(i8080 * emulator_device):
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

void I8080Core::inte_changed(unsigned int inte)
{
    emulator_device->inte_changed(inte);
}

bool I8080Core::int_request()
{
    return emulator_device->int_request();
}

uint8_t I8080Core::int_acknowledge()
{
    return emulator_device->int_acknowledge();
}

uint16_t I8080Core::int_call_address()
{
    return emulator_device->int_call_address();
}

//----------------------- Emulator device -----------------------------------

i8080::i8080(InterfaceManager *im, EmulatorConfigDevice *cd):
      CPU(im, cd)
    , i_nmi(this, im, 1, "nmi", MODE_R)
    , i_int(this, im, 1, "int", MODE_R)
    , i_inte(this, im, 1, "inte", MODE_W)
    , i_m1(this, im, 1, "m1", MODE_W)
    , i_inta(this, im, 1, "inta", MODE_W)
    , i_int_address(this, im, 16, "int_address", MODE_R)

{
    core = new I8080Core(this);

    //Interrupts are taken only where the config says what the acknowledge
    //reads, since a machine with a ВН59 (Irisha) would need a CALL there
    m_int_opcode = read_confg_value(cd, "int_opcode", false, (unsigned int)0x100);

    over_commands.push_back(0xCD);
    over_commands.push_back(0xDD);
    over_commands.push_back(0xED);
    over_commands.push_back(0xFD);
}

i8080::~i8080()
{
    //The base class of the core has no virtual destructor
    delete static_cast<I8080Core*>(core);
}

unsigned int i8080::get_pc()
{
    return core->get_context()->registers.regs.PC;
}

unsigned int i8080::read_mem(unsigned int address)
{
    return mm->read(address);
}

void i8080::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

unsigned int i8080::read_port(unsigned int address)
{
    return mm->read_port(address);
}

void i8080::write_port(unsigned int address, unsigned int data)
{
    mm->write_port(address, data);
}

void i8080::reset(bool cold)
{
    CPU::reset(cold);
    //Lines start at _FFFF: the first acknowledge would not be an edge
    i_inta.change(0);
}

void i8080::inte_changed(unsigned int inte)
{
    i_inte.change(inte);
}

bool i8080::int_request()
{
    return m_int_opcode <= 0xFF && i_int.linked > 0 && (i_int.value & 1) != 0;
}

emulator::Result i8080::load_config(SystemData *sd)
{
    emulator::Result res = CPU::load_config(sd);
    if (!res) return res;
    //CALL без адреса ушёл бы на FFFF молча
    if (m_int_opcode == 0xCD && i_int_address.linked == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "int_opcode = $CD needs ~int_address")) + "} " + name);
    return emulator::Result::ok();
}

uint8_t i8080::int_acknowledge()
{
    //A ВН59 picks the level to serve on the strobe and answers with the CALL
    //address on int_address; int_opcode = $CD says the machine has one
    i_inta.change(1);
    i_inta.change(0);
    return static_cast<uint8_t>(m_int_opcode);
}

uint16_t i8080::int_call_address()
{
    return static_cast<uint16_t>(i_int_address.value);
}

void i8080::save_state(StateWriter &w)
{
    CPU::save_state(w);
    i8080context * c = core->get_context();
    w.array("regs", c->registers.reg_array_8, 8);
    //The flags follow A in the context and are not part of that array
    w.u("F", c->registers.regs.F, 8);
    w.u("SP", c->registers.regs.SP);
    w.u("PC", c->registers.regs.PC);
    w.b("halted", c->halted);
    w.u("int_enable", c->int_enable, 8);
    w.b("ei_delay", c->ei_delay);
}

emulator::Result i8080::load_state(const StateReader &r)
{
    emulator::Result res = CPU::load_state(r);
    if (!res) return res;
    i8080context * c = core->get_context();
    r.array("regs", c->registers.reg_array_8, 8);
    r.u("F", c->registers.regs.F);
    r.u("SP", c->registers.regs.SP);
    r.u("PC", c->registers.regs.PC);
    r.b("halted", c->halted);
    r.u("int_enable", c->int_enable);
    r.b("ei_delay", c->ei_delay);
    return emulator::Result::ok();
}

std::vector<std::pair<std::string, std::string>> i8080::get_registers()
{
    i8080context * c = core->get_context();
    return {
        {"A",  hex_str(c->registers.regs.A, 2)},
        {"BC", hex_str(c->registers.reg_pairs.BC, 4)},
        {"DE", hex_str(c->registers.reg_pairs.DE, 4)},
        {"HL", hex_str(c->registers.reg_pairs.HL, 4)},
        {"-",  ""},
        {"SP", hex_str(c->registers.regs.SP, 4)},
        {"PC", hex_str(c->registers.regs.PC, 4)}
    };
}

std::vector<std::pair<std::string, std::string>> i8080::get_flags()
{
    i8080context * c = core->get_context();
    return {
        {"C", std::to_string(((c->registers.regs.F & F_CARRY) != 0) ? 1 : 0)},
        {"P", std::to_string(((c->registers.regs.F & F_PARITY) != 0) ? 1 : 0)},
        {"H", std::to_string(((c->registers.regs.F & F_HALF_CARRY) != 0) ? 1 : 0)},
        {"Z", std::to_string(((c->registers.regs.F & F_ZERO) != 0) ? 1 : 0)},
        {"S", std::to_string(((c->registers.regs.F & F_SIGN) != 0) ? 1 : 0)}
    };
}


unsigned int i8080::execute()
{
    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    //No cycles, as the other processors do: Emulator::timer_proc() then keeps
    //the machine's time still. Returning 10 here let the clock, the timers,
    //the screen and the tape run on while the debugger held the processor
    if (m_debug == DEBUG_STOPPED)
        return 0;

    //The bus belongs to a DMA controller for these cycles: emulated time moves
    //on, the processor does not, and an interrupt is not taken either
    unsigned int held = take_hold();
    if (held > 0) return held;

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

void i8080::set_context_value(const std::string &name, unsigned int value)
{
    if (name == "PC")
    {
        core->get_context()->registers.regs.PC = value;
    }
}

unsigned int i8080::get_command()
{
    //Asked by the debugger and by LOG cpu.command, not by the processor
    return peek_mem(get_pc());
}



ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new i8080(im, cd);
}
