// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 (К1810ВМ86/ВМ88) emulator interface class

#include "i8086.h"
#include "emulator/disasm_x86.h"
#include "emulator/utils.h"

using namespace I8086;

#define CALLBACK_INTR   1
#define CALLBACK_NMI    2

//---------------------------  Library wrapper --------------------------------

I8086Core::I8086Core(i8086 * emulator_device, int family):
    i8086core(family)
{
    this->emulator_device = emulator_device;
}

uint8_t I8086Core::mem_read8(uint32_t address)
{
    return (uint8_t)emulator_device->mm->read(address);
}

void I8086Core::mem_write8(uint32_t address, uint8_t value)
{
    emulator_device->mm->write(address, value);
}

uint16_t I8086Core::mem_read16(uint32_t address)
{
    return (uint16_t)emulator_device->mm->read_word(address);
}

void I8086Core::mem_write16(uint32_t address, uint16_t value)
{
    emulator_device->mm->write_word(address, value);
}

uint8_t I8086Core::io_read8(uint16_t port)
{
    return (uint8_t)emulator_device->mm->read_port(port);
}

void I8086Core::io_write8(uint16_t port, uint8_t value)
{
    emulator_device->mm->write_port(port, value);
}

//The ports of the machines are byte registers: a word is two of them
uint16_t I8086Core::io_read16(uint16_t port)
{
    const unsigned lo = emulator_device->mm->read_port(port) & 0xFF;
    return (uint16_t)(lo | ((emulator_device->mm->read_port((uint16_t)(port + 1)) & 0xFF) << 8));
}

void I8086Core::io_write16(uint16_t port, uint16_t value)
{
    emulator_device->mm->write_port(port, value & 0xFF);
    emulator_device->mm->write_port((uint16_t)(port + 1), value >> 8);
}

//Right after the access: last_device is the memory that has just answered
unsigned I8086Core::mem_wait(uint32_t address, bool write, unsigned offset)
{
    AddressableDevice * d = emulator_device->mm->last_device;
    if (d == nullptr || d->wait_source == nullptr) return 0;
    return d->wait_source->wait_states(address, write, offset);
}

uint8_t I8086Core::int_ack()
{
    emulator_device->i_inta.change(1);
    const uint8_t v = (uint8_t)emulator_device->i_vector.value;
    emulator_device->i_inta.change(0);
    return v;
}

// ---------------------------  Emulator device --------------------------------

i8086::i8086(InterfaceManager *im, EmulatorConfigDevice *cd, int family):
      CPU(im, cd)
    , i_intr(this, im, 1, "intr", MODE_R, CALLBACK_INTR)
    , i_nmi(this, im, 1, "nmi", MODE_R, CALLBACK_NMI)
    , i_inta(this, im, 1, "inta", MODE_W)
    , i_vector(this, im, 8, "vector", MODE_R)
    , m_family(family)
{
    core = new I8086Core(this, family);

    over_commands.push_back(0xE8);  //CALL
    over_commands.push_back(0x9A);  //CALL far
    over_commands.push_back(0xCD);  //INT
}

i8086::~i8086()
{
    delete core;
}

emulator::Result i8086::load_config(SystemData *sd)
{
    emulator::Result res = CPU::load_config(sd);
    if (!res) return res;
    try {
        //Wait states of every I/O cycle: the IBM PC adds one
        core->io_wait = read_confg_value(cd, "io_wait", false, (unsigned int)0);
    } catch (std::exception &) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "Incorrect parameters for")) + "} " + name);
    }
    return emulator::Result::ok();
}

DisAsm * i8086::create_disasm(MAYBE_UNUSED const std::string &data_path, emulator::Result &res)
{
    res = emulator::Result::ok();
    return new DisAsmX86(&core->get_context()->s[CS]);
}

void i8086::reset(bool cold)
{
    CPU::reset(cold);
    //Lines start at _FFFF: left there, the first acknowledge would not be a
    //rising edge for the controller, and the vector would never come. Here
    //and not in load_config(): the controller links to this line after it
    i_inta.change(0);
}

unsigned int i8086::get_pc()
{
    return core->get_pc();
}

unsigned int i8086::get_command()
{
    return peek_mem(get_pc());
}

unsigned int i8086::read_mem(unsigned int address)
{
    return mm->read(address);
}

void i8086::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

void i8086::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_INTR:
        core->set_intr((new_value & 1) != 0);
        break;
    case CALLBACK_NMI:
        core->set_nmi((new_value & 1) != 0);
        break;
    }
}

unsigned int i8086::execute()
{
    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    if (m_debug == DEBUG_STOPPED)
        return 0;

    //Cycles a bus master has taken off the bus, see CPU::hold()
    unsigned int held = take_hold();
    if (held > 0) {
        core->idle(held);
        return held;
    }

    m_history[m_history_pos++ & (HISTORY_SIZE - 1)] = core->get_pc();
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

std::vector<std::pair<std::string, std::string>> i8086::get_registers()
{
    i8086context * c = core->get_context();
    return {
        {"AX", hex_str(c->r[AX], 4)},
        {"BX", hex_str(c->r[BX], 4)},
        {"CX", hex_str(c->r[CX], 4)},
        {"DX", hex_str(c->r[DX], 4)},
        {"SI", hex_str(c->r[SI], 4)},
        {"DI", hex_str(c->r[DI], 4)},
        {"BP", hex_str(c->r[BP], 4)},
        {"SP", hex_str(c->r[SP], 4)},
        {"-1", ""},
        {"CS", hex_str(c->s[CS], 4)},
        {"DS", hex_str(c->s[DS], 4)},
        {"ES", hex_str(c->s[ES], 4)},
        {"SS", hex_str(c->s[SS], 4)},
        {"-2", ""},
        {"IP", hex_str((core->get_pc() - ((uint32_t)c->s[CS] << 4)) & 0xFFFF, 4)},
        {"F", hex_str(core->get_flags(), 4)}
    };
}

std::vector<std::pair<std::string, std::string>> i8086::get_flags()
{
    const uint16_t f = core->get_flags();
    return {
        {"O", std::to_string((f & F_OF) ? 1 : 0)},
        {"D", std::to_string((f & F_DF) ? 1 : 0)},
        {"I", std::to_string((f & F_IF) ? 1 : 0)},
        {"T", std::to_string((f & F_TF) ? 1 : 0)},
        {"S", std::to_string((f & F_SF) ? 1 : 0)},
        {"Z", std::to_string((f & F_ZF) ? 1 : 0)},
        {"A", std::to_string((f & F_AF) ? 1 : 0)},
        {"P", std::to_string((f & F_PF) ? 1 : 0)},
        {"C", std::to_string((f & F_CF) ? 1 : 0)}
    };
}

//The registers by name, IP and CS through the core (the queue is emptied), PC
//as a linear address taken in the current code segment
void i8086::set_context_value(const std::string &name, unsigned int value)
{
    i8086context * c = core->get_context();
    static const char * regs[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
    static const char * segs[] = {"ES", "CS", "SS", "DS"};
    for (unsigned i = 0; i < 8; i++)
        if (name == regs[i]) { c->r[i] = (uint16_t)value; return; }
    for (unsigned i = 0; i < 4; i++)
        if (name == segs[i]) {
            if (i == CS) core->set_cs((uint16_t)value);
            else c->s[i] = (uint16_t)value;
            return;
        }
    if (name == "IP") { core->set_ip((uint16_t)value); return; }
    if (name == "PC") { core->set_ip((uint16_t)(value - ((uint32_t)c->s[CS] << 4))); return; }
    if (name == "F" || name == "FLAGS") { core->set_flags((uint16_t)value); return; }
    static const char * flags[] = {"C", "P", "A", "Z", "S", "T", "I", "D", "O"};
    static const uint16_t bits[] = {F_CF, F_PF, F_AF, F_ZF, F_SF, F_TF, F_IF, F_DF, F_OF};
    for (unsigned i = 0; i < 9; i++)
        if (name == flags[i]) {
            uint16_t f = core->get_flags();
            f = (value & 1) ? (uint16_t)(f | bits[i]) : (uint16_t)(f & ~bits[i]);
            core->set_flags(f);
            return;
        }
}

//The queue goes in too, with its bytes as they were fetched: the program may
//have changed the memory behind them since, and on the chip that change is
//not seen until the queue is emptied. Times are absolute and restored as such:
//the core only ever compares them with each other
void i8086::save_state(StateWriter &w)
{
    CPU::save_state(w);
    i8086context * c = core->get_context();
    i8086bus_state * b = core->get_bus_state();
    w.array("regs", c->r, 8);
    w.array("segs", c->s, 4);
    w.u("IP", c->ip);
    w.u("F", core->get_flags());
    w.b("halted", c->halted);

    w.n64("t", b->t);
    w.n64("bus_free", b->bus_free);
    w.n64("pf_ready", b->pf_ready);
    w.u("pf_ip", b->pf_ip);
    uint8_t q[QUEUE_MAX];
    uint32_t lag[QUEUE_MAX];
    for (unsigned i = 0; i < b->q_len; i++) {
        const unsigned k = (b->q_head + i) % QUEUE_MAX;
        q[i] = b->q[k];
        lag[i] = (b->q_avail[k] > b->t) ? (uint32_t)(b->q_avail[k] - b->t) : 0;
    }
    w.n("q_len", b->q_len);
    if (b->q_len > 0) {
        w.array("queue", q, b->q_len);
        w.array("queue_lag", lag, b->q_len);
    }
    w.b("nmi_pending", b->nmi_pending);
    w.b("inhibit", b->inhibit);
    w.b("trap", b->trap);
    w.b("rep_active", b->rep_active);
    if (b->rep_active) {
        w.u("rep_opcode", b->rep_opcode, 8);
        w.u("rep_prefix", b->rep_prefix, 8);
        w.n("rep_seg", (uint32_t)(b->rep_seg + 1));
        w.u("rep_ip", b->rep_ip);
        w.u("rep_last_prefix_ip", b->rep_last_prefix_ip);
    }
}

emulator::Result i8086::load_state(const StateReader &r)
{
    emulator::Result res = CPU::load_state(r);
    if (!res) return res;
    i8086context * c = core->get_context();
    i8086bus_state * b = core->get_bus_state();
    r.array("regs", c->r, 8);
    r.array("segs", c->s, 4);
    r.u("IP", c->ip);
    uint16_t f = core->get_flags();
    r.u("F", f);
    core->set_flags(f);
    r.b("halted", c->halted);

    r.n64("t", b->t);
    r.n64("bus_free", b->bus_free);
    r.n64("pf_ready", b->pf_ready);
    r.u("pf_ip", b->pf_ip);
    //A snapshot may come from a link: what does not fit the chip is refused
    //rather than run past its queue or its segment registers
    const emulator::Result bad = emulator::Result::error(emulator::ErrorCode::FileError,
        "{MachineState|Saved state} " + name + ": the processor state does not fit the chip");
    uint32_t n = 0;
    r.u("q_len", n);
    if (n > core->queue_size()) return bad;
    uint8_t q[QUEUE_MAX] = {0};
    uint32_t lag[QUEUE_MAX] = {0};
    r.array("queue", q, n);
    r.array("queue_lag", lag, n);
    b->q_head = 0;
    b->q_len = n;
    for (unsigned i = 0; i < n; i++) {
        b->q[i] = q[i];
        b->q_avail[i] = b->t + lag[i];
    }
    r.b("nmi_pending", b->nmi_pending);
    r.b("inhibit", b->inhibit);
    r.b("trap", b->trap);
    b->rep_active = false;
    r.b("rep_active", b->rep_active);
    if (b->rep_active) {
        r.u("rep_opcode", b->rep_opcode);
        r.u("rep_prefix", b->rep_prefix);
        uint32_t seg = 0;
        r.u("rep_seg", seg);
        b->rep_seg = (int)seg - 1;
        r.u("rep_ip", b->rep_ip);
        r.u("rep_last_prefix_ip", b->rep_last_prefix_ip);
        //MOVS, CMPS, STOS, LODS, SCAS; REP or REPNE; no override or ES..DS
        const uint8_t op = b->rep_opcode;
        const bool string_op = (op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF);
        if (!string_op || (b->rep_prefix != 0xF2 && b->rep_prefix != 0xF3) || seg > 4) return bad;
    }
    return emulator::Result::ok();
}

//The lines were restored without their callbacks: the core takes their
//levels from them now
void i8086::state_restored()
{
    CPU::state_restored();
    i8086bus_state * b = core->get_bus_state();
    b->intr = i_intr.linked > 0 && (i_intr.value & 1) != 0;
    b->nmi_level = i_nmi.linked > 0 && (i_nmi.value & 1) != 0;
}

std::vector<DeviceFieldInfo> i8086::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = CPU::get_device_fields();
    r.push_back({"queue",   "Bytes in the prefetch queue",              false});
    r.push_back({"time",    "Clocks the processor has run since start, low 32 bits", false});
    r.push_back({"history", "Linear addresses of the last instructions, oldest first (history(n): the last n)", true});
    r.push_back({"interrupts", "The last interrupts entered as vector:AX@return address, oldest first", true});
    return r;
}

bool i8086::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    i8086bus_state * b = core->get_bus_state();
    if (field == "queue") {
        out.numeric = true;
        out.width = 8;
        for (unsigned i = 0; i < b->q_len; i++)
            out.values.push_back(b->q[(b->q_head + i) % QUEUE_MAX]);
        return true;
    }
    if (field == "history") {
        //A repeated string instruction shows once per pass
        const unsigned n = (from == 0 || from > HISTORY_SIZE) ? (unsigned)HISTORY_SIZE : from;
        out.numeric = true;
        out.width = 20;
        for (unsigned i = n; i > 0; i--)
            out.values.push_back(m_history[(m_history_pos - i) & (HISTORY_SIZE - 1)]);
        return true;
    }
    if (field == "interrupts") {
        //"vector:AX@return" per entry, oldest first
        const unsigned size = i8086core::INT_LOG_SIZE;
        const unsigned n = (from == 0 || from > size) ? size : from;
        for (unsigned i = n; i > 0; i--) {
            const i8086core::IntRecord &r = core->int_log[(core->int_log_pos - i) & (size - 1)];
            if (!out.text.empty()) out.text += " ";
            out.text += hex_str(r.vector, 2) + ":" + hex_str(r.ax, 4) + "@" + hex_str(r.from, 5);
        }
        return true;
    }
    if (field == "time") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back((unsigned int)b->t);
        return true;
    }
    return CPU::get_field(field, from, to, out);
}

ComputerDevice * create_i8086(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new i8086(im, cd, I8086_FAMILY_8086);
}

ComputerDevice * create_i8088(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new i8086(im, cd, I8086_FAMILY_8088);
}
