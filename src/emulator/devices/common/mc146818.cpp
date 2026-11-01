// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MC146818 (КР512ВИ1) real time clock with battery RAM

#include <cstring>

#include "mc146818.h"
#include "emulator/utils.h"

#define REG_SEC     0
#define REG_ASEC    1
#define REG_MIN     2
#define REG_AMIN    3
#define REG_HOUR    4
#define REG_AHOUR   5
#define REG_DOW     6
#define REG_DAY     7
#define REG_MONTH   8
#define REG_YEAR    9
#define REG_A       10
#define REG_B       11
#define REG_C       12
#define REG_D       13

#define A_UIP       0200
#define B_SET       0200
#define B_PIE       0100
#define B_AIE       0040
#define B_UIE       0020
#define B_DM        0004            // 1 - binary, 0 - BCD
#define B_24H       0002
#define C_IRQF      0200
#define C_PF        0100
#define C_AF        0040
#define C_UF        0020

#define TICKS_HZ        32768u
#define UIP_TICKS       8u          // 244 us before the update

MC146818::MC146818(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irq(this, im, 1, "irq", MODE_W)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 128;
}

emulator::Result MC146818::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_battery = read_confg_value(cd, "battery", false, false);
    m_word_spaced = read_confg_value(cd, "word_spaced", false, true);
    addresable_size = m_word_spaced ? 128 : 64;
    return emulator::Result::ok();
}

void MC146818::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        memset(m_regs, 0, sizeof(m_regs));
        m_regs[REG_A] = 040;        // 32768 Hz
        m_regs[REG_B] = B_DM | B_24H;
        m_regs[REG_DAY] = 1;
        m_regs[REG_MONTH] = 1;
        m_regs[REG_YEAR] = 88;
        m_regs[REG_DOW] = 6;
        m_valid = m_battery;
        m_fraction = 0;
        m_second_ticks = 0;
        m_periodic_ticks = 0;
    }
    // The reset pin: interrupts off, flags down. Time and RAM stay
    m_regs[REG_B] &= (uint8_t)~(B_PIE | B_AIE | B_UIE);
    m_regs[REG_C] = 0;
    update_irq();
}

unsigned int MC146818::index_of(unsigned int address) const
{
    return (m_word_spaced ? (address >> 1) : address) & 077;
}

unsigned int MC146818::to_binary(unsigned int v) const
{
    return (m_regs[REG_B] & B_DM) ? v : ((v >> 4) * 10 + (v & 017));
}

unsigned int MC146818::from_binary(unsigned int v) const
{
    return (m_regs[REG_B] & B_DM) ? v : (((v / 10) << 4) | (v % 10));
}

void MC146818::advance_second()
{
    static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned int s = to_binary(m_regs[REG_SEC]) + 1;
    if (s < 60) { m_regs[REG_SEC] = (uint8_t)from_binary(s); return; }
    m_regs[REG_SEC] = (uint8_t)from_binary(0);
    unsigned int m = to_binary(m_regs[REG_MIN]) + 1;
    if (m < 60) { m_regs[REG_MIN] = (uint8_t)from_binary(m); return; }
    m_regs[REG_MIN] = (uint8_t)from_binary(0);

    if (m_regs[REG_B] & B_24H) {
        unsigned int h = to_binary(m_regs[REG_HOUR]) + 1;
        if (h < 24) { m_regs[REG_HOUR] = (uint8_t)from_binary(h); return; }
        m_regs[REG_HOUR] = (uint8_t)from_binary(0);
    } else {
        const unsigned int pm = m_regs[REG_HOUR] & 0200;
        unsigned int h = to_binary(m_regs[REG_HOUR] & 0177) + 1;
        if (h == 12) {
            m_regs[REG_HOUR] = (uint8_t)(from_binary(12) | (pm ^ 0200));
            if (pm == 0) return;
        } else {
            if (h == 13) h = 1;
            m_regs[REG_HOUR] = (uint8_t)(from_binary(h) | pm);
            return;
        }
    }

    unsigned int dow = m_regs[REG_DOW] + 1;
    m_regs[REG_DOW] = (uint8_t)((dow > 7) ? 1 : dow);
    const unsigned int year = to_binary(m_regs[REG_YEAR]);
    const unsigned int month = to_binary(m_regs[REG_MONTH]);
    unsigned int last = (month >= 1 && month <= 12) ? DAYS[month - 1] : 31;
    if (month == 2 && (year & 3) == 0) last = 29;
    unsigned int d = to_binary(m_regs[REG_DAY]) + 1;
    if (d <= last) { m_regs[REG_DAY] = (uint8_t)from_binary(d); return; }
    m_regs[REG_DAY] = (uint8_t)from_binary(1);
    if (month < 12) { m_regs[REG_MONTH] = (uint8_t)from_binary(month + 1); return; }
    m_regs[REG_MONTH] = (uint8_t)from_binary(1);
    m_regs[REG_YEAR] = (uint8_t)from_binary((year + 1) % 100);
}

// A byte of 300-377 in an alarm register matches anything
bool MC146818::alarm_matches() const
{
    for (unsigned int i = 0; i < 3; i++) {
        const uint8_t alarm = m_regs[REG_ASEC + 2 * i];
        if ((alarm & 0300) != 0300 && alarm != m_regs[REG_SEC + 2 * i]) return false;
    }
    return true;
}

void MC146818::tick(unsigned int ticks)
{
    // Register A, bits 6-4: the divider runs only on 010
    if ((m_regs[REG_A] & 0160) != 040) return;

    const unsigned int rate = m_regs[REG_A] & 017;
    if (rate != 0) {
        const uint32_t period = (rate < 3) ? (1u << (rate + 6)) : (1u << (rate - 1));
        m_periodic_ticks += ticks;
        if (m_periodic_ticks >= period) {
            m_periodic_ticks %= period;
            m_regs[REG_C] |= C_PF;
        }
    }

    m_second_ticks += ticks;
    if (m_second_ticks >= TICKS_HZ) {
        m_second_ticks -= TICKS_HZ;
        if ((m_regs[REG_B] & B_SET) == 0) {
            advance_second();
            m_regs[REG_C] |= C_UF;
            if (alarm_matches()) m_regs[REG_C] |= C_AF;
        }
    }
    if ((m_regs[REG_B] & B_SET) == 0 && m_second_ticks >= TICKS_HZ - UIP_TICKS)
        m_regs[REG_A] |= A_UIP;
    else
        m_regs[REG_A] &= (uint8_t)~A_UIP;
    update_irq();
}

void MC146818::clock(unsigned int counter)
{
    m_fraction += (uint64_t)counter * TICKS_HZ;
    if (m_fraction < m_system_clock) return;
    const unsigned int ticks = (unsigned int)(m_fraction / m_system_clock);
    m_fraction -= (uint64_t)ticks * m_system_clock;
    tick(ticks);
}

void MC146818::update_irq()
{
    const unsigned int flags = m_regs[REG_C] & m_regs[REG_B] & (C_PF | C_AF | C_UF);
    if (flags != 0) m_regs[REG_C] |= C_IRQF;
    else m_regs[REG_C] &= (uint8_t)~C_IRQF;
    const unsigned int line = flags != 0 ? 1 : 0;
    if (i_irq.value != line) {
        if (line) m_interrupts++;
        i_irq.change(line);
    }
}

unsigned int MC146818::read_register(unsigned int index, bool direct)
{
    switch (index) {
    case REG_C: {
        const unsigned int v = m_regs[REG_C];
        if (!direct) {
            m_regs[REG_C] = 0;
            update_irq();
        }
        return v;
    }
    case REG_D: {
        const unsigned int v = m_valid ? 0200 : 0;
        if (!direct) m_valid = true;
        return v;
    }
    default:
        return m_regs[index];
    }
}

void MC146818::write_register(unsigned int index, unsigned int value)
{
    value &= 0xFF;
    switch (index) {
    case REG_A:
        m_regs[REG_A] = (uint8_t)((m_regs[REG_A] & A_UIP) | (value & ~A_UIP));
        break;
    case REG_B:
        m_regs[REG_B] = (uint8_t)value;
        update_irq();
        break;
    case REG_C:
    case REG_D:
        break;
    default:
        m_regs[index] = (uint8_t)value;
        break;
    }
}

unsigned int MC146818::get_value_word(unsigned int address)
{
    return read_register(index_of(address), false);
}

void MC146818::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    write_register(index_of(address), value);
}

unsigned MC146818::get_direct(unsigned address)
{
    if (m_word_spaced && (address & 1)) return 0;
    return read_register(index_of(address), true);
}

unsigned int MC146818::get_value(unsigned int address)
{
    if (m_word_spaced && (address & 1)) return 0;
    return get_value_word(address);
}

void MC146818::set_value(unsigned int address, unsigned int value, bool force)
{
    if (m_word_spaced && (address & 1)) return;
    set_value_word(address, value, force);
}

std::vector<DeviceFieldInfo> MC146818::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"time",       "Registers 0-9: seconds, alarm, minutes, alarm, hours, alarm, day of week, day, month, year", false});
    r.push_back({"control",    "Registers A, B, C, D", false});
    r.push_back({"interrupts", "Interrupts requested", false});
    return r;
}

bool MC146818::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "time") {
        for (unsigned int i = REG_SEC; i <= REG_YEAR; i++) out.values.push_back(m_regs[i]);
        return true;
    }
    if (field == "control") {
        for (unsigned int i = REG_A; i <= REG_D; i++) out.values.push_back(read_register(i, true));
        return true;
    }
    if (field == "interrupts") {
        out.width = 16;
        out.values.push_back(m_interrupts);
        return true;
    }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void MC146818::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.array("regs", m_regs, sizeof(m_regs));
    w.b("valid", m_valid);
    w.n64("fraction", m_fraction);
    w.n("second_ticks", m_second_ticks);
    w.n("periodic_ticks", m_periodic_ticks);
}

emulator::Result MC146818::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.array("regs", m_regs, sizeof(m_regs));
    r.b("valid", m_valid);
    r.n64("fraction", m_fraction);
    r.u("second_ticks", m_second_ticks);
    r.u("periodic_ticks", m_periodic_ticks);
    return emulator::Result::ok();
}

ComputerDevice * create_mc146818(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new MC146818(im, cd);
}
