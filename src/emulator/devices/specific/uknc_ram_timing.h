// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ memory timing, one processor's side of the 1515ХМ1-031

#pragma once

#include "emulator/core.h"

// When the УК-НЦ memory answers a processor (timing = vm2). The planes are
// dynamic RAM behind the video controller (the early К1515ХМ1-031 or the
// revised К1515ХМ2-001, two gate arrays), which serves the raster, both
// processors and the refresh; there is no netlist of it, so this is a model
// fitted on the chip model of the КМ1801ВМ2 (cpu11) against the times
// measured on real machines with both (литера 7: Alex_K's UTEST.DSK tests,
// TSSPD by Titus, Patron's tables; tools/uknc-timing has the bench and the fit):
//
// - a cycle starts on a point of the controller's grid (`grid`, 640 ns: eight
//   points of the 12.5 MHz scan), the first one at least `setup` after the
//   strobe and after the previous cycle of the same processor is over;
// - RPLY comes `reply` after that point (`reply_write` for a write,
//   `reply_write_byte` for a byte write: plane 0 is eight bits wide, a byte
//   is one access of the two a word takes - the ПП's BISB and MOVB to
//   memory are 4 clocks shorter than BIS and MOV on the machine);
// - the cycle keeps the memory for `read`, `write` or `write_byte`, and for
//   `rmw_write` / `rmw_write_byte` when the write is the second half of a
//   read-modify-write (DATIO, which every byte write of the ВМ2 is). Plane 0
//   is eight bits wide, so a word of the peripheral processor takes two
//   accesses and a byte one: its read is 3.2 us, a word write 2.56, a byte
//   write 1.92, in a read-modify-write too. The central processor has planes
//   1 and 2 side by side, a word is one access: a read 0.96 us, a write 2.24,
//   the write of a read-modify-write 0.64 - the row is open already.
//
// Each processor has its own device: the measurements show no contention
// between them, and their clocks are unrelated (the central one has its own
// crystal). Times are absolute, counted in ps from the clocks of its domain.
//
// The central processor's clock is not locked to the grid, and the time of
// an instruction depends on where in the grid its first cycle falls: a
// machine averages over all of them because the two crystals drift apart. In
// the emulator 8 MHz against 640 ns is an exact ratio, and a loop of copies
// would sit at one phase for good (MOV (R0),R1 ran 28.5 clocks or 32 by
// where the program happened to start, against 33.1 on a machine). `drift`
// lengthens the grid by so many millionths, which walks the phase through
// the grid every few milliseconds, as on the machine.
//
// `late` adds clocks to the answer: the ВМ2 sees RPLY on its own edges, and a
// reply exactly on one of them is seen there (0) or on the next (1). The
// bench gives 0.
class UKNCRamTiming: public ComputerDevice, public WaitSource
{
private:
    unsigned int m_grid = 640;
    unsigned int m_phase = 0;
    unsigned int m_reply = 320;
    unsigned int m_reply_write = 320;
    unsigned int m_reply_write_byte = 320;
    unsigned int m_read = 0;
    unsigned int m_write = 0;
    unsigned int m_write_byte = 0;
    unsigned int m_rmw_write = 0;
    unsigned int m_rmw_write_byte = 0;
    unsigned int m_late = 0;
    unsigned int m_drift = 0;
    unsigned int m_setup = 0;

    unsigned int m_period = 0;      // processor clock, ps
    uint32_t m_grid_ps = 1;         // the grid with its drift, ps
    uint64_t m_pos = 0;             // clocks counted
    uint64_t m_busy = 0;            // the memory is free from this moment, ps
    uint64_t m_last = 0;            // a point of the grid not after m_busy, ps
    uint64_t m_cycles = 0;          // cycles timed, for scripts

    unsigned int answer(unsigned int flags, unsigned int offset);
    uint64_t grid_floor(uint64_t t) const;

public:
    UKNCRamTiming(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int wait_states(unsigned int address, bool write, unsigned int offset) override;
    unsigned int wait_cycle(unsigned int address, unsigned int flags, unsigned int offset) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_uknc_ram_timing(InterfaceManager *im, EmulatorConfigDevice *cd);
