// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Юниор / Арго tape container (.bt)
//
// This cassette is not a soundtrack but a store: TCP/M treats it the way it
// treats a floppy. The machine drives the transport itself and the data go
// through a КР580ВВ51 in synchronous mode, so what crosses the head is bytes,
// not half periods - the modulation is in hardware and none of the encoders
// next door apply. A tape is therefore a run of records with pauses between
// them, which is exactly what TapeRecord holds.
//
// The layout (read off А. Морозов's images and checked against the ROM):
//
//   [dword]                                  file header
//   [start:4][duration:4][length:4][AA AA 19 00][data]
//   ...
//
// length counts the record INCLUDING the four preamble bytes - $0021 for a
// 33 byte record, $080A for a 2058 byte one. The preamble is what the ROM
// sends before every record (FB50); the data begin with the sync character
// $E6, which the ВВ51 catches in hunt mode and does not put in the buffer.
//
// The first two dwords are NOT a pause and a length but a TIME: when the
// record starts and how long it lasts, in units of which a byte takes 3.3-3.6
// (milliseconds, near enough, at about 2400 baud). The ratio of duration to
// length is the same for every record of every image we have, Юниор's and
// Арго's alike, which is how it was found. A real gap is the start of a record
// minus the end of the one before it, and it comes out quite different: about
// 1100 ms before a marker and about 600 before a data block, not 111 and 6905
// as they would read if the second dword were the gap.
//
// The difference is not cosmetic. After every rejected record the Арго ROM
// restarts the transport and waits 20 ВГ75 frames, about 400 ms, before it
// turns playback on. With a 111 ms gap the marker passes the head entirely
// inside that wait, and the volume driver finds no block at all beyond the
// zeroth one it reaches from the start of the tape. With the true 1100 ms it
// has time.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "emulator/devices/common/tape_record.h"

namespace bt_tape
{
    //What the ROM sends before every record
    const uint8_t PREAMBLE[4] = {0xAA, 0xAA, 0x19, 0x00};

    inline uint32_t rd32(const uint8_t * p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
             | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    inline void put32(std::vector<uint8_t> &out, uint32_t v)
    {
        out.push_back((uint8_t)(v & 0xFF));
        out.push_back((uint8_t)((v >> 8) & 0xFF));
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        out.push_back((uint8_t)((v >> 24) & 0xFF));
    }

    //err is one of "short", "preamble" or "empty" - the device turns it into a
    //message of its own, because that is where the translations live
    inline bool parse(const uint8_t * raw, size_t size, unsigned int baud,
                      std::vector<TapeRecord> &out, double &unit_per_bit,
                      bool &blank, std::string &err)
    {
        if (size < 4) { err = "short"; return false; }

        std::vector<TapeRecord> recs;
        uint64_t sum_units = 0, sum_bits = 0, prev_end = 0;
        size_t off = 0;
        while (off + 12 <= size)
        {
            const uint32_t start = rd32(raw + off);
            const uint32_t dur   = rd32(raw + off + 4);
            const uint32_t len   = rd32(raw + off + 8);
            const size_t sync = off + 12;
            //The length comes from the file: sync + len would wrap on a 32-bit
            //size_t, and sync <= size is already known from the loop condition
            if (len < 4 || len > size - sync) break;
            if (memcmp(&raw[sync], PREAMBLE, 4) != 0) { err = "preamble"; return false; }

            TapeRecord r;
            r.file_gap = (uint32_t)((start > prev_end)?(start - prev_end):0);
            r.file_dur = dur;
            r.from_file = true;
            r.data.assign(raw + sync, raw + sync + len);
            r.units = (uint64_t)r.data.size() * 8;
            recs.push_back(r);

            prev_end = (uint64_t)start + dur;
            sum_units += dur;
            sum_bits += (uint64_t)len * 8;
            off = sync + len;
        }

        //The file's unit of time is its own, and it need not be named here:
        //only the ratio matters, and the records give it themselves - their
        //duration against their length
        unit_per_bit = (sum_bits > 0 && sum_units > 0)
                        ?((double)sum_units / (double)sum_bits)
                        :(1000.0 / (double)((baud > 0)?baud:2400));
        for (size_t i = 0; i < recs.size(); i++)
            recs[i].gap = (uint32_t)((double)recs[i].file_gap / unit_per_bit + 0.5);

        //A file of nothing but a header is a blank cassette, and a blank
        //cassette is laid out from scratch. Anything longer that yielded no
        //records is a broken image
        if (recs.empty() && size > 8) { err = "empty"; return false; }

        blank = recs.empty();
        out.swap(recs);
        return true;
    }

    inline void build(const std::vector<TapeRecord> &records, double unit_per_bit,
                      unsigned int baud, std::vector<uint8_t> &out)
    {
        out.clear();
        if (records.empty()) return;

        size_t total = 0;
        for (size_t i = 0; i < records.size(); i++) total += 12 + records[i].data.size() + 4;
        out.reserve(total);

        const double upb = (unit_per_bit > 0.0)
                            ?unit_per_bit
                            :(1000.0 / (double)((baud > 0)?baud:2400));
        uint64_t pos = 0;
        for (size_t i = 0; i < records.size(); i++)
        {
            const TapeRecord &r = records[i];
            //A record the machine wrote starts with the same preamble as one
            //from a file, but it cannot be relied on: without it the image
            //will not load back
            const bool has_preamble = r.data.size() >= 4 && memcmp(r.data.data(), PREAMBLE, 4) == 0;
            const uint32_t len = (uint32_t)(r.data.size() + (has_preamble?0:4));
            //A record from a file gives back its own numbers, and the image
            //goes out byte for byte; for a record the machine wrote they are
            //computed from the gap and the length
            const uint32_t gap_units = r.from_file?r.file_gap:(uint32_t)((double)r.gap * upb + 0.5);
            const uint32_t dur_units = r.from_file?r.file_dur:(uint32_t)((double)len * 8.0 * upb + 0.5);
            pos += gap_units;
            put32(out, (uint32_t)pos);
            put32(out, dur_units);
            put32(out, len);
            pos += dur_units;
            if (!has_preamble) out.insert(out.end(), PREAMBLE, PREAMBLE + 4);
            out.insert(out.end(), r.data.begin(), r.data.end());
        }
    }
}
