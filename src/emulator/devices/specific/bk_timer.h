// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК programmable timer device

#pragma once

#include "emulator/core.h"

// Control register bits
#define BK_TIMER_PRESET     (1 << 0)    // hold the counter at the preset value
#define BK_TIMER_CONTINUOUS (1 << 1)    // keep counting after passing zero
#define BK_TIMER_INDICATE   (1 << 2)    // raise the flag when passing zero
#define BK_TIMER_NO_REPEAT  (1 << 3)    // do not reload the counter from the preset
#define BK_TIMER_ENABLE     (1 << 4)    // counting enabled
#define BK_TIMER_FLAG       (1 << 7)    // set when the counter passed through zero

class BKTimer: public AddressableDevice
{
private:
    Interface i_out;                    // the flag, for configurations that need an interrupt

    unsigned int m_divider;             // system clock ticks per timer tick
    unsigned int m_ticks;               // ticks accumulated towards the next count

    uint16_t m_preset;
    uint16_t m_counter;
    uint16_t m_control;

    void tick();

public:
    BKTimer(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;
};

ComputerDevice * create_bk_timer(InterfaceManager *im, EmulatorConfigDevice *cd);
