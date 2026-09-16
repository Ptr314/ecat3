// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Indirect memory access through an address register and data registers

#pragma once

#include "emulator/core.h"

#define INDIRECT_MAX_TARGETS 4

// An address register followed by one or more data registers, each looking at a
// memory of its own. Writing the address register latches a word from every
// target; reading a data register hands back that latch, writing one puts the
// word into its target. The address then steps on if auto_increment is set.
//
// This is how the two processors of the УК-НЦ reach memory they cannot address:
// the central one has 176640 (address) and 176642 (data into planes 1 and 2),
// the peripheral one 177010 (address), 177012 (plane 0) and 177014 (planes 1
// and 2) - and through the latter it reaches all 192 KB, including everything
// the central processor can see. The reverse is not true: plane 0 is out of the
// central processor's reach entirely.
//
// The latch matters: re-reading a data register does not re-read memory, it
// returns what the last write to the address register captured. Software that
// wants a fresh word writes the address again, even the same one.
class IndirectMemory: public AddressableDevice
{
private:
    struct Target {
        AddressableDevice * device;
        unsigned int        value;      // latched on a write to the address register
    };

    Target m_targets[INDIRECT_MAX_TARGETS];
    unsigned int m_count = 0;

    unsigned int m_address = 0;
    unsigned int m_address_mask = 0xFFFF;
    bool m_auto_increment = false;

    void latch();                       // samples every target at the current address

public:
    IndirectMemory(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_indirect_memory(InterfaceManager *im, EmulatorConfigDevice *cd);
