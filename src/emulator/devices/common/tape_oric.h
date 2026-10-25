// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Oric cassette encoding (Правец-8Д), *.tap files
//
// The fast format of the Oric ROM, the one CSAVE writes by default: a bit is
// one period of a square wave, 1 - 416 us (208 us a half), 0 - 832 us. The
// ROM writes it with timer 1 of the VIA on PB7 and reads it timing the edges
// of CB1. A byte goes as a 1, a start bit 0, the eight data bits from the
// least significant, an odd parity bit and three stop bits 1 (as Oricutron
// sends it); between bytes the line keeps playing 1s.
//
// A .tap file is the bytes of the tape as they are: a leader of $16, $24, a
// header of nine bytes, the name ending in 0, the data - one record after
// another. The files keep only a few bytes of the leader, so the leader is
// made longer here for the ROM to lock onto, and the header is followed by
// 100 ms of 1s while the ROM prints what it found.
//
// On the tape here a step of the signal is 52 us (19231 steps a second).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oric_tape
{
    const unsigned STEPS_PER_SECOND = 19231;
    const unsigned ONE_HALF = 4;            // 208 us
    const unsigned ZERO_HALF = 8;           // 416 us
    const unsigned LEADER_EXTRA = 80;       // bytes of $16 put before a header
    const unsigned PAUSE_BITS = 240;        // 100 ms of 1s after a header
    const unsigned LEAD_IN = STEPS_PER_SECOND / 2;

    inline void put_bit(std::vector<uint8_t> &steps, unsigned bit)
    {
        const unsigned half = bit ? ONE_HALF : ZERO_HALF;
        steps.insert(steps.end(), half, (uint8_t)1);
        steps.insert(steps.end(), half, (uint8_t)0);
    }

    inline void put_byte(std::vector<uint8_t> &steps, uint8_t b)
    {
        put_bit(steps, 1);
        put_bit(steps, 0);
        unsigned parity = 1;
        for (unsigned k = 0; k < 8; k++) {
            const unsigned bit = (b >> k) & 1;
            parity ^= bit;
            put_bit(steps, bit);
        }
        put_bit(steps, parity);
        put_bit(steps, 1);
        put_bit(steps, 1);
        put_bit(steps, 1);
    }

    // Where the header that starts at i (a leader of three $16 or more, $24)
    // ends: past the 0 of the name. 0 when there is no header at i
    inline size_t header_end(const std::vector<uint8_t> &f, size_t i)
    {
        size_t n = 0;
        while (i + n < f.size() && f[i + n] == 0x16) n++;
        if (n < 3 || i + n >= f.size() || f[i + n] != 0x24) return 0;
        size_t e = i + n + 1 + 9;
        while (e < f.size() && f[e] != 0) e++;
        return (e < f.size()) ? e + 1 : 0;
    }

    // A .tap file to the signal: steps of levels, packed 8 to a byte, most
    // significant first
    inline void build_steps(const std::vector<uint8_t> &file, std::vector<uint8_t> &steps)
    {
        steps.assign(LEAD_IN, 0);
        size_t pause_at = 0;
        // A header is looked for only where a record may start: past the data
        // of the one before, whose length its header gives (end and start,
        // high byte first, at 4-5 and 6-7 after the $24). Inside the data
        // $16 $16 $16 $24 is a screen with cyan paper and a "$"
        size_t next_record = 0;
        for (size_t i = 0; i < file.size(); i++) {
            if (i >= next_record && file[i] == 0x16 && (i == 0 || file[i - 1] != 0x16)) {
                const size_t e = header_end(file, i);
                if (e != 0) {
                    for (unsigned k = 0; k < LEADER_EXTRA; k++) put_byte(steps, 0x16);
                    pause_at = e;
                    size_t h = i;
                    while (file[h] == 0x16) h++;
                    h++;    // past the $24
                    const unsigned end = (file[h + 4] << 8) | file[h + 5];
                    const unsigned start = (file[h + 6] << 8) | file[h + 7];
                    next_record = e + ((end >= start) ? end - start + 1 : 0);
                }
            }
            put_byte(steps, file[i]);
            if (i + 1 == pause_at)
                for (unsigned k = 0; k < PAUSE_BITS; k++) put_bit(steps, 1);
        }
        for (unsigned k = 0; k < PAUSE_BITS; k++) put_bit(steps, 1);
        steps.insert(steps.end(), STEPS_PER_SECOND / 4, (uint8_t)0);
    }

    inline void encode(const std::vector<uint8_t> &file, std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> steps;
        build_steps(file, steps);
        out.reserve(out.size() + (steps.size() + 7) / 8);
        for (size_t i = 0; i < steps.size(); i += 8) {
            uint8_t b = 0;
            for (size_t k = 0; k < 8; k++)
                b = (uint8_t)((b << 1) | ((i + k < steps.size()) ? steps[i + k] : 0));
            out.push_back(b);
        }
    }

    // What the ROM writes, back to a .tap file: the periods of PB7 between
    // its rising edges are the bits, a 0 after the 1s starts a byte. The long
    // leader the ROM writes is cut to the few bytes a .tap keeps
    class Decoder
    {
    public:
        // clock: of the machine, Hz
        void reset(uint32_t clock)
        {
            m_split = (uint32_t)((uint64_t)clock * 624 / 1000000);     // 1 from 0
            m_gap = (uint32_t)((uint64_t)clock * 2000 / 1000000);      // no signal
            m_file.clear();
            m_bits = -1;
            m_record_start = 0;
        }

        void add_period(uint32_t length)
        {
            if (length >= m_gap) { m_bits = -1; m_record_start = m_file.size(); return; }
            const unsigned bit = (length < m_split) ? 1 : 0;
            if (m_bits < 0) {
                // Waiting for the start bit
                if (bit == 0) { m_bits = 0; m_byte = 0; }
                return;
            }
            if (m_bits < 8) {
                m_byte |= (uint8_t)(bit << m_bits);
                m_bits++;
                return;
            }
            // The parity bit closes the byte
            m_bits = -1;
            // Only a leader that opens a record, after a pause: the data may
            // hold $16 ... $24 too
            if (m_byte == 0x24 && m_record_start < m_file.size()) {
                size_t n = 0;
                while (n < m_file.size() && m_file[m_file.size() - 1 - n] == 0x16) n++;
                if (n > 4 && m_file.size() - n == m_record_start)
                    m_file.resize(m_file.size() - (n - 4));
                m_record_start = m_file.size() + 1;     // until the next pause
            }
            m_file.push_back(m_byte);
        }

        const std::vector<uint8_t> * file() const { return &m_file; }

    private:
        uint32_t m_split = 1;
        uint32_t m_gap = 1;
        std::vector<uint8_t> m_file;
        int m_bits = -1;
        uint8_t m_byte = 0;
        size_t m_record_start = 0;  // where the bytes after the last pause begin
    };
}
