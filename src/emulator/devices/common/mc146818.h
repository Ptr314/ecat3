// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MC146818 (КР512ВИ1) real time clock with battery RAM

#pragma once

#include "emulator/core.h"

// Fourteen registers and 50 bytes of RAM, one byte on each word address when
// word_spaced = 1. The chip counts emulated time from 32768 Hz, so that a run
// repeats exactly. It starts from date, or, with battery = 0, from the state of
// a chip that lost its power: register D says so until it is first read.
class MC146818: public AddressableDevice
{
private:
    Interface i_irq;

    uint8_t m_regs[64] = {};
    bool m_valid = false;           // VRT, bit 7 of register D
    bool m_battery = false;
    bool m_word_spaced = true;

    uint64_t m_fraction = 0;        // of a 32768 Hz tick, in clocks * 32768
    uint32_t m_second_ticks = 0;    // ticks of the second under way
    uint32_t m_periodic_ticks = 0;

    unsigned int m_interrupts = 0;

    unsigned int index_of(unsigned int address) const;
    unsigned int to_binary(unsigned int v) const;
    unsigned int from_binary(unsigned int v) const;
    void advance_second();
    bool alarm_matches() const;
    void tick(unsigned int ticks);
    void update_irq();
    unsigned int read_register(unsigned int index, bool direct);
    void write_register(unsigned int index, unsigned int value);

public:
    MC146818(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_mc146818(InterfaceManager *im, EmulatorConfigDevice *cd);
