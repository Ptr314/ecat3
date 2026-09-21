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
#define CALLBACK_DCLO   5
#define CALLBACK_ACLO   6

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

void K1801VM1Core::on_virq_ack(uint16_t vector)
{
    emulator_device->virq_acknowledged(vector);
}

void K1801VM1Core::on_bus_init()
{
    emulator_device->bus_init();
}

void K1801VM1Core::on_halt_mode(bool state)
{
    emulator_device->set_halt_mode(state);
}

// ---------------------------  Emulator device --------------------------------

k1801vm1::k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type):
    CPU(im, cd)
    , i_virq(this, im, 1, "virq", MODE_R, CALLBACK_VIRQ)
    , i_vector(this, im, 16, "vector", MODE_R)
    , i_irq2(this, im, 1, "irq2", MODE_R, CALLBACK_IRQ2)
    , i_irq3(this, im, 1, "irq3", MODE_R, CALLBACK_IRQ3)
    , i_halt(this, im, 1, "halt", MODE_R, CALLBACK_HALT)
    , i_dclo(this, im, 1, "dclo", MODE_R, CALLBACK_DCLO)
    , i_aclo(this, im, 1, "aclo", MODE_R, CALLBACK_ACLO)
    , i_halt_mode(this, im, 1, "halt_mode", MODE_W)
    , i_iako(this, im, 16, "iako", MODE_W)
    , i_init(this, im, 1, "init", MODE_W)
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
    // Clock periods the memory takes to answer, which sets the length of every
    // bus cycle: 2 on a 3 MHz БК0010, more where the memory is slower to reply
    core->set_reply_delay(read_confg_value(cd, "reply_delay", false, (unsigned int)2));

    // База векторов пультового режима - вывод SEL процессора. Ноль оставляет
    // прежнее поведение: вход в режим идёт обычной ловушкой через halt_vector,
    // как на БК. У УК-НЦ здесь 160000, и тогда работает настоящий пультовый
    // режим с теневой парой КРСК/КРСП и командами RUN, STEP, MFPC и прочими
    core->halt_sel = read_confg_value(cd, "halt_sel", false, (unsigned int)0);

    // Линию режима надо выставить сразу: сообщается она только при смене, а
    // диспетчер адресов читает её с первого же обращения. Незаданный интерфейс
    // остаётся в _FFFF, и разряд режима читался бы единицей всегда
    set_halt_mode(core->get_context()->halt_mode);

    return emulator::Result::ok();
}

void k1801vm1::reset(bool cold)
{
    CPU::reset(cold);
    // Сам режим выставит ядро в core->reset(), на первом же execute(): пуск
    // по вектору - это вход в пультовый режим, и линия поднимется оттуда
}

void k1801vm1::save_state(StateWriter &w)
{
    CPU::save_state(w);

    pdp11context * c = core->get_context();
    w.array("R", c->R, 8);
    w.u("PSW", c->PSW);
    w.b("halted", c->halted);
    w.b("halt_mode", c->halt_mode);
    w.b("stop", c->stop);
    w.u("console_pc", c->console_pc);
    w.u("console_psw", c->console_psw);

    //Latched requests: a snapshot taken between two instructions with one
    //pending has to bring it back, or the machine simply loses the interrupt
    pdp11core::saved_latches s;
    core->get_latches(s);
    w.b("step_pending", s.step_pending);
    w.b("virq", s.is_virq);
    w.u("virq_vector", s.virq_vector);
    w.b("irq2", s.is_irq2);
    w.b("irq3", s.is_irq3);
    w.b("halt_req", s.is_halt_req);
    w.b("halt_pin", s.halt_pin);
    w.b("aclo", s.is_aclo);
    w.b("abort", s.abort);
    w.b("no_trace", s.no_trace);

    w.b("held_in_reset", m_held_in_reset);
    w.b("aclo_active", m_aclo_active);

    //The ring of executed addresses is what LOG cpu.history prints: a
    //diagnostic, not machine state, and 256 lines of it in every snapshot
    //would be pure noise
}

emulator::Result k1801vm1::load_state(const StateReader &r)
{
    emulator::Result res = CPU::load_state(r);
    if (!res) return res;

    pdp11context * c = core->get_context();
    r.array("R", c->R, 8);
    r.u("PSW", c->PSW);
    r.b("halted", c->halted);
    r.b("halt_mode", c->halt_mode);
    r.b("stop", c->stop);
    r.u("console_pc", c->console_pc);
    r.u("console_psw", c->console_psw);

    pdp11core::saved_latches s;
    core->get_latches(s);
    r.b("step_pending", s.step_pending);
    r.b("virq", s.is_virq);
    r.u("virq_vector", s.virq_vector);
    r.b("irq2", s.is_irq2);
    r.b("irq3", s.is_irq3);
    r.b("halt_req", s.is_halt_req);
    r.b("halt_pin", s.halt_pin);
    r.b("aclo", s.is_aclo);
    r.b("abort", s.abort);
    r.b("no_trace", s.no_trace);
    core->set_latches(s);

    r.b("held_in_reset", m_held_in_reset);
    r.b("aclo_active", m_aclo_active);
    return emulator::Result::ok();
}

unsigned int k1801vm1::read_mem(unsigned int address)
{
    unsigned int v = mm->read(address) & 0xFF;
    note_timeout(address);
    return v;
}

void k1801vm1::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data & 0xFF);
    note_timeout(address);
}

unsigned int k1801vm1::read_mem_word(unsigned int address)
{
    unsigned int v = mm->read_word(address) & 0xFFFF;
    note_timeout(address);
    return v;
}

void k1801vm1::write_mem_word(unsigned int address, unsigned int data)
{
    mm->write_word(address, data & 0xFFFF);
    note_timeout(address);
}

bool k1801vm1::bus_timeout()
{
    return mm->no_device;
}

void k1801vm1::virq_acknowledged(unsigned int vector)
{
    i_iako.change(vector & 0xFFFF);
    i_iako.change(0);
}

void k1801vm1::bus_init()
{
    i_init.change(1);
    i_init.change(0);
}

void k1801vm1::set_halt_mode(bool state)
{
    i_halt_mode.change(state? 1 : 0);
}

void k1801vm1::note_timeout(unsigned int address)
{
    if (!mm->no_device) return;
    m_timeouts++;
    m_timeout_address = address & 0xFFFF;
    m_timeout_pc = core->get_pc();
}

std::vector<DeviceFieldInfo> k1801vm1::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = CPU::get_device_fields();
    r.push_back({"timeouts",        "Number of bus timeouts since start",       false});
    r.push_back({"timeout_address", "Address of the last bus timeout",          false});
    r.push_back({"timeout_pc",      "PC of the instruction that timed out last", false});
    r.push_back({"traps",           "Number of traps and interrupts taken",     false});
    r.push_back({"trap_vector",     "Vector of the last trap or interrupt",     false});
    r.push_back({"trap_pc",         "PC saved by the last trap or interrupt",   false});
    r.push_back({"history",         "Addresses of the last commands executed, oldest first (from,to count back from the newest)", true});
    return r;
}

bool k1801vm1::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "timeouts")        { out.values.push_back(m_timeouts);        return true; }
    if (field == "timeout_address") { out.values.push_back(m_timeout_address); return true; }
    if (field == "timeout_pc")      { out.values.push_back(m_timeout_pc);      return true; }
    if (field == "traps")           { out.values.push_back(core->m_trap_count);  return true; }
    if (field == "trap_vector")     { out.values.push_back(core->m_trap_vector); return true; }
    if (field == "trap_pc")         { out.values.push_back(core->m_trap_pc);     return true; }
    if (field == "history") {
        // Без диапазона - всё кольцо; history(n) - последние n команд
        const unsigned int size = pdp11core::HISTORY_SIZE;
        unsigned int n = (from == 0 && to == 0)? size : ((from > size)? size : from);
        for (unsigned int i = n; i > 0; i--)
            out.values.push_back(core->m_history[(core->m_history_pos - i) & (size - 1)]);
        return true;
    }
    out.width = 0;
    out.numeric = false;
    return CPU::get_field(field, from, to, out);
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
        c->R[PDP11::REG_PC] = (uint16_t)value;
    else if (name == "SP")
        c->R[PDP11::REG_SP] = (uint16_t)value;
    else if (name == "PSW")
        c->PSW = (uint16_t)value;
    else if (name.size() == 2 && name[0] == 'R' && name[1] >= '0' && name[1] <= '5')
        c->R[name[1] - '0'] = (uint16_t)value;
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
    case CALLBACK_DCLO:
        // Held down while the line is active; released, the processor starts
        // over from its start address. The line idles inactive, so a machine
        // that leaves ~dclo unconnected runs exactly as before
        if (active) {
            m_held_in_reset = true;
        } else if (m_held_in_reset) {
            m_held_in_reset = false;
            reset_mode = true;
        }
        break;
    case CALLBACK_ACLO:
        // Запрос даёт снятие линии, а не её появление: так и на машине -
        // прерывание по аварии сети приходит, когда напряжение возвращается.
        // Пока процессор держат сбросом, ничего не происходит. Не подключённая
        // линия стоит снятой и не даёт ни одного запроса
        if (active) {
            m_aclo_active = true;
        } else if (m_aclo_active) {
            m_aclo_active = false;
            if (!m_held_in_reset) core->set_aclo(true);
        }
        break;
    }
}

unsigned int k1801vm1::execute()
{
    // Power-fail asserted: this processor runs nothing, but the machine around
    // it does, so it spends idle bus cycles rather than no time at all - the
    // same thing a WAIT instruction does (pdp11core::C_IDLE). Returning 0 here
    // is what a processor stopped by the debugger does, and that is a different
    // situation on purpose: there emulated time is meant to freeze. On a УК-НЦ
    // the central processor is held like this from power-on until the
    // peripheral one releases it, and the machine has to keep running meanwhile
    if (m_held_in_reset) return 8;

    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    if (m_debug == DEBUG_STOPPED)
        return 0;

    //Cycles a DMA controller has taken off the bus, see CPU::hold()
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

ComputerDevice * create_k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM1);
}

ComputerDevice * create_k1801vm2(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM2);
}
