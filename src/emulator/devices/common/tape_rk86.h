// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Radio-86RK tape encoding, reading side
//
// The waveform was read off the output routine of the Радио-86РК monitor
// (0F846 in the 32K ROM) rather than taken from a description, and Микроша,
// Апогей, Спектр-001 and Орион-128 write the same one.
//
// A bit occupies two half periods: the first carries the complement of the
// bit, the second the bit itself. There is therefore always an edge in the
// middle of a cell, and the level right after it is the value. An edge on the
// boundary between two cells appears only when the two bits are equal, so
// every interval between edges is either one half period or two.
//
// A file goes out as 256 zero bytes, the synchronisation byte $E6 and then the
// bytes themselves, most significant bit first. The monitor closes the record
// with two zero bytes, another $E6 and the checksum.
//
// What the decoder hands over starts at the first $E6 and runs to the end. The
// synchronisation byte is included because the formats disagree about it: a
// .rk / .rkm / .rka begins with the addresses and gets its $E6 back from the
// [TapeFiles] entry on the way to the tape, while a .rko carries its own
// preamble and $E6. Only the device knows which of the two it is writing.
//
// The half period is not quite constant: the two halves of a cell differ by a
// few per cent because the loop that emits the second one carries the byte
// bookkeeping, and the last half period of every byte is shortened on purpose
// to pay for the call overhead. Two halves still stay well below one and a
// half of one, which is all the classification needs.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rk86_tape
{
    // The synchronisation byte that separates the preamble from the data. The
    // reader in the monitor takes its complement as well: it locks onto edges
    // without knowing the phase and can read the whole tape inverted. The
    // decoder here can end up in the same place, so it is just as tolerant.
    const uint8_t SYNC = 0xE6;

    // Half periods of the preamble to require before decoding starts. The
    // preamble is 256 zero bytes, that is four thousand of them.
    const size_t LEADER = 256;

    // Intervals to average the half period over, taken from the front of the
    // recording where the preamble is
    const size_t SAMPLE = 1024;

    class Decoder
    {
    private:
        // The level held between two edges and how long it was held. Levels
        // alternate, but keeping the value is cheaper than tracking the phase.
        struct Run
        {
            uint32_t cycles;
            uint8_t level;
        };

        std::vector<Run> runs;
        std::vector<uint8_t> result;
        size_t decoded_runs = 0;

        void decode()
        {
            result.clear();
            decoded_runs = runs.size();

            if (runs.size() < LEADER * 2) return;

            // Every interval is one half period or two, and the preamble at
            // the front of the tape is nothing but the short ones, so the
            // middle of a sorted slice taken there is the half period. A few
            // stray edges from the keyboard before the recording started do
            // not move it.
            const size_t sample = (runs.size() < SAMPLE)? runs.size() : SAMPLE;
            std::vector<uint32_t> head;
            head.reserve(sample);
            for (size_t i = 0; i < sample; i++) head.push_back(runs[i].cycles);
            std::sort(head.begin(), head.end());
            const uint32_t unit = head[sample / 2];
            if (unit == 0) return;
            const uint32_t threshold = unit + unit / 2;

            // All the bits of the preamble are equal, so all of its intervals
            // are short. Nothing in front of it on the tape is that regular,
            // and taking the first such stretch rather than the longest one
            // keeps a file made of equal bytes from being mistaken for it.
            size_t start = 0, length = 0;
            bool found = false;
            for (size_t i = 0; i < runs.size(); i++) {
                if (runs[i].cycles > threshold) { length = 0; continue; }
                if (length++ == 0) start = i;
                if (length >= LEADER) { found = true; break; }
            }
            if (!found) return;

            // A long interval always ends on the edge in the middle of a cell,
            // and that is what puts the decoder in phase. Inside the preamble
            // every interval is short and the phase is arbitrary; the first
            // bit of the synchronisation byte differs from the preamble and
            // makes the first long one.
            std::vector<uint8_t> bits;
            bits.reserve(runs.size());
            bool at_middle = false;
            for (size_t i = start + 1; i < runs.size(); i++) {
                if (runs[i - 1].cycles > threshold) {
                    bits.push_back(runs[i].level);
                    at_middle = true;
                } else if (at_middle) {
                    at_middle = false;              // an edge on a cell boundary
                } else {
                    bits.push_back(runs[i].level);
                    at_middle = true;
                }
            }

            // What comes out starts at the synchronisation byte itself, not
            // after it: a .rk keeps it out of the file and gets one back from
            // the [TapeFiles] entry on the way to the tape, a .rko carries it,
            // and only the device knows which of the two it is writing.
            uint8_t window = 0;
            size_t sync_start = 0;
            bool synced = false, inverted = false;
            for (size_t i = 7; i < bits.size(); i++) {
                window = 0;
                for (int k = 0; k < 8; k++)
                    window = (uint8_t)((window << 1) | (bits[i - 7 + k] & 1));
                if (window == SYNC || window == (uint8_t)(~SYNC)) {
                    sync_start = i - 7;
                    inverted = window != SYNC;
                    synced = true;
                    break;
                }
            }
            if (!synced) return;

            // Bits go out most significant first; a byte left unfinished by
            // switching the recording off is dropped.
            const size_t count = (bits.size() - sync_start) / 8;
            result.reserve(count);
            for (size_t i = 0; i < count; i++) {
                uint8_t b = 0;
                for (int k = 0; k < 8; k++)
                    b = (uint8_t)((b << 1) | (bits[sync_start + i * 8 + k] & 1));
                result.push_back(inverted? (uint8_t)(~b) : b);
            }
        }

    public:
        void reset()
        {
            runs.clear();
            result.clear();
            decoded_runs = 0;
        }

        // The level that has just ended and how many processor cycles it held
        void add_run(uint8_t level, uint32_t cycles)
        {
            Run r;
            r.cycles = cycles;
            r.level = (uint8_t)(level & 1);
            runs.push_back(r);
        }

        const std::vector<uint8_t> * file()
        {
            if (decoded_runs != runs.size()) decode();
            return &result;
        }
    };
}
