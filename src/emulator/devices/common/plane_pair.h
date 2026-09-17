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
//
// With `index = 1` the address is the byte index in the planes instead, and
// every address holds a whole word. That is how the indirect registers see the
// planes: the value written to 176640 on the УК-НЦ is a plane index, not a
// processor address. Taking it for an address halved it, so writing through
// "address" 070075 landed on the processor word 070074.
class PlanePair: public AddressableDevice
{
private:
    Memory * m_low = nullptr;       // even addresses, low byte of a word
    Memory * m_high = nullptr;      // odd addresses, high byte of a word

    // Offset in the planes that address 0 of this device maps to, in bytes of a
    // plane. The УК-НЦ keeps the screen in the upper half of every plane, out
    // of reach of the processor
    unsigned int m_base = 0;

    bool m_index = false;           // the address is a plane index
    unsigned int index_of(unsigned int address) const {
        return m_base + (m_index? address : (address >> 1));
    }

public:
    PlanePair(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_plane_pair(InterfaceManager *im, EmulatorConfigDevice *cd);
