// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ tape encoding
//
// The waveform was read off the tape driver in the peripheral processor's ROM
// (126236 and below), not taken from a description. The output bit is flipped
// from the timer interrupt, so the signal is a series of half periods of two
// lengths: 0320 timer steps of 2 us (416 us) and 0150 (208 us) at the normal
// density, half of that at the double one.
//
//   bit 0        one long period  - two long half periods
//   bit 1        two short periods - four short half periods
//   byte         bit 0 (start), eight bits least significant first, two bits 1
//   word         low byte, then high byte
//   pilot tone   a run of bits 1
//
// Both kinds of bit last the same 832 us, so the rate is 1200 baud. The reader
// calibrates on the pilot tone, measures only the first half period of every
// bit and skips the rest, so the absolute rate is not critical.
//
// A file with a header (function 020 of the driver, 126714) goes out as
//
//   pilot tone of 8000 bits
//   name block, 8 words: whatever the program put there
//   length in words and load address - only when the length is not zero
//   pilot tone of 2000 bits
//   the data words
//   checksum: the sum of the data words with end-around carry
//   one more bit 1
//
// and a block without a header (function 021, 126662) as the second half of
// that only: pilot tone of 2000 bits, 128 data words, checksum, bit 1. The
// cassette БЕЙСИК writes a program as a file of its first 128 words followed
// by blocks with the rest, and reads the blocks back with function 016.
//
// A tape image here is the sequence of such files without the signal and
// without the checksum: 16 bytes of the name block, the length, the address
// and the data. A file of zero length keeps the two zero words, so every file
// in an image starts with the same twenty bytes. A block has the same twenty
// bytes with a zero name, bit 15 of the length set (no file is 32K words
// long: that is the whole address space) and a zero address.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uknc_tape
{
    const unsigned NAME_SIZE   = 16;
    const unsigned HEADER_SIZE = NAME_SIZE + 4;

    // Pilot tones as the ROM writes them. The reader wants 8160 steady half
    // periods in front of a header and 1000 in front of data
    const unsigned HEADER_PILOT = 8000;
    const unsigned DATA_PILOT   = 2000;

    // A run of bits 1 this long between two bytes starts a new record: inside
    // a record the bytes follow each other with only their two stop bits
    const unsigned RECORD_GAP = 256;

    // A record behind a pilot tone this long is a header, behind a shorter
    // one data: the two tones are 8000 and 2000 bits
    const unsigned HEADER_GAP = 5000;

    // The length of a block in an image: bit 15 and the number of words
    const uint16_t BLOCK_FLAG = 0x8000;

    // One bit is four half periods of the short length, so a bit stream at
    // four times the baud rate carries the waveform exactly
    inline void put_bit(std::vector<uint8_t> &bits, unsigned bit)
    {
        static const uint8_t ONE[4]  = {1, 0, 1, 0};
        static const uint8_t ZERO[4] = {1, 1, 0, 0};
        const uint8_t * p = (bit & 1)? ONE : ZERO;
        bits.insert(bits.end(), p, p + 4);
    }

    inline void put_pilot(std::vector<uint8_t> &bits, unsigned count)
    {
        for (unsigned i = 0; i < count; i++) put_bit(bits, 1);
    }

    inline void put_byte(std::vector<uint8_t> &bits, uint8_t b)
    {
        put_bit(bits, 0);
        for (int i = 0; i < 8; i++) put_bit(bits, (b >> i) & 1);
        put_bit(bits, 1);
        put_bit(bits, 1);
    }

    inline void put_word(std::vector<uint8_t> &bits, uint16_t w)
    {
        put_byte(bits, (uint8_t)(w & 0xFF));
        put_byte(bits, (uint8_t)(w >> 8));
    }

    inline uint16_t get_word(const std::vector<uint8_t> &data, size_t pos)
    {
        const uint16_t lo = (pos < data.size())? data[pos] : 0;
        const uint16_t hi = (pos + 1 < data.size())? data[pos + 1] : 0;
        return (uint16_t)(lo | (hi << 8));
    }

    // The sum the ROM keeps while writing (127014): ADD, then ADC
    inline uint16_t add_checksum(uint16_t sum, uint16_t w)
    {
        uint32_t s = (uint32_t)sum + w;
        s += s >> 16;
        return (uint16_t)s;
    }

    // Bits to bytes, most significant first, as TapeRecorder plays them
    inline void pack(const std::vector<uint8_t> &bits, std::vector<uint8_t> &out)
    {
        out.reserve(out.size() + (bits.size() + 7) / 8);
        for (size_t i = 0; i < bits.size(); i += 8) {
            uint8_t b = 0;
            for (size_t k = 0; k < 8; k++) {
                // The tail is padded with the short half periods of a bit 1
                const uint8_t v = (i + k < bits.size())? bits[i + k] : (uint8_t)((k & 1) == 0);
                b = (uint8_t)((b << 1) | v);
            }
            out.push_back(b);
        }
    }

    // A tape image to the signal. Every file of the image goes out as the ROM
    // would write it; a short tail is padded with zeros
    inline void encode(const std::vector<uint8_t> &image, std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> bits;
        size_t pos = 0;
        while (pos < image.size()) {
            const uint16_t flagged = get_word(image, pos + NAME_SIZE);
            if (flagged & BLOCK_FLAG) {
                const unsigned length = flagged & ~BLOCK_FLAG;
                pos += HEADER_SIZE;
                put_pilot(bits, DATA_PILOT);
                uint16_t sum = 0;
                for (unsigned i = 0; i < length; i++) {
                    const uint16_t w = get_word(image, pos);
                    pos += 2;
                    put_word(bits, w);
                    sum = add_checksum(sum, w);
                }
                put_word(bits, sum);
                put_bit(bits, 1);
                continue;
            }
            put_pilot(bits, HEADER_PILOT);
            for (unsigned i = 0; i < NAME_SIZE; i++)
                put_byte(bits, (pos + i < image.size())? image[pos + i] : 0);
            const uint16_t length  = get_word(image, pos + NAME_SIZE);
            const uint16_t address = get_word(image, pos + NAME_SIZE + 2);
            pos += HEADER_SIZE;
            if (length != 0) {
                put_word(bits, length);
                put_word(bits, address);
                put_pilot(bits, DATA_PILOT);
                uint16_t sum = 0;
                for (unsigned i = 0; i < length; i++) {
                    const uint16_t w = get_word(image, pos);
                    pos += 2;
                    put_word(bits, w);
                    sum = add_checksum(sum, w);
                }
                put_word(bits, sum);
            }
            put_bit(bits, 1);
        }
        // Some tone after the last file, the way a tape runs on
        put_pilot(bits, 64);
        pack(bits, out);
    }

    // The signal back to a tape image. It is fed with the time between every
    // two edges of the recorder's input, whichever way the level goes
    class Decoder
    {
    private:
        std::vector<uint32_t> halves;
        std::vector<uint8_t> result;
        size_t decoded_halves = 0;

        // Intervals to take the short half period from. The pilot tone in
        // front of the first header is nothing but those
        enum { SAMPLE = 1024 };

        void decode()
        {
            result.clear();
            decoded_halves = halves.size();
            if (halves.size() < SAMPLE) return;

            std::vector<uint32_t> head(halves.begin(), halves.begin() + SAMPLE);
            std::sort(head.begin(), head.end());
            const uint32_t unit = head[SAMPLE / 2];
            if (unit == 0) return;
            const uint32_t threshold = unit + unit / 2;

            // Half periods to bits, the way the ROM does it (130530): only the
            // first half of a bit is measured, the rest of it is skipped. The
            // line stands still between two calls of the driver, and the
            // interval across such a pause is no bit at all
            const uint8_t PAUSE = 2;
            const uint32_t pause = unit * 4;
            std::vector<uint8_t> bits;
            bits.reserve(halves.size() / 2);
            for (size_t i = 0; i < halves.size(); ) {
                if (halves[i] > pause)          { bits.push_back(PAUSE); i += 1; }
                else if (halves[i] > threshold) { bits.push_back(0); i += 2; }
                else                            { bits.push_back(1); i += 4; }
            }

            // Bits to records: a byte starts with a bit 0, and a long run of
            // ones in front of it means a new record. The length of that run
            // tells a header from data
            std::vector<std::vector<uint8_t>> records;
            std::vector<unsigned> pilots;
            unsigned ones = RECORD_GAP;
            bool paused = true;
            for (size_t i = 0; i < bits.size(); ) {
                if (bits[i] == PAUSE) { ones = 0; paused = true; i++; continue; }
                if (bits[i])          { ones++; i++; continue; }
                if (i + 9 > bits.size()) break;         // an unfinished byte
                uint8_t b = 0;
                for (int k = 0; k < 8; k++) b |= (uint8_t)((bits[i + 1 + k] & 1) << k);
                if (ones >= RECORD_GAP || paused) {
                    records.push_back(std::vector<uint8_t>());
                    pilots.push_back(ones);
                }
                records.back().push_back(b);
                ones = 0;
                paused = false;
                i += 9;
            }

            // Records to files: a header, then its data without the checksum;
            // data behind no header is a block. The first record may have
            // lost its pilot tone when the recording began late
            for (size_t r = 0; r < records.size(); r++) {
                const std::vector<uint8_t> &h = records[r];
                const bool header = pilots[r] >= HEADER_GAP || (r == 0 && h.size() <= HEADER_SIZE + 1);
                if (!header) {
                    if (h.size() < 4) continue;         // noise: not a word and its sum
                    const size_t words = std::min<size_t>(h.size() / 2 - 1, BLOCK_FLAG - 1);
                    const uint16_t length = (uint16_t)(BLOCK_FLAG | words);
                    result.insert(result.end(), NAME_SIZE, 0);
                    result.push_back((uint8_t)(length & 0xFF));
                    result.push_back((uint8_t)(length >> 8));
                    result.push_back(0);
                    result.push_back(0);
                    result.insert(result.end(), h.begin(), h.begin() + words * 2);
                    continue;
                }
                if (h.size() < NAME_SIZE) continue;     // noise, not a header
                result.insert(result.end(), h.begin(), h.begin() + NAME_SIZE);
                const uint16_t length = (h.size() >= HEADER_SIZE)? get_word(h, NAME_SIZE) : 0;
                result.push_back((uint8_t)(length & 0xFF));
                result.push_back((uint8_t)(length >> 8));
                const uint16_t address = (h.size() >= HEADER_SIZE)? get_word(h, NAME_SIZE + 2) : 0;
                result.push_back((uint8_t)(address & 0xFF));
                result.push_back((uint8_t)(address >> 8));
                if (length == 0) continue;
                std::vector<uint8_t> data;
                if (r + 1 < records.size() && pilots[r + 1] < HEADER_GAP) data = records[++r];
                data.resize((size_t)length * 2, 0);
                result.insert(result.end(), data.begin(), data.end());
            }
        }

    public:
        void reset()
        {
            halves.clear();
            result.clear();
            decoded_halves = 0;
        }

        void add_half(uint32_t cycles)
        {
            halves.push_back(cycles);
        }

        const std::vector<uint8_t> * file()
        {
            if (decoded_halves != halves.size()) decode();
            return &result;
        }
    };
}
