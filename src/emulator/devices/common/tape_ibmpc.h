// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: IBM PC cassette encoding (the Поиск-1 BIOS uses it as is)
//
// The signal of the cassette BIOS of the IBM PC, which the Поиск-1 repeats in
// INT 15h (RWCAS.ASM, INT15_A.ASM of its sources): every bit is one period of
// a square wave from channel 2 of the timer, 1184 counts for a bit 1 and 592
// for a bit 0. With the 1.25 MHz timer of the Поиск that is 947 and 474 us.
//
//   record       leader of 2048 bits 1, a bit 0, the byte 16h, blocks, 32 bits 1
//   block        256 bytes and their CRC (polynomial 1021h, from FFFFh,
//                written inverted, high byte first)
//   byte         most significant bit first
//
// The reader calibrates on the leader, so the exact rate does not matter.
//
// A tape image is a CAS file, the format of the PCE emulator: the bytes the
// BIOS would write, with the leader as FFh bytes and the bit 0 after it as the
// byte FEh, so that the record stays byte aligned. A CAS file has no silence:
// one record follows the other.
//
// On the tape here a step of the signal is a half period of a bit 0 (296
// timer counts, about 4223 steps a second): a bit 0 is two steps, high and
// low, a bit 1 is four, two high and two low.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ibmpc_tape
{
    // Steps of silence in front of the first record: the BIOS looks for the
    // first edge before it starts timing the leader
    const unsigned LEAD_IN = 2000;

    inline void put_bit(std::vector<uint8_t> &steps, unsigned bit)
    {
        if (bit & 1) {
            steps.push_back(1); steps.push_back(1);
            steps.push_back(0); steps.push_back(0);
        } else {
            steps.push_back(1); steps.push_back(0);
        }
    }

    // Steps to bytes, most significant first, as TapeRecorder plays them. The
    // tail is padded with silence
    inline void pack(const std::vector<uint8_t> &steps, std::vector<uint8_t> &out)
    {
        out.reserve(out.size() + (steps.size() + 7) / 8);
        for (size_t i = 0; i < steps.size(); i += 8) {
            uint8_t b = 0;
            for (size_t k = 0; k < 8; k++)
                b = (uint8_t)((b << 1) | ((i + k < steps.size()) ? steps[i + k] : 0));
            out.push_back(b);
        }
    }

    // CRC of a block as the BIOS counts it: polynomial 1021h from FFFFh
    inline uint16_t crc(const uint8_t *data, size_t size)
    {
        uint16_t c = 0xFFFF;
        for (size_t i = 0; i < size; i++)
            for (int k = 7; k >= 0; k--) {
                const unsigned fb = ((c >> 15) ^ (data[i] >> k)) & 1;
                c = (uint16_t)(c << 1);
                if (fb) c ^= 0x1021;
            }
        return c;
    }

    // One record as the BIOS writes it: the leader, 16h, blocks of 256 bytes
    // with the CRC inverted, high byte first, and the trailer. The last block
    // is filled with zeros (the BIOS fills it with whatever follows the data)
    inline void put_record(const std::vector<uint8_t> &data, std::vector<uint8_t> &cas)
    {
        cas.insert(cas.end(), 256, 0xFF);
        cas.push_back(0xFE);
        cas.push_back(0x16);
        size_t pos = 0;
        do {
            uint8_t block[256] = {0};
            for (size_t i = 0; i < 256 && pos + i < data.size(); i++) block[i] = data[pos + i];
            cas.insert(cas.end(), block, block + 256);
            const uint16_t c = (uint16_t)~crc(block, 256);
            cas.push_back((uint8_t)(c >> 8));
            cas.push_back((uint8_t)c);
            pos += 256;
        } while (pos < data.size());
        cas.insert(cas.end(), 4, 0xFF);
    }

    // Is this a recording (a CAS file), or the bare contents of a file? A
    // recording starts with its leader and the byte FEh
    inline bool is_recording(const std::vector<uint8_t> &cas)
    {
        size_t i = 0;
        while (i < cas.size() && cas[i] == 0xFF) i++;
        return i >= 8 && i + 1 < cas.size() && cas[i] == 0xFE && cas[i + 1] == 0x16;
    }

    // The bare contents of a file, as digitised cassettes are often kept,
    // made into the two records the menu of the Поиск-1 BIOS loads: a header
    // (A5h, the name of 8 characters, the length at offset 0Ah) and the data.
    // The BIOS puts the data at 0060:0000 and starts it there
    inline void wrap_file(const std::string &name, const std::vector<uint8_t> &data,
                          std::vector<uint8_t> &cas)
    {
        std::vector<uint8_t> header(17, 0);
        header[0] = 0xA5;
        for (size_t i = 0; i < 8; i++) header[1 + i] = (i < name.size()) ? (uint8_t)name[i] : ' ';
        header[9] = 0x01;
        header[10] = (uint8_t)data.size();
        header[11] = (uint8_t)(data.size() >> 8);
        header[12] = 0x60;
        put_record(header, cas);
        put_record(data, cas);
    }

    // A CAS file to the signal: every byte, every bit, most significant first
    inline void encode(const std::vector<uint8_t> &cas, std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> steps(LEAD_IN, 0);
        for (size_t i = 0; i < cas.size(); i++)
            for (int k = 7; k >= 0; k--)
                put_bit(steps, (cas[i] >> k) & 1);
        // Some silence after the last record, the way a tape runs on
        steps.insert(steps.end(), 64, 0);
        pack(steps, out);
    }

    // The signal the machine writes back to a CAS file. It is fed with the time
    // between every two edges, in clocks of the machine; a bit is two half
    // periods of the same length. A record starts at the bit 0 that ends a run
    // of bits 1 and ends with a pause, the end of the recording or the leader
    // of the next record
    class Decoder
    {
    public:
        // threshold: the length that tells a half period of a bit 1 from one of
        // a bit 0; gap: a pause this long between two edges ends a record
        void reset(uint32_t threshold, uint32_t gap)
        {
            m_threshold = threshold;
            m_gap = gap;
            m_file.clear();
            m_half = -1;
            m_ones = 0;
            m_in_record = false;
            m_byte = 0;
            m_bits = 0;
        }

        void add_half(uint32_t length)
        {
            if (length >= m_gap) {
                end_record();
                m_half = -1;
                m_ones = 0;
                return;
            }
            const int h = (length >= m_threshold) ? 1 : 0;
            if (m_half < 0) { m_half = h; return; }
            if (m_half != h) { m_half = h; return; }    //out of step: the pair starts here
            m_half = -1;
            add_bit((unsigned)h);
        }

        // The recording stopped: whatever record is open is complete
        void finish() { end_record(); }

        const std::vector<uint8_t> * file() const { return &m_file; }

    private:
        // Ones that can only be the trailer of one record and the leader of
        // the next: 32 + 2048. A block of 256 bytes FFh gives 2048, plus
        // fewer than 16 at each side from the CRCs around it
        static const unsigned NEXT_LEADER = 2080;
        // A block on the tape: 256 bytes and the CRC
        static const size_t BLOCK = 258;

        uint32_t m_threshold = 1;
        uint32_t m_gap = 1;
        std::vector<uint8_t> m_file;
        int m_half = -1;
        unsigned m_ones = 0;
        bool m_in_record = false;
        uint8_t m_byte = 0;
        unsigned m_bits = 0;
        size_t m_start = 0;         // m_file index of the byte after FEh
        size_t m_rec_bits = 0;      // bits of the record so far
        size_t m_run_start = 0;     // where the current run of ones began, in bits

        void start_record()
        {
            m_file.insert(m_file.end(), 256, 0xFF);
            m_file.push_back(0xFE);
            m_in_record = true;
            m_start = m_file.size();
            m_rec_bits = 0;
            m_ones = 0;
            m_byte = 0;
            m_bits = 0;
        }

        void add_bit(unsigned bit)
        {
            if (!m_in_record) {
                if (bit) { m_ones++; return; }
                // The bit 0 after the leader; a short run of ones is noise
                if (m_ones < 32) { m_ones = 0; return; }
                start_record();
                return;
            }
            if (bit) {
                if (m_ones++ == 0) m_run_start = m_rec_bits;
            } else {
                if (m_ones >= NEXT_LEADER) {
                    // The BIOS writes the next record straight after the
                    // trailer, with no pause to end this one: the record is
                    // cut after the last block that ends where the run began,
                    // plus the 4 bytes of the trailer
                    size_t end = 1;     // the byte 16h
                    while ((end + BLOCK) * 8 <= m_run_start + 15) end += BLOCK;
                    m_file.resize(m_start + end);
                    m_file.insert(m_file.end(), 4, 0xFF);
                    start_record();
                    return;
                }
                m_ones = 0;
            }
            m_rec_bits++;
            m_byte = (uint8_t)((m_byte << 1) | (bit & 1));
            if (++m_bits == 8) {
                m_file.push_back(m_byte);
                m_byte = 0;
                m_bits = 0;
            }
        }

        // A byte cut short is the end of the trailer: it is filled with ones
        void end_record()
        {
            if (m_in_record && m_bits > 0) {
                while (m_bits < 8) { m_byte = (uint8_t)((m_byte << 1) | 1); m_bits++; }
                m_file.push_back(m_byte);
            }
            m_in_record = false;
            m_bits = 0;
            m_ones = 0;
        }
    };
}
