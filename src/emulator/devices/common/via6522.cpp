// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MOS 6522 versatile interface adapter (VIA)

#include <climits>

#include "via6522.h"
#include "emulator/utils.h"

#define CB_CA1      1
#define CB_CB1      2
#define CB_CA2_IN   3
#define CB_CB2_IN   4
#define CB_PB_IN    5

// IFR bits
#define IF_CA2      0x01
#define IF_CA1      0x02
#define IF_SR       0x04
#define IF_CB2      0x08
#define IF_CB1      0x10
#define IF_T2       0x20
#define IF_T1       0x40

// ACR bits
#define ACR_PA_LATCH    0x01
#define ACR_PB_LATCH    0x02
#define ACR_SR          0x1C
#define ACR_T2_COUNT    0x20
#define ACR_T1_FREE     0x40
#define ACR_T1_PB7      0x80

// The CA2/CB2 control field of the PCR
#define C2_IN_NEG       0
#define C2_IN_NEG_IND   1
#define C2_IN_POS       2
#define C2_IN_POS_IND   3
#define C2_HANDSHAKE    4
#define C2_PULSE        5
#define C2_LOW          6
#define C2_HIGH         7

VIA6522::VIA6522(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_pa(this, im, 8, "pa", MODE_W)
    , i_pa_in(this, im, 8, "pa_in", MODE_R)
    , i_pb(this, im, 8, "pb", MODE_W)
    , i_pb_in(this, im, 8, "pb_in", MODE_R, CB_PB_IN)
    , i_ca1(this, im, 1, "ca1", MODE_R, CB_CA1)
    , i_ca2(this, im, 1, "ca2", MODE_W)
    , i_ca2_in(this, im, 1, "ca2_in", MODE_R, CB_CA2_IN)
    , i_cb1(this, im, 1, "cb1", MODE_R, CB_CB1)
    , i_cb2(this, im, 1, "cb2", MODE_W)
    , i_cb2_in(this, im, 1, "cb2_in", MODE_R, CB_CB2_IN)
    , i_irq(this, im, 1, "irq", MODE_W)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 16;
}

emulator::Result VIA6522::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    VIA6522::reset(true);
    return emulator::Result::ok();
}

// ~RES clears the port and control registers and the interrupts; the timers,
// their latches and the shift register keep what they hold
void VIA6522::reset(bool cold)
{
    if (cold) {
        m_t1_latch = m_t1 = 0xFFFF;
        m_t2_latch_lo = 0xFF;
        m_t2 = 0xFFFF;
        m_sr = 0;
    }
    m_ora = m_orb = m_ddra = m_ddrb = 0;
    m_ira_latch = m_irb_latch = 0;
    m_acr = m_pcr = m_ifr = m_ier = 0;
    m_t1_reload = false;
    m_t1_armed = m_t2_armed = false;
    m_t2_hold = false;
    m_pb7 = true;
    m_ca2_pulse = m_cb2_pulse = false;
    output_a();
    output_b();
    m_ca2_out = m_cb2_out = false;
    apply_pcr();
    m_irq_out = 0;
    update_irq();
}

//------------------- Interrupts --------------------------------------------//

void VIA6522::set_flag(uint8_t bits)
{
    m_ifr |= bits;
    update_irq();
}

void VIA6522::clear_flag(uint8_t bits)
{
    m_ifr &= ~bits;
    update_irq();
}

void VIA6522::update_irq()
{
    const unsigned v = (m_ifr & m_ier & 0x7F) ? 0 : 1;
    if (v != m_irq_out) {
        m_irq_out = v;
        i_irq.change(v);
    }
}

//------------------- Ports and handshake ------------------------------------//

// A pin set as an input is not driven and reads as 1 to whatever is wired to it
void VIA6522::output_a()
{
    i_pa.change((m_ora & m_ddra) | (~m_ddra & 0xFF));
}

void VIA6522::output_b()
{
    unsigned v = (m_orb & m_ddrb) | (~m_ddrb & 0xFF);
    if (m_acr & ACR_T1_PB7) v = (v & 0x7F) | (m_pb7 ? 0x80 : 0);
    i_pb.change(v);
}

uint8_t VIA6522::read_a() const
{
    if (m_acr & ACR_PA_LATCH) return m_ira_latch;
    return (uint8_t)((m_ora & m_ddra) | (i_pa_in.value & ~m_ddra & 0xFF));
}

uint8_t VIA6522::read_b() const
{
    unsigned in = (m_acr & ACR_PB_LATCH) ? m_irb_latch : (i_pb_in.value & 0xFF);
    unsigned v = (m_orb & m_ddrb) | (in & ~m_ddrb & 0xFF);
    if (m_acr & ACR_T1_PB7) v = (v & 0x7F) | (m_pb7 ? 0x80 : 0);
    return (uint8_t)v;
}

void VIA6522::set_ca2(bool v)
{
    if (v == m_ca2_out) return;
    m_ca2_out = v;
    i_ca2.change(v ? 1 : 0);
}

void VIA6522::set_cb2(bool v)
{
    if (v == m_cb2_out) return;
    m_cb2_out = v;
    i_cb2.change(v ? 1 : 0);
}

// The output modes drive the line at once; a handshake or pulse line rests
// high, and an input is not driven at all (1)
void VIA6522::apply_pcr()
{
    const unsigned ca = (m_pcr >> 1) & 7, cb = (m_pcr >> 5) & 7;
    m_ca2_pulse = m_cb2_pulse = false;
    set_ca2(ca != C2_LOW);
    set_cb2(cb != C2_LOW);
}

// A read or write of ORA (register 1): clears the CA1 flag and, unless the
// CA2 input is independent, the CA2 one; a handshake or pulse output goes low
void VIA6522::ca2_access()
{
    const unsigned ca = (m_pcr >> 1) & 7;
    uint8_t clr = IF_CA1;
    if (ca == C2_IN_NEG || ca == C2_IN_POS) clr |= IF_CA2;
    clear_flag(clr);
    if (ca == C2_HANDSHAKE) set_ca2(false);
    else if (ca == C2_PULSE) { set_ca2(false); m_ca2_pulse = true; }
}

// The same for ORB, but only a write moves CB2
void VIA6522::cb2_access()
{
    const unsigned cb = (m_pcr >> 5) & 7;
    uint8_t clr = IF_CB1;
    if (cb == C2_IN_NEG || cb == C2_IN_POS) clr |= IF_CB2;
    clear_flag(clr);
}

void VIA6522::ca1_edge(bool level)
{
    if (level != ((m_pcr & 0x01) != 0)) return;
    m_ira_latch = (uint8_t)((m_ora & m_ddra) | (i_pa_in.value & ~m_ddra & 0xFF));
    set_flag(IF_CA1);
    if (((m_pcr >> 1) & 7) == C2_HANDSHAKE) set_ca2(true);
}

void VIA6522::cb1_edge(bool level)
{
    if (level != ((m_pcr & 0x10) != 0)) return;
    m_irb_latch = (uint8_t)(i_pb_in.value & 0xFF);
    set_flag(IF_CB1);
    if (((m_pcr >> 5) & 7) == C2_HANDSHAKE) set_cb2(true);
}

void VIA6522::ca2_edge(bool level)
{
    const unsigned ca = (m_pcr >> 1) & 7;
    if (ca >= C2_HANDSHAKE) return;
    if (level == ((ca & 2) != 0)) set_flag(IF_CA2);
}

void VIA6522::cb2_edge(bool level)
{
    const unsigned cb = (m_pcr >> 5) & 7;
    if (cb >= C2_HANDSHAKE) return;
    if (level == ((cb & 2) != 0)) set_flag(IF_CB2);
}

void VIA6522::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    const bool changed = ((new_value ^ old_value) & 1) != 0;
    const bool level = (new_value & 1) != 0;
    switch (callback_id) {
    case CB_CA1:    if (changed) ca1_edge(level); break;
    case CB_CB1:    if (changed) cb1_edge(level); break;
    case CB_CA2_IN: if (changed) ca2_edge(level); break;
    case CB_CB2_IN: if (changed) cb2_edge(level); break;
    case CB_PB_IN:
        // T2 counting pulses on PB6: each falling edge, the flag at zero
        if ((m_acr & ACR_T2_COUNT) && (old_value & 0x40) && !(new_value & 0x40)) {
            m_t2 = (uint16_t)(m_t2 - 1);
            if (m_t2 == 0 && m_t2_armed) { m_t2_armed = false; set_flag(IF_T2); }
        }
        break;
    default:
        break;
    }
}

//------------------- Timers --------------------------------------------------//

// The counter takes the latch, then steps N, N-1 ... 0, $FFFF: the flag is
// set as it passes zero, N+2 clocks after the load (the data sheet's N+1.5,
// as the processor sees it). Free running, it takes the latch again on the
// clock after, which makes the period N+2
void VIA6522::t1_run(unsigned n)
{
    while (n > 0) {
        if (m_t1_reload) {
            m_t1 = m_t1_latch;
            m_t1_reload = false;
            n--;
            continue;
        }
        const unsigned steps = (unsigned)m_t1 + 1;
        if (n < steps) { m_t1 = (uint16_t)(m_t1 - n); return; }
        n -= steps;
        m_t1 = 0xFFFF;
        const bool free = (m_acr & ACR_T1_FREE) != 0;
        if (free || m_t1_armed) {
            set_flag(IF_T1);
            if (m_acr & ACR_T1_PB7) {
                m_pb7 = free ? !m_pb7 : true;
                output_b();
            }
        }
        if (free) m_t1_reload = true;
        else m_t1_armed = false;
    }
}

// One-shot only: the flag once, then the counter rolls on
void VIA6522::t2_run(unsigned n)
{
    if (m_acr & ACR_T2_COUNT) return;
    if (m_t2_hold) {
        m_t2_hold = false;
        if (--n == 0) return;
    }
    if (n > m_t2 && m_t2_armed) {
        m_t2_armed = false;
        set_flag(IF_T2);
    }
    m_t2 = (uint16_t)(m_t2 - n);
}

void VIA6522::clock(unsigned int counter)
{
    if (counter == 0) return;
    if (m_ca2_pulse) { m_ca2_pulse = false; set_ca2(true); }
    if (m_cb2_pulse) { m_cb2_pulse = false; set_cb2(true); }
    t1_run(counter);
    t2_run(counter);
}

// When the next timeout falls, for the devices that wait on events (WAIT of
// the 1801 processors); the 6502 does not ask
unsigned int VIA6522::clocks_to_event() const
{
    unsigned r = UINT_MAX;
    if (m_ca2_pulse || m_cb2_pulse) return 1;
    if ((m_acr & ACR_T1_FREE) || m_t1_armed)
        r = m_t1_reload ? (unsigned)m_t1_latch + 2 : (unsigned)m_t1 + 1;
    const unsigned t2 = (unsigned)m_t2 + 1 + (m_t2_hold ? 1 : 0);
    if (m_t2_armed && !(m_acr & ACR_T2_COUNT) && t2 < r) r = t2;
    return r;
}

//------------------- Bus ------------------------------------------------------//

unsigned VIA6522::read_reg(unsigned reg) const
{
    switch (reg & 0x0F) {
    case 0x0: return read_b();
    case 0x1: case 0x0F: return read_a();
    case 0x2: return m_ddrb;
    case 0x3: return m_ddra;
    case 0x4: return m_t1 & 0xFF;
    case 0x5: return m_t1 >> 8;
    case 0x6: return m_t1_latch & 0xFF;
    case 0x7: return m_t1_latch >> 8;
    case 0x8: return m_t2 & 0xFF;
    case 0x9: return m_t2 >> 8;
    case 0xA: return m_sr;
    case 0xB: return m_acr;
    case 0xC: return m_pcr;
    case 0xD: return m_ifr | ((m_ifr & m_ier & 0x7F) ? 0x80 : 0);
    case 0xE: return m_ier | 0x80;
    }
    return 0xFF;
}

unsigned int VIA6522::get_direct(unsigned int address)
{
    return read_reg(address);
}

unsigned int VIA6522::get_value(unsigned int address)
{
    const unsigned reg = address & 0x0F;
    const unsigned v = read_reg(reg);
    switch (reg) {
    case 0x0: cb2_access(); break;
    case 0x1: ca2_access(); break;
    case 0x4: clear_flag(IF_T1); break;
    case 0x8: clear_flag(IF_T2); break;
    case 0xA: clear_flag(IF_SR); break;
    default: break;
    }
    return v;
}

void VIA6522::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    const unsigned reg = address & 0x0F;
    const uint8_t v = (uint8_t)value;
    switch (reg) {
    case 0x0:
        m_orb = v;
        output_b();
        cb2_access();
        {
            const unsigned cb = (m_pcr >> 5) & 7;
            if (cb == C2_HANDSHAKE) set_cb2(false);
            else if (cb == C2_PULSE) { set_cb2(false); m_cb2_pulse = true; }
        }
        break;
    case 0x1:
        m_ora = v;
        output_a();
        ca2_access();
        break;
    case 0xF:
        m_ora = v;
        output_a();
        break;
    case 0x2: m_ddrb = v; output_b(); break;
    case 0x3: m_ddra = v; output_a(); break;
    case 0x4: case 0x6:
        m_t1_latch = (uint16_t)((m_t1_latch & 0xFF00) | v);
        break;
    case 0x5:
        m_t1_latch = (uint16_t)((m_t1_latch & 0x00FF) | (v << 8));
        // Loaded on the next clock, as a free running reload: the flag comes
        // N+2 clocks after the write, where the processor sees the N+1.5 of
        // the data sheet (Oricutron counts the same)
        m_t1 = m_t1_latch;
        m_t1_reload = true;
        m_t1_armed = true;
        clear_flag(IF_T1);
        if (m_acr & ACR_T1_PB7) { m_pb7 = false; output_b(); }
        break;
    case 0x7:
        m_t1_latch = (uint16_t)((m_t1_latch & 0x00FF) | (v << 8));
        clear_flag(IF_T1);
        break;
    case 0x8:
        m_t2_latch_lo = v;
        break;
    case 0x9:
        m_t2 = (uint16_t)((v << 8) | (m_t2_latch_lo & 0xFF));
        m_t2_armed = true;
        m_t2_hold = true;
        clear_flag(IF_T2);
        break;
    case 0xA:
        m_sr = v;
        clear_flag(IF_SR);
        break;
    case 0xB:
        if ((v & ACR_SR) != 0 && (m_acr & ACR_SR) != (v & ACR_SR)) note_unsupported("shift register mode");
        m_acr = v;
        output_b();
        break;
    case 0xC:
        m_pcr = v;
        apply_pcr();
        break;
    case 0xD:
        clear_flag(v & 0x7F);
        break;
    case 0xE:
        if (v & 0x80) m_ier |= (v & 0x7F);
        else m_ier &= ~(v & 0x7F);
        update_irq();
        break;
    }
}

//------------------- Introspection --------------------------------------------//

std::vector<DeviceFieldInfo> VIA6522::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"regs", "Registers 0-F as a read would give them, without its side effects", true});
    r.push_back({"t1",   "Timer 1 counter",                      false});
    r.push_back({"t1_latch", "Timer 1 latch",                    false});
    r.push_back({"t2",   "Timer 2 counter",                      false});
    r.push_back({"ifr",  "Interrupt flags",                      false});
    r.push_back({"ier",  "Interrupt enable",                     false});
    r.push_back({"irq",  "~IRQ, 0 while an interrupt is requested", false});
    r.push_back({"pa",   "Port A pins as driven",                false});
    r.push_back({"pb",   "Port B pins as driven",                false});
    return r;
}

bool VIA6522::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "regs") {
        if (from == 0 && to == 0) to = 15;
        if (to > 15) to = 15;
        if (from > to) from = to;
        out.width = 8;
        out.has_start = true;
        out.start = from;
        for (unsigned i = from; i <= to; i++) out.values.push_back(read_reg(i));
        return true;
    }
    out.width = 16;
    if (field == "t1")       { out.values.push_back(m_t1); return true; }
    if (field == "t1_latch") { out.values.push_back(m_t1_latch); return true; }
    if (field == "t2")       { out.values.push_back(m_t2); return true; }
    out.width = 8;
    if (field == "ifr")      { out.values.push_back(read_reg(0xD)); return true; }
    if (field == "ier")      { out.values.push_back(m_ier); return true; }
    if (field == "pa")       { out.values.push_back(i_pa.value & 0xFF); return true; }
    if (field == "pb")       { out.values.push_back(i_pb.value & 0xFF); return true; }
    out.width = 0;
    if (field == "irq")      { out.values.push_back(m_irq_out); return true; }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void VIA6522::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("ora", m_ora, 8);
    w.u("orb", m_orb, 8);
    w.u("ddra", m_ddra, 8);
    w.u("ddrb", m_ddrb, 8);
    w.u("ira_latch", m_ira_latch, 8);
    w.u("irb_latch", m_irb_latch, 8);
    w.u("acr", m_acr, 8);
    w.u("pcr", m_pcr, 8);
    w.u("ifr", m_ifr, 8);
    w.u("ier", m_ier, 8);
    w.u("sr", m_sr, 8);
    w.u("t1_latch", m_t1_latch);
    w.u("t1", m_t1);
    w.u("t2_latch_lo", m_t2_latch_lo, 8);
    w.u("t2", m_t2);
    w.b("t1_reload", m_t1_reload);
    w.b("t1_armed", m_t1_armed);
    w.b("t2_armed", m_t2_armed);
    w.b("t2_hold", m_t2_hold);
    w.b("pb7", m_pb7);
    w.b("ca2_out", m_ca2_out);
    w.b("cb2_out", m_cb2_out);
    w.b("ca2_pulse", m_ca2_pulse);
    w.b("cb2_pulse", m_cb2_pulse);
    w.n("irq_out", m_irq_out);
}

emulator::Result VIA6522::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("ora", m_ora);
    r.u("orb", m_orb);
    r.u("ddra", m_ddra);
    r.u("ddrb", m_ddrb);
    r.u("ira_latch", m_ira_latch);
    r.u("irb_latch", m_irb_latch);
    r.u("acr", m_acr);
    r.u("pcr", m_pcr);
    r.u("ifr", m_ifr);
    r.u("ier", m_ier);
    r.u("sr", m_sr);
    r.u("t1_latch", m_t1_latch);
    r.u("t1", m_t1);
    r.u("t2_latch_lo", m_t2_latch_lo);
    r.u("t2", m_t2);
    r.b("t1_reload", m_t1_reload);
    r.b("t1_armed", m_t1_armed);
    r.b("t2_armed", m_t2_armed);
    r.b("t2_hold", m_t2_hold);
    r.b("pb7", m_pb7);
    r.b("ca2_out", m_ca2_out);
    r.b("cb2_out", m_cb2_out);
    r.b("ca2_pulse", m_ca2_pulse);
    r.b("cb2_pulse", m_cb2_pulse);
    r.u("irq_out", m_irq_out);
    return emulator::Result::ok();
}

ComputerDevice * create_via6522(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new VIA6522(im, cd);
}
