// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Wait states chosen by the address of the cycle

#pragma once

#include <vector>

#include "emulator/core.h"

// A WaitSource that hands each cycle to another one by its address: the
// first @wait[lo-hi] range holding it, or "default" (no wait without one).
// It stands where a memory's or a mapper's "wait" is, and lets registers of
// one device answer apart: on the УК-НЦ a write to 177010, 177012 or 177030
// takes the peripheral processor 8 clocks longer than to the other
// registers, 177024 4 longer, a read of the ROM at 100000-137777 4 longer
// than at 140000 (К1515ХМ2-001; Alex_K's PRDWR).
//
// The address is the processor's, as the bus timing hands it on. The
// sources keep their own state and are clocked as devices of their own.
class WaitMap: public ComputerDevice, public WaitSource
{
private:
    struct Range {
        unsigned int lo, hi;
        WaitSource * wait;
    };
    std::vector<Range> m_ranges;
    WaitSource * m_default = nullptr;

    WaitSource * source(unsigned int address) const
    {
        for (const Range & r : m_ranges)
            if (address >= r.lo && address <= r.hi) return r.wait;
        return m_default;
    }

public:
    WaitMap(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;

    unsigned int wait_states(unsigned int address, bool write, unsigned int offset) override;
    unsigned int wait_cycle(unsigned int address, unsigned int flags, unsigned int offset) override;
};

ComputerDevice * create_wait_map(InterfaceManager *im, EmulatorConfigDevice *cd);
