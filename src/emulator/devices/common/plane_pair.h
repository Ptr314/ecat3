// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A word-wide address space built from two byte-wide memory planes

#pragma once

#include "emulator/core.h"

// Two memories side by side, one holding the low byte of every word and the
// other the high one. The УК-НЦ builds the central processor's address space
// this way: a word at address A is the byte A/2 of plane 1 plus the byte A/2 of
// plane 2, so 64 KB of address space costs 32 KB of each plane. The video
// controller reads the same planes a byte at a time, which is why the memory
// is laid out like this in the first place.
//
// A byte access picks a plane by bit 0 of the address, a word access takes one
// byte from each - so the device is exactly half as deep as it is wide.
class PlanePair: public AddressableDevice
{
private:
    Memory * m_low = nullptr;       // even addresses, low byte of a word
    Memory * m_high = nullptr;      // odd addresses, high byte of a word

    // Offset in the planes that address 0 of this device maps to, in bytes of a
    // plane. The УК-НЦ keeps the screen in the upper half of every plane, out
    // of reach of the processor
    unsigned int m_base = 0;

    // The planes' own buffers, when both are plain RAM with no access hook:
    // every word the УК-НЦ central processor touches comes through here, and
    // two virtual Memory calls per word were a measurable part of its cost.
    // Taken in reset(), after every device has loaded (and set its hooks)
    uint8_t * m_low_buf = nullptr;
    uint8_t * m_high_buf = nullptr;
    unsigned int m_depth = 0;       // indexes both buffers hold
    unsigned int index_of(unsigned int address) const {
        return m_base + (address >> 1);
    }

public:
    PlanePair(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_plane_pair(InterfaceManager *im, EmulatorConfigDevice *cd);
