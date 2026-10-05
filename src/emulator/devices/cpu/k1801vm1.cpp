// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВМ1 / К1801ВМ2 emulator interface class

#include "k1801vm1.h"
#include "emulator/disasm_pdp11.h"
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
uint16_t K1801VM1Core::read_word(uint32_t address)
{
    uint16_t v = (uint16_t)emulator_device->read_mem_word(address);
    if (emulator_device->bus_timeout()) m_abort = true;
    if (Vm1BusTiming * t = emulator_device->timing()) t->record(address & 0xFFFE, false, m_stream, emulator_device->last_reply(), emulator_device->last_wait());
    else bus_cycle(address, false);
    return v;
}

void K1801VM1Core::write_word(uint32_t address, uint16_t value)
{
    emulator_device->write_mem_word(address, value);
    if (emulator_device->bus_timeout()) m_abort = true;
    if (Vm1BusTiming * t = emulator_device->timing()) t->record(address & 0xFFFE, true, false, emulator_device->last_reply(), emulator_device->last_wait());
    else bus_cycle(address, true);
}

uint8_t K1801VM1Core::read_byte(uint32_t address)
{
    uint8_t v = (uint8_t)emulator_device->read_mem(address);
    if (emulator_device->bus_timeout()) m_abort = true;
    if (Vm1BusTiming * t = emulator_device->timing()) t->record(address & 0xFFFE, false, m_stream, emulator_device->last_reply(), emulator_device->last_wait());
    else bus_cycle(address, false);
    return v;
}

void K1801VM1Core::write_byte(uint32_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
    if (emulator_device->bus_timeout()) m_abort = true;
    if (Vm1BusTiming * t = emulator_device->timing()) t->record(address & 0xFFFE, true, false, emulator_device->last_reply(), emulator_device->last_wait());
    else bus_cycle(address, true);
}

// Такты от начала цикла шины до строба данных (DIN, DOUT): оценка, по ней
// память узнаёт, в какой момент её спросили
#define BUS_STROBE  2

void K1801VM1Core::bus_cycle(uint32_t address, bool write)
{
    int cycle = (int)(write ? C_DATO : C_DATI);
    if (WaitSource * w = emulator_device->last_wait()) {
        const unsigned int reply = w->wait_states(address, write, bus_offset + BUS_STROBE);
        if (reply != 0) {
            const int extra = (int)reply - (int)C_REPLY;
            bus_extra += extra;
            cycle += extra;
        }
    }
    if (cycle > 0) bus_offset += (unsigned int)cycle;
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
    m_family = family_type;

    // JSR, EMT and TRAP are the instructions worth stepping over. They occupy
    // whole opcode ranges rather than single codes, so the list is filled in.
    for (unsigned int c = 0004000; c <= 0004777; c++) over_commands.push_back(c);
    for (unsigned int c = 0104000; c <= 0104777; c++) over_commands.push_back(c);
}

k1801vm1::~k1801vm1()
{
    delete core;
    delete m_timing;
}

// The PDP-11 family decodes bit fields instead of whole opcode bytes and
// therefore brings its own decoder rather than a table file; the ВМ2 has the
// EIS instructions on top
DisAsm * k1801vm1::create_disasm(MAYBE_UNUSED const std::string &data_path, emulator::Result &res)
{
    res = emulator::Result::ok();
    return new DisAsmPDP11(m_family == PDP11_FAMILY_1801VM2 || m_family == PDP11_FAMILY_1801VM3);
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

    // Which clock count this processor keeps. legacy: the formulas above,
    // built from reply_delay. vm1, vm2: every bus cycle placed in time from a
    // simulation of the chip (К1801ВМ1 or КМ1801ВМ2), with each memory
    // replying as its BusReply or its WaitSource says
    const std::string timing = read_confg_value(cd, "timing", false, std::string("legacy"));
    delete m_timing;
    m_timing = nullptr;
    if (timing == "vm1")
        m_timing = new Vm1BusTiming(clock);
    else if (timing == "vm2")
        m_timing = new Vm1BusTiming(clock, Vm1BusTiming::CHIP_VM2);
    else if (timing != "legacy")
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "Unknown timing")) + "} " + name + ": " + timing);
    m_timing_vm2 = (timing == "vm2");

    // База векторов пультового режима - вывод SEL процессора. Ноль оставляет
    // прежнее поведение: вход в режим идёт обычной ловушкой через halt_vector,
    // как на БК. У УК-НЦ здесь 160000, и тогда работает настоящий пультовый
    // режим с теневой парой КРСК/КРСП и командами RUN, STEP, MFPC и прочими
    core->halt_sel = read_confg_value(cd, "halt_sel", false, (unsigned int)0);
    // Регистр начального пуска для пультовой команды 000020. По умолчанию -
    // адрес пуска без режима
    core->una_value = read_confg_value(cd, "una_value", false, (unsigned int)core->start_address);
    // ВМ3: после пуска сразу в пульт - переключатель платы на «пульт»
    core->start_in_halt = read_confg_value(cd, "start_halt", false, false);

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
    w.b("dclo_active", m_dclo_active);
    w.b("aclo_active", m_aclo_active);

    if (core->is_vm3()) {
        uint16_t v[pdp11core::VM3_STATE_WORDS];
        core->get_vm3(v);
        w.array("vm3", v, pdp11core::VM3_STATE_WORDS);
    }

    //Where the chain of bus cycles stands against the 037 cycle: without it a
    //restored БК0011М would run with another phase of the windows
    if (m_timing) {
        w.n64("timing_now", m_timing->m_now);
        w.n64("timing_start", m_timing->m_start);
        if (m_timing_vm2) {
            w.n64("timing_s0", m_timing->m_s0);
            w.u("timing_kind", m_timing->m_kind2);
            w.u("timing_flags", m_timing->m_flags2);
        }
    }

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
    r.b("dclo_active", m_dclo_active);
    r.b("aclo_active", m_aclo_active);
    if (core->is_vm3()) {
        uint16_t v[pdp11core::VM3_STATE_WORDS] = {};
        if (r.array("vm3", v, pdp11core::VM3_STATE_WORDS)) core->set_vm3(v);
    }
    if (m_timing) {
        r.n64("timing_now", m_timing->m_now);
        r.n64("timing_start", m_timing->m_start);
        if (m_timing_vm2) {
            unsigned int kind = 0;
            r.n64("timing_s0", m_timing->m_s0);
            r.u("timing_kind", kind);
            m_timing->m_kind2 = (uint8_t)(kind < 3 ? kind : 0);
            unsigned int flags = Vm1BusTiming::F2_PREV_DATA_FREE;
            r.u("timing_flags", flags);
            m_timing->m_flags2 = (uint8_t)(flags & 3);
        }
        m_timing->state_restored();
    }
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
    m_timeout_address = address & (core->is_vm3() ? 0x7FFFFFu : 0xFFFFu);
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
    r.push_back({"halt_history",    "The same ring as it was at the last entry into the console (halt) mode", true});
    if (core->is_vm3()) {
        r.push_back({"mmu",  "ВМ3: SR0, SR2, SR3, HPAR", false});
        r.push_back({"sps",  "ВМ3: указатели стека ядра и пользователя (вне текущего режима)", false});
        r.push_back({"par",  "ВМ3: PAR ядра 0-7, затем пользователя 0-7", false});
        r.push_back({"pdr",  "ВМ3: PDR ядра 0-7, затем пользователя 0-7", false});
    }
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
    if (core->is_vm3() && (field == "mmu" || field == "sps" || field == "par" || field == "pdr")) {
        uint16_t v[pdp11core::VM3_STATE_WORDS];
        core->get_vm3(v);
        unsigned int from_i = 0, count = 0;
        if (field == "sps") { from_i = 0; count = 4; }
        else if (field == "par") { from_i = 4; count = 16; }
        else if (field == "pdr") { from_i = 20; count = 16; }
        else { from_i = 36; count = 4; }
        for (unsigned int i = 0; i < count; i++) out.values.push_back(v[from_i + i]);
        return true;
    }
    if (field == "history" || field == "halt_history") {
        // Без диапазона - всё кольцо; history(n) - последние n команд
        const bool halt = (field == "halt_history");
        const uint16_t * ring = halt ? core->m_halt_history : core->m_history;
        const unsigned int pos = halt ? core->m_halt_history_pos : core->m_history_pos;
        const unsigned int size = pdp11core::HISTORY_SIZE;
        unsigned int n = (from == 0 && to == 0)? size : ((from > size)? size : from);
        for (unsigned int i = n; i > 0; i--)
            out.values.push_back(ring[(pos - i) & (size - 1)]);
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

// У ВМ3 отладчик видит память так же, как процессор: через диспетчер, в
// пульте - теневую. Адрес физический, если он уже шире 16 разрядов
unsigned int k1801vm1::peek_mem(unsigned int address)
{
    if (core->is_vm3() && address <= 0xFFFF)
        return mm->get_direct(core->vm3_peek_address((uint16_t)address)) & 0xFF;
    return CPU::peek_mem(address);
}

unsigned int k1801vm1::get_command()
{
    //Asked by the debugger and by LOG cpu.command, not by the processor. A
    //bus cycle here would leave a timeout for the next instruction to trap on
    //and a record in the timing chain that no instruction made
    const unsigned int pc = get_pc() & 0xFFFE;
    return peek_mem(pc) | (peek_mem(pc + 1) << 8);
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
        // that leaves ~dclo unconnected runs exactly as before. Released while
        // ~aclo is still active, it is the power-up sequence (the ДВК panel
        // drops АИП 10 ms before АСП): the processor starts once ~aclo goes
        // too, and that release is the start, not a power-fail interrupt
        m_dclo_active = active;
        if (active) {
            m_held_in_reset = true;
        } else if (m_held_in_reset && !m_aclo_active) {
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
            else if (!m_dclo_active) {
                m_held_in_reset = false;
                reset_mode = true;
            }
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
    if (m_held_in_reset) {
        if (m_timing) m_timing->idle(8);
        return 8;
    }

    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
        if (m_timing) m_timing->discard();
    }

    if (m_debug == DEBUG_STOPPED)
        return 0;

    //Cycles a DMA controller has taken off the bus, see CPU::hold()
    unsigned int held = take_hold();
    if (held > 0) {
        if (m_timing) m_timing->idle(held);
        return held;
    }

    core->bus_offset = 0;
    core->bus_extra = 0;
    unsigned int cycles = core->execute();
    if (m_timing) cycles = timed_cycles(cycles);
    else if (core->bus_extra != 0) {
        const int c = (int)cycles + core->bus_extra;
        cycles = (c > 0) ? (unsigned int)c : 1;
    }

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

// The clocks the last core->execute() took under timing = vm1. Anything the
// table has no template for - an instruction that ended in a trap, WAIT idling -
// keeps the count of the old formulas and only moves the chain along
unsigned int k1801vm1::timed_cycles(unsigned int legacy)
{
    const uint16_t pc = (uint16_t)core->get_pc();
    if (m_timing_vm2) return timed_cycles_vm2(legacy, pc);
    unsigned int form = Vm1BusTiming::F_COUNT;
    bool skip_opcode = false;
    // Вход в пультовое исключение и возврат из него - свои циклы шины (RMW
    // 0177716, 0177674/0177676), которых нет ни в одном шаблоне
    if (!core->m_last_console) {
        switch (core->m_last_kind) {
        case pdp11core::LAST_INSN:
            if (!core->m_last_trapped) {
                form = Vm1BusTiming::form_of(core->m_last_command, core->m_last_taken);
                skip_opcode = true;
            }
            break;
        case pdp11core::LAST_INTERRUPT:
            form = core->m_last_iako ? Vm1BusTiming::F_INTERRUPT_VIRQ : Vm1BusTiming::F_INTERRUPT;
            break;
        default:
            break;
        }
    }
    if (form < Vm1BusTiming::F_COUNT) {
        AddressableDevice * d = mm->peek_read_device(pc);
        const int c = m_timing->finish(form, (d != nullptr) ? d->bus_reply : nullptr, skip_opcode);
        if (c >= 0) return (unsigned int)c;
    }
    m_timing->discard();
    m_timing->idle(legacy);
    return legacy;
}

// The same under timing = vm2: the forms of the ВМ2, the steps of an ASH or
// ASHC shift on top of its template, and the memory of the next opcode with
// its WaitSource
unsigned int k1801vm1::timed_cycles_vm2(unsigned int legacy, uint16_t pc)
{
    unsigned int form = Vm1BusTiming::F2_COUNT;
    unsigned int extra = 0;
    bool skip_opcode = false;
    if (!core->m_last_console) {
        switch (core->m_last_kind) {
        case pdp11core::LAST_INSN:
            if (!core->m_last_trapped) {
                const uint16_t w = core->m_last_command;
                form = Vm1BusTiming::form2_of(w, core->m_last_taken,
                                              (w & 0177000) == 0071000 && core->m_last_div_v);
                if ((w & 0176000) == 0072000) extra = core->m_last_shift * Vm1BusTiming::VM2_SHIFT_CLOCKS;
                skip_opcode = true;
            }
            break;
        case pdp11core::LAST_INTERRUPT:
            form = core->m_last_iako ? Vm1BusTiming::F2_INTERRUPT_VIRQ : Vm1BusTiming::F2_INTERRUPT;
            break;
        default:
            break;
        }
    }
    if (form < Vm1BusTiming::F2_COUNT) {
        AddressableDevice * d = mm->peek_read_device(pc);
        const int c = m_timing->finish(form, (d != nullptr) ? d->bus_reply : nullptr, skip_opcode,
                                       (d != nullptr) ? d->wait_source : nullptr, pc, extra);
        if (c >= 0) return (unsigned int)c;
    }
    m_timing->discard();
    m_timing->idle(legacy);
    return legacy;
}

ComputerDevice * create_k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM1);
}

ComputerDevice * create_k1801vm2(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM2);
}

ComputerDevice * create_k1801vm3(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new k1801vm1(im, cd, PDP11_FAMILY_1801VM3);
}
