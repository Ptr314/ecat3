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

    // A tape header is the load address, the length and a sixteen byte name
    const unsigned NAME_SIZE   = 16;
    const unsigned HEADER_SIZE = 4 + NAME_SIZE;

    // БЕЙСИК does not write a program saved in text form as one file. It cuts
    // the text into parts of 256 bytes, writes every part as a separate tape
    // file named "NAME  .ASC #NNN" and closes the series with a file of the
    // same name without a number holding two zero bytes. LOAD reads part #000
    // and then looks for the next one, so a single file never finishes loading.
    const unsigned ASCII_PART_SIZE = 256;
    const uint8_t  ASCII_EOT       = 0x1A;      // ends the text inside the last part

    // The address БЕЙСИК puts into the header of a text part is the address of
    // its own buffer. The reader ignores it and loads the text into the buffer
    // it has itself, so the value only has to look like the real one.
    const uint16_t ASCII_ADDRESS = 0x3DEE;

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

    // One complete file on the tape: the leading series the reader calibrates
    // on, then the header record and the data record. Every record opens with
    // a short sync series and a marker, the way the monitor emits them at
    // 0116474. The name is a raw sixteen byte field, already padded by the
    // caller.
    inline void put_file(std::vector<uint8_t> &bits, uint16_t address,
                         const uint8_t * data, unsigned length,
                         const uint8_t * name)
    {
        put_sync(bits, LEADER_PULSES);
        put_marker(bits);

        put_sync(bits, RECORD_PULSES);
        put_marker(bits);
        put_byte(bits, address & 0xFF);
        put_byte(bits, (address >> 8) & 0xFF);
        put_byte(bits, length & 0xFF);
        put_byte(bits, (length >> 8) & 0xFF);
        for (unsigned i = 0; i < NAME_SIZE; i++) put_byte(bits, name[i]);

        put_sync(bits, RECORD_PULSES);
        put_marker(bits);
        for (unsigned i = 0; i < length; i++) put_byte(bits, data[i]);
        const uint16_t cs = checksum(data, length);
        put_byte(bits, cs & 0xFF);
        put_byte(bits, (cs >> 8) & 0xFF);

        put_sync(bits, 32);
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

        uint8_t name_field[NAME_SIZE];
        for (unsigned i = 0; i < NAME_SIZE; i++)
            name_field[i] = (i < name.size())? (uint8_t)name[i] : (uint8_t)' ';

        put_file(bits, address, file.data() + data_at, length, name_field);

        pack(bits, out);
    }

    // The name field of a text part: six characters of the name, the .ASC
    // extension and the number of the part. The byte 032 after the number and
    // the four zeroes in the closing file are what БЕЙСИК leaves there, and
    // the reader compares the whole field, so they are reproduced as measured.
    inline void ascii_name(uint8_t * field, const std::string &name, int part)
    {
        for (unsigned i = 0; i < 6; i++)
            field[i] = (i < name.size())? (uint8_t)name[i] : (uint8_t)' ';
        field[6] = '.'; field[7] = 'A'; field[8] = 'S'; field[9] = 'C';
        field[10] = ' ';
        if (part < 0) {
            field[11] = ' ';
            field[12] = field[13] = field[14] = field[15] = 0;
        } else {
            field[11] = '#';
            field[12] = (uint8_t)('0' + (part / 100) % 10);
            field[13] = (uint8_t)('0' + (part / 10) % 10);
            field[14] = (uint8_t)('0' + part % 10);
            field[15] = ASCII_EOT;
        }
    }

    // A БЕЙСИК program saved in text form. The text is cut into parts of 256
    // bytes, each written as a file of its own, and a closing file without a
    // number tells the reader that the program is over.
    inline void encode_ascii(const std::vector<uint8_t> &text, const std::string &name,
                             std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> bits;
        uint8_t name_field[NAME_SIZE];

        // The text always ends with 032, and the last part is padded with
        // zeroes up to the full size
        std::vector<uint8_t> body = text;
        while (!body.empty() && (body.back() == 0 || body.back() == ASCII_EOT)) body.pop_back();
        body.push_back(ASCII_EOT);
        const unsigned parts = (unsigned)((body.size() + ASCII_PART_SIZE - 1) / ASCII_PART_SIZE);
        body.resize((size_t)parts * ASCII_PART_SIZE, 0);

        for (unsigned p = 0; p < parts; p++) {
            ascii_name(name_field, name, (int)p);
            put_file(bits, ASCII_ADDRESS, body.data() + (size_t)p * ASCII_PART_SIZE,
                     ASCII_PART_SIZE, name_field);
        }

        const uint8_t closing[2] = {0, 0};
        ascii_name(name_field, name, -1);
        put_file(bits, ASCII_ADDRESS, closing, 2, name_field);

        pack(bits, out);
    }

    // Recovers a file from what the machine wrote to the tape. Periods are
    // measured between rising edges, the same way the monitor does it, so a
    // synchronisation pulse is one period, the marker four of them, a set bit
    // two and a clear bit one. Every bit is followed by a separator period.
    class Decoder
    {
    private:
        struct TapeFile
        {
            uint16_t address;
            std::string name;                   // the raw sixteen byte field
            std::vector<uint8_t> data;
        };

        std::vector<uint32_t> periods;
        std::vector<uint8_t> result;
        std::string m_name;
        size_t decoded_periods = 0;

        // Turns a tape name into something that can be offered as a file name
        static std::string clean_name(const std::string &name)
        {
            std::string s;
            for (size_t i = 0; i < name.size(); i++) {
                const char c = name[i];
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
                    || (c >= 'a' && c <= 'z') || c == '-' || c == '_')
                    s += c;
            }
            return s.empty()? std::string("tape") : s;
        }

        // Splits the stream into the records between the markers
        void decode()
        {
            result.clear();
            m_name.clear();
            decoded_periods = periods.size();

            // Recording begins when the user presses the button, so the stream
            // normally opens with the clicks of the keys that type the command:
            // on the БК the same bit drives the speaker and the tape. The
            // leading series is a run of thousands of identical periods and
            // nothing else in the recording comes close, so the first long
            // enough run gives both the length of one period and the point
            // where the useful signal starts.
            const size_t LEADER = 1024;         // certainly the leading series
            const size_t SHORTEST = 256;        // enough to average a period on
            if (periods.size() < SHORTEST * 2) return;

            size_t start = 0, length = 0;
            size_t p = 0;
            while (p < periods.size()) {
                uint32_t lo = periods[p], hi = periods[p];
                size_t q = p + 1;
                for (; q < periods.size(); q++) {
                    const uint32_t v = periods[q];
                    const uint32_t new_lo = (v < lo)? v : lo;
                    const uint32_t new_hi = (v > hi)? v : hi;
                    if (new_lo == 0 || new_hi > new_lo + new_lo / 8) break;
                    lo = new_lo; hi = new_hi;
                }
                if (q - p > length) { length = q - p; start = p; }
                if (length >= LEADER) break;
                p = q;
            }
            if (length < SHORTEST) return;

            std::vector<uint32_t> head(periods.begin() + start,
                                       periods.begin() + start + SHORTEST);
            std::sort(head.begin(), head.end());
            const uint32_t unit = head[head.size() / 2];
            if (unit == 0) return;

            const uint32_t bit_threshold = unit * 3 / 2;
            const uint32_t marker_threshold = unit * 5 / 2;

            std::vector<std::vector<uint8_t>> records;
            size_t i = start;

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

            // A file is a header record of the full size followed by a data
            // record as long as the header says plus the two checksum bytes.
            // The leading series of every file after the first one decodes into
            // records of zeroes, and this check is what throws them away.
            std::vector<TapeFile> files;
            for (size_t r = 0; r + 1 < records.size(); r++) {
                const std::vector<uint8_t> & h = records[r];
                const std::vector<uint8_t> & d = records[r + 1];
                if (h.size() != HEADER_SIZE) continue;
                const unsigned length = h[2] | (h[3] << 8);
                // A record can carry a couple of bytes more than the header
                // says: the series that closes the file decodes into bits too
                if (length == 0 || d.size() < (size_t)length + 2) continue;
                if (checksum(d.data(), length) != (uint16_t)(d[length] | (d[length + 1] << 8))) continue;

                TapeFile f;
                f.address = (uint16_t)(h[0] | (h[1] << 8));
                f.name.assign(h.begin() + 4, h.end());
                f.data.assign(d.begin(), d.begin() + length);
                files.push_back(f);
                r++;
            }

            if (files.empty()) {
                // A recording switched off too early leaves a data record
                // shorter than the header promises. Nothing checks out then,
                // but the first record is still a header and what was read is
                // worth handing over.
                if (records.size() >= 2 && records[0].size() >= 4) {
                    const std::vector<uint8_t> & h = records[0];
                    const std::vector<uint8_t> & d = records[1];
                    unsigned length = h[2] | (h[3] << 8);
                    if (length > d.size()) length = (unsigned)d.size();
                    result.push_back(h[0]); result.push_back(h[1]);
                    result.push_back((uint8_t)(length & 0xFF));
                    result.push_back((uint8_t)((length >> 8) & 0xFF));
                    result.insert(result.end(), d.begin(), d.begin() + length);
                    if (h.size() == HEADER_SIZE)
                        m_name = clean_name(std::string(h.begin() + 4, h.end())) + ".bin";
                } else
                    for (size_t r = 0; r < records.size(); r++)
                        result.insert(result.end(), records[r].begin(), records[r].end());
                return;
            }

            // A program saved in text form arrives as a series of numbered
            // parts. They are glued back together and the closing file, which
            // has no number, is dropped.
            if (files[0].name.find(".ASC") != std::string::npos) {
                unsigned parts = 0;
                for (size_t f = 0; f < files.size(); f++) {
                    if (files[f].name.find('#') == std::string::npos) continue;
                    result.insert(result.end(), files[f].data.begin(), files[f].data.end());
                    parts++;
                }
                if (parts != 0) {
                    for (size_t i = 0; i < result.size(); i++)
                        if (result[i] == ASCII_EOT) { result.resize(i); break; }
                    m_name = clean_name(files[0].name.substr(0, 6)) + ".asc";
                    return;
                }
                result.clear();
            }

            // A single file keeps the address and the length in front of the
            // data, the way a БК file looks on disk
            const TapeFile & f = files[0];
            result.push_back((uint8_t)(f.address & 0xFF));
            result.push_back((uint8_t)((f.address >> 8) & 0xFF));
            result.push_back((uint8_t)(f.data.size() & 0xFF));
            result.push_back((uint8_t)((f.data.size() >> 8) & 0xFF));
            result.insert(result.end(), f.data.begin(), f.data.end());
            m_name = clean_name(f.name) + ".bin";
        }

    public:
        void reset()
        {
            periods.clear();
            result.clear();
            m_name.clear();
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

        // The name the machine wrote into the header, with the extension the
        // decoded content has to keep to be readable back
        const std::string & name()
        {
            if (decoded_periods != periods.size()) decode();
            return m_name;
        }
    };
}
