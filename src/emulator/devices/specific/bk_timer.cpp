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

BKTimer::BKTimer(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_out(this, im, 1, "out", MODE_W)
    , m_divider(128)
    , m_ticks(0)
    , m_preset(0)
    , m_counter(0)
    , m_control(0)
{
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
    m_control = 0;
    m_ticks = 0;
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

    m_counter--;

    if (m_counter != 0) return;

    // Passing through zero
    if ((m_control & BK_TIMER_INDICATE) != 0) {
        m_control |= BK_TIMER_FLAG;
        i_out.change(1);
    }

    if ((m_control & BK_TIMER_NO_REPEAT) == 0) m_counter = m_preset;

    // Without the continuous mode the timer stops after a single pass
    if ((m_control & BK_TIMER_CONTINUOUS) == 0) m_control &= ~BK_TIMER_ENABLE;
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
        m_counter = (uint16_t)value;
        break;
    default:
        // Writing the control register starts the timer, which loads the
        // counter from the preset. The flag is status only and the write
        // clears it.
        m_control = (uint16_t)(value & 0x1F);
        m_counter = m_preset;
        i_out.change(0);
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

ComputerDevice * create_bk_timer(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new BKTimer(im, cd);
}
