// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК tape encoding
//
// The waveform below was measured on the output routine of the БК monitor
// (0116404 and 0116506) rather than taken from a description. One unit is the
// half period of a short data pulse: 317 processor cycles at 3 MHz on the
// БК0010, 473 at 4 MHz on the БК0011М. The leading series is emitted by a
// tighter loop and comes out a few percent shorter, which does not matter
// because the reader calibrates itself on whatever it receives.
//
//   sync pulse   1 unit high, 1 unit low
//   marker       4 units low, 4 units high
//   data bit 0   short pulse (1 unit high, 1 unit low) plus a 1 unit separator
//   data bit 1   long pulse (2 units low, 2 units high) plus a 1 unit separator
//
// Bits of a byte go out least significant first. The reader in the monitor
// measures whole periods and calibrates itself on the leading sync series, so
// the absolute rate is not critical as long as the long pulse stays clearly
// above one and a half times the short one.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace bk_tape
{
    // The reader needs a long stretch of identical pulses before it accepts
    // the signal: it waits for 2048 stable ones and then averages 128 periods.
    const unsigned LEADER_PULSES = 4096;

    // A short series in front of every record
    const unsigned RECORD_PULSES = 8;

    inline void put(std::vector<uint8_t> &bits, uint8_t level, unsigned count)
    {
        for (unsigned i = 0; i < count; i++) bits.push_back(level);
    }

    inline void put_sync(std::vector<uint8_t> &bits, unsigned pulses)
    {
        for (unsigned i = 0; i < pulses; i++) { put(bits, 1, 1); put(bits, 0, 1); }
    }

    // The marker is a pulse four times longer than a sync one, followed by the
    // pattern of a single set bit, exactly as the monitor emits it at 0116432.
    inline void put_marker(std::vector<uint8_t> &bits)
    {
        put(bits, 1, 4);
        put(bits, 0, 4);
        put(bits, 1, 2); put(bits, 0, 2);
        put(bits, 1, 1); put(bits, 0, 1);
    }

    // Every bit is two pulses: the one carrying the value and a short
    // separator. The reader skips one period and measures the next, so it sees
    // exactly one period per bit.
    inline void put_byte(std::vector<uint8_t> &bits, uint8_t b)
    {
        for (int i = 0; i < 8; i++) {
            if ((b >> i) & 1) {
                put(bits, 1, 2); put(bits, 0, 2);        // long pulse
            } else {
                put(bits, 1, 1); put(bits, 0, 1);        // short pulse
            }
            put(bits, 1, 1); put(bits, 0, 1);            // separator
        }
    }

    // Sum of the bytes with the carry folded back in, the way the monitor
    // computes it at 0116622.
    inline uint16_t checksum(const uint8_t * data, size_t size)
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < size; i++) {
            sum += data[i];
            if (sum > 0xFFFF) sum = (sum & 0xFFFF) + 1;
        }
        return (uint16_t)sum;
    }

    // Packs a stream of one bit per byte into the bit buffer the tape device
    // plays back, most significant bit of every byte first.
    inline void pack(const std::vector<uint8_t> &bits, std::vector<uint8_t> &out)
    {
        size_t n = bits.size();
        out.reserve(out.size() + (n + 7) / 8);
        for (size_t i = 0; i < n; i += 8) {
            uint8_t b = 0;
            for (int k = 0; k < 8; k++) {
                b <<= 1;
                if (i + k < n) b |= bits[i + k] & 1;
            }
            out.push_back(b);
        }
    }

    // A БК file starts with the load address and the length, which are also
    // the first four bytes of the tape header. The rest of the header is the
    // sixteen character name.
    inline void encode(const std::vector<uint8_t> &file, const std::string &name,
                       std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> bits;

        uint16_t address = 0, length = 0;
        size_t data_at = 0;
        if (file.size() >= 4) {
            address = (uint16_t)(file[0] | (file[1] << 8));
            length  = (uint16_t)(file[2] | (file[3] << 8));
            data_at = 4;
            if (length == 0 || length > file.size() - 4) length = (uint16_t)(file.size() - 4);
        }

        // The leader the reader calibrates on, closed by a marker of its own
        put_sync(bits, LEADER_PULSES);
        put_marker(bits);

        // Header record. Every record starts with a short sync series and a
        // marker, the way the monitor emits them at 0116474.
        put_sync(bits, RECORD_PULSES);
        put_marker(bits);
        put_byte(bits, address & 0xFF);
        put_byte(bits, (address >> 8) & 0xFF);
        put_byte(bits, length & 0xFF);
        put_byte(bits, (length >> 8) & 0xFF);
        for (int i = 0; i < 16; i++)
            put_byte(bits, (i < (int)name.size())? (uint8_t)name[i] : (uint8_t)' ');

        // Data record with the checksum appended
        put_sync(bits, RECORD_PULSES);
        put_marker(bits);
        for (unsigned i = 0; i < length; i++) put_byte(bits, file[data_at + i]);
        uint16_t cs = checksum(file.data() + data_at, length);
        put_byte(bits, cs & 0xFF);
        put_byte(bits, (cs >> 8) & 0xFF);

        put_sync(bits, 32);

        pack(bits, out);
    }

    // Recovers a file from what the machine wrote to the tape. Periods are
    // measured between rising edges, the same way the monitor does it, so a
    // synchronisation pulse is one period, the marker four of them, a set bit
    // two and a clear bit one. Every bit is followed by a separator period.
    class Decoder
    {
    private:
        std::vector<uint32_t> periods;
        std::vector<uint8_t> result;
        size_t decoded_periods = 0;

        // Splits the stream into the records between the markers
        void decode()
        {
            result.clear();
            decoded_periods = periods.size();
            if (periods.size() < 256) return;

            // The leader is a long run of identical pulses, so the median of
            // the first of them is the length of one period.
            std::vector<uint32_t> head(periods.begin(), periods.begin() + 128);
            std::sort(head.begin(), head.end());
            uint32_t unit = head[head.size() / 2];
            if (unit == 0) return;

            const uint32_t bit_threshold = unit * 3 / 2;
            const uint32_t marker_threshold = unit * 5 / 2;

            std::vector<std::vector<uint8_t>> records;
            size_t i = 0;

            while (i < periods.size()) {
                // Look for a marker
                while (i < periods.size() && periods[i] <= marker_threshold) i++;
                if (i >= periods.size()) break;
                i++;                            // the marker itself
                i += 2;                         // the set bit that closes it

                std::vector<uint8_t> bytes;
                uint8_t current = 0;
                unsigned bit_count = 0;

                while (i + 1 < periods.size()) {
                    if (periods[i] > marker_threshold) break;   // the next record
                    if (periods[i] > bit_threshold) current |= (uint8_t)(1 << bit_count);
                    i += 2;
                    if (++bit_count == 8) {
                        bytes.push_back(current);
                        current = 0;
                        bit_count = 0;
                    }
                }
                if (!bytes.empty()) records.push_back(bytes);
            }

            if (records.empty()) return;

            // The first record is the header, the second the data followed by
            // a checksum. Anything else is handed over as it was read.
            if (records.size() >= 2 && records[0].size() >= 4) {
                const std::vector<uint8_t> & header = records[0];
                const std::vector<uint8_t> & data = records[1];
                unsigned length = header[2] | (header[3] << 8);
                if (length > data.size()) length = (unsigned)data.size();

                result.push_back(header[0]); result.push_back(header[1]);
                result.push_back((uint8_t)(length & 0xFF));
                result.push_back((uint8_t)((length >> 8) & 0xFF));
                result.insert(result.end(), data.begin(), data.begin() + length);
            } else {
                for (size_t r = 0; r < records.size(); r++)
                    result.insert(result.end(), records[r].begin(), records[r].end());
            }
        }

    public:
        void reset()
        {
            periods.clear();
            result.clear();
            decoded_periods = 0;
        }

        void add_period(uint32_t cycles)
        {
            periods.push_back(cycles);
        }

        const std::vector<uint8_t> * file()
        {
            if (decoded_periods != periods.size()) decode();
            return &result;
        }
    };
}
