// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК programmable timer device

#include "emulator/utils.h"
#include "bk_timer.h"

// Three 16-bit registers occupying six bytes of the I/O page:
//   +0 (0177706) preset, +2 (0177710) counter, +4 (0177712) control
#define REG_PRESET  0
#define REG_COUNTER 2
#define REG_CONTROL 4

// Bits 5-6 of the control register divide the counting rate by 1, 16, 4 or 64
static const unsigned int TIMER_RATES[4] = {1, 16, 4, 64};

BKTimer::BKTimer(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_out(this, im, 1, "out", MODE_W)
    , m_divider(128)
    , m_ticks(0)
    , m_prescaler(0)
    , m_preset(0)
    , m_counter(0)
    , m_control(BK_TIMER_HIGH_BITS)
{
    m_clocked = true;   //clock() is overridden here
    addresable_size = 6;
    can_read = true;
    can_write = true;
}

emulator::Result BKTimer::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    // One count every 128 system clocks gives the 42.9 us step of a 3 MHz БК
    m_divider = read_confg_value(cd, "divider", false, (unsigned int)128);
    if (m_divider == 0) m_divider = 1;

    return emulator::Result::ok();
}

void BKTimer::reset(MAYBE_UNUSED bool cold)
{
    m_preset = 0;
    m_counter = 0;
    m_control = BK_TIMER_HIGH_BITS;
    m_ticks = 0;
    m_prescaler = 0;
    i_out.change(0);
}

void BKTimer::tick()
{
    // Bit 0 holds the counter loaded and stops it
    if ((m_control & BK_TIMER_PRESET) != 0) {
        m_counter = m_preset;
        return;
    }

    if ((m_control & BK_TIMER_ENABLE) == 0) return;

    m_prescaler++;
    if (m_prescaler < TIMER_RATES[(m_control >> BK_TIMER_DIV_SHIFT) & BK_TIMER_DIV_MASK]) return;
    m_prescaler = 0;

    m_counter--;

    if (m_counter != 0) return;

    // In the wraparound mode the counter just keeps going down from 0177777
    // and passing zero is not signalled at all
    if ((m_control & BK_TIMER_WRAP) != 0) return;

    // A single pass stops the counting
    if ((m_control & BK_TIMER_ONESHOT) != 0) m_control &= ~BK_TIMER_ENABLE;

    m_counter = m_preset;

    if ((m_control & BK_TIMER_INDICATE) != 0) {
        m_control |= BK_TIMER_FLAG;
        i_out.change(1);
    }
}

void BKTimer::clock(unsigned int counter)
{
    m_ticks += counter;
    while (m_ticks >= m_divider) {
        m_ticks -= m_divider;
        tick();
    }
}

unsigned int BKTimer::get_value_word(unsigned int address)
{
    switch (address & 0x06) {
    case REG_PRESET:  return m_preset;
    case REG_COUNTER: return m_counter;
    default:          return m_control;
    }
}

void BKTimer::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    switch (address & 0x06) {
    case REG_PRESET:
        m_preset = (uint16_t)value;
        break;
    case REG_COUNTER:
        // The counter itself is read only
        break;
    default:
        // Writing the control register starts the timer, which loads the
        // counter from the preset and restarts the rate divider
        m_control = (uint16_t)(BK_TIMER_HIGH_BITS | (value & 0xFF));
        m_counter = m_preset;
        m_prescaler = 0;
        if ((m_control & BK_TIMER_FLAG) == 0) i_out.change(0);
        break;
    }
}

unsigned int BKTimer::get_value(unsigned int address)
{
    unsigned int v = get_value_word(address);
    return (address & 1)? ((v >> 8) & 0xFF) : (v & 0xFF);
}

void BKTimer::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int v = get_value_word(address);
    if (address & 1) v = (v & 0x00FF) | ((value & 0xFF) << 8);
    else             v = (v & 0xFF00) | (value & 0xFF);
    set_value_word(address, v, force);
}

unsigned int BKTimer::get_direct(unsigned int address)
{
    return get_value_word(address);
}

std::vector<DeviceFieldInfo> BKTimer::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"preset",    "Reload value, register 0177706",                       false});
    r.push_back({"counter",   "Current count, register 0177710",                      false});
    r.push_back({"control",   "Control register 0177712, raw",                        false});
    r.push_back({"flags",     "Control register decoded as name=value pairs",         false});
    r.push_back({"rate",      "Count rate divider selected by bits 5-6: 1, 16, 4, 64", false});
    r.push_back({"divider",   "System clock ticks per timer tick",                    false});
    r.push_back({"out",       "State of the out line, the flag that can interrupt",   false});
    return r;
}

bool BKTimer::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //The three registers are 16 bit, so they ignore an 8 bit LOGDEFS width
    if (field == "preset" || field == "counter" || field == "control")
    {
        out.numeric = true;
        out.width = 16;
        if (field == "preset")       out.values.push_back(m_preset);
        else if (field == "counter") out.values.push_back(m_counter);
        else                         out.values.push_back(m_control);
        return true;
    }

    if (field == "rate")
    {
        out.numeric = true;
        out.values.push_back(TIMER_RATES[(m_control >> BK_TIMER_DIV_SHIFT) & BK_TIMER_DIV_MASK]);
        return true;
    }

    if (field == "divider")
    {
        out.numeric = true;
        out.values.push_back(m_divider);
        return true;
    }

    if (field == "out")
    {
        out.numeric = true;
        out.values.push_back(i_out.value & 1);
        return true;
    }

    //Rendered the way a CPU renders its flags: the control register is a set of
    //named bits, and reading them out of a hex value every time is a waste
    if (field == "flags")
    {
        out.numeric = false;
        out.text =
            std::string("PRESET=")   + ((m_control & BK_TIMER_PRESET)   ? "1" : "0")
                     + " WRAP="      + ((m_control & BK_TIMER_WRAP)     ? "1" : "0")
                     + " INDICATE="  + ((m_control & BK_TIMER_INDICATE) ? "1" : "0")
                     + " ONESHOT="   + ((m_control & BK_TIMER_ONESHOT)  ? "1" : "0")
                     + " ENABLE="    + ((m_control & BK_TIMER_ENABLE)   ? "1" : "0")
                     + " FLAG="      + ((m_control & BK_TIMER_FLAG)     ? "1" : "0")
                     + " RATE="      + std::to_string(TIMER_RATES[(m_control >> BK_TIMER_DIV_SHIFT) & BK_TIMER_DIV_MASK]);
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_bk_timer(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new BKTimer(im, cd);
}
