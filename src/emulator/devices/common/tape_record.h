// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: One record on a tape, shared by every medium
//
// A tape is a run of records with pauses between them. That is true of a
// cassette a machine drives like a disk (the Юниор writes sectors on it) and
// just as true of one it only listens to: there a file is a record and the
// silence before it is the gap. Holding both in one structure is what lets the
// transport - winding, seeking, the blank tail, "what is under the head" - be
// written once instead of once per format.
//
// A UNIT is the tape's own quantum of time, and ticks_per_bit converts it to
// cycles of the machine. What a unit is depends on the medium:
//
//   Levels   one sample of the line          (rk86, msx, bk, uknc)
//   Bytes    one bit interval, eight to a byte (unior)
//   Pulses   one cycle, ticks_per_bit = 1    (zx)
//
// so position in seconds is units * ticks_per_bit / clock everywhere.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TapeRecord
{
    uint64_t gap   = 0;                 // Pause before the record, in units
    uint64_t units = 0;                 // Length of the record itself, in units

    // The record itself. Bytes as the machine reads them, packed line levels
    // (eight to a byte), or the source block a waveform is generated from -
    // whichever the medium says
    std::vector<uint8_t> data;

    // The same gap and duration in the units of a .bt file. Kept so that an
    // image the machine never touched goes back to disk byte for byte
    uint32_t file_gap = 0;
    uint32_t file_dur = 0;
    bool     from_file = false;
};
