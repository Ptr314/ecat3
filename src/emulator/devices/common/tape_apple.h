// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple ][ cassette encoding (the Правец-82 and 8М use it as is)
//
// The signal of WRITE and READ of the Apple ][ Monitor ($FECD, $FEFD), which
// БЕЙСИК's SAVE and LOAD call: every half period is a toggle of $C020 on
// writing and a change of bit 7 of $C060 on reading.
//
//   record       header tone of 770 Hz (half periods of 650 us), a sync
//                pulse (half periods of 200 and 250 us), the bytes, and their
//                checksum: $FF xored with every byte
//   bit          one period: 0 - 500 us (2 kHz), 1 - 1000 us (1 kHz)
//   byte         most significant bit first
//
// READ times the half periods itself, so the exact rate does not matter.
//
// A tape image is a WAV recording of a cassette, or a PTP file (Правец tape): the records one after another,
// each as its length (2 bytes, low first) and its bytes, without the
// checksum. A bare file is one record, the one the Monitor reads with
// "start.endR".
//
// On the tape here a step of the signal is 50 us (20000 steps a second).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace apple_tape
{
    const unsigned STEPS_PER_SECOND = 20000;
    const unsigned HEADER_HALF = 13;        // 650 us
    const unsigned SYNC_HALF_1 = 4;         // 200 us
    const unsigned SYNC_HALF_2 = 5;         // 250 us
    const unsigned ZERO_HALF = 5;           // 250 us
    const unsigned ONE_HALF = 10;           // 500 us
    // READ waits 3.5 s into the header before it looks for the sync, WRITE
    // writes 10 s of it; a header of 5 s is long enough for both READs of
    // LOAD, the one of the length and the one of the program
    const unsigned HEADER_SECONDS = 5;
    const unsigned LEAD_IN = STEPS_PER_SECOND / 2;

    inline void put_half(std::vector<uint8_t> &steps, unsigned &level, unsigned length)
    {
        steps.insert(steps.end(), length, (uint8_t)level);
        level ^= 1;
    }

    inline void put_record(std::vector<uint8_t> &steps, unsigned &level, const uint8_t *data, size_t size)
    {
        const unsigned header = HEADER_SECONDS * STEPS_PER_SECOND / HEADER_HALF;
        for (unsigned i = 0; i < header; i++) put_half(steps, level, HEADER_HALF);
        put_half(steps, level, SYNC_HALF_1);
        put_half(steps, level, SYNC_HALF_2);
        uint8_t sum = 0xFF;
        for (size_t i = 0; i <= size; i++) {
            const uint8_t b = (i < size) ? data[i] : sum;
            if (i < size) sum ^= b;
            for (int k = 7; k >= 0; k--) {
                const unsigned half = ((b >> k) & 1) ? ONE_HALF : ZERO_HALF;
                put_half(steps, level, half);
                put_half(steps, level, half);
            }
        }
        // The line rests a moment before the next header
        put_half(steps, level, ONE_HALF);
    }

    // Is this a PTP file? Its records fill it exactly
    inline bool is_ptp(const std::vector<uint8_t> &file)
    {
        size_t i = 0;
        while (i + 2 <= file.size()) {
            const size_t len = file[i] | (file[i + 1] << 8);
            if (len == 0) return false;
            i += 2 + len;
        }
        return i == file.size() && !file.empty();
    }

    // A PTP file (or a bare file as one record) to the signal: steps of
    // levels, packed 8 to a byte, most significant first
    inline void build_steps(const std::vector<uint8_t> &file, bool ptp, std::vector<uint8_t> &steps)
    {
        steps.assign(LEAD_IN, 0);
        unsigned level = 0;
        if (ptp) {
            for (size_t i = 0; i + 2 <= file.size(); ) {
                const size_t len = file[i] | (file[i + 1] << 8);
                put_record(steps, level, file.data() + i + 2, len);
                i += 2 + len;
            }
        } else
            put_record(steps, level, file.data(), file.size());
        steps.insert(steps.end(), STEPS_PER_SECOND / 4, (uint8_t)level);
    }

    inline void encode(const std::vector<uint8_t> &file, bool ptp, std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> steps;
        build_steps(file, ptp, steps);
        out.reserve(out.size() + (steps.size() + 7) / 8);
        for (size_t i = 0; i < steps.size(); i += 8) {
            uint8_t b = 0;
            for (size_t k = 0; k < 8; k++)
                b = (uint8_t)((b << 1) | ((i + k < steps.size()) ? steps[i + k] : 0));
            out.push_back(b);
        }
    }

    // A PTP file as sound, to be played into a real machine or kept: a
    // square wave, 22050 Hz, 8 bits, mono, silence before and after
    inline void ptp_to_wav(const std::vector<uint8_t> &ptp, std::vector<uint8_t> &wav)
    {
        const unsigned RATE = 22050;
        std::vector<uint8_t> steps;
        build_steps(ptp, true, steps);
        const size_t count = (size_t)((uint64_t)steps.size() * RATE / STEPS_PER_SECOND);
        std::vector<uint8_t> pcm(count);
        // The lead-in is silence, not a held level: the first edge is the
        // first half period of the header
        for (size_t n = 0; n < count; n++) {
            const size_t st = (size_t)((uint64_t)n * STEPS_PER_SECOND / RATE);
            pcm[n] = (st < LEAD_IN) ? 128 : (steps[st] ? 218 : 38);
        }
        const uint32_t size = (uint32_t)pcm.size();
        wav.clear();
        const uint8_t head[44] = {
            'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
            (uint8_t)RATE, (uint8_t)(RATE >> 8), 0, 0, (uint8_t)RATE, (uint8_t)(RATE >> 8), 0, 0, 1, 0, 8, 0,
            'd', 'a', 't', 'a', 0, 0, 0, 0 };
        wav.assign(head, head + 44);
        const uint32_t riff = size + 36;
        for (int k = 0; k < 4; k++) { wav[4 + k] = (uint8_t)(riff >> (8 * k)); wav[40 + k] = (uint8_t)(size >> (8 * k)); }
        wav.insert(wav.end(), pcm.begin(), pcm.end());
    }

    // A recording of a real cassette, a WAV file (PCM 8, 16, 24 or 32 bits,
    // or 32-bit float; any rate, channels mixed): the level the input of the
    // machine sees, a comparator with some hysteresis after the DC is taken
    // out. The moments it flips are found between samples, so a rate of
    // 11 kHz still places them to a step of 50 us. False: not a WAV file
    inline bool wav_to_steps(const std::vector<uint8_t> &f, std::vector<uint8_t> &out)
    {
        if (f.size() < 44 || memcmp(f.data(), "RIFF", 4) != 0 || memcmp(f.data() + 8, "WAVE", 4) != 0) return false;
        unsigned format = 0, channels = 0, rate = 0, bits = 0;
        size_t data = 0, data_size = 0;
        for (size_t at = 12; at + 8 <= f.size(); ) {
            const size_t size = f[at + 4] | (f[at + 5] << 8) | (f[at + 6] << 16) | ((size_t)f[at + 7] << 24);
            if (memcmp(&f[at], "fmt ", 4) == 0 && size >= 16 && at + 24 <= f.size()) {
                format = f[at + 8] | (f[at + 9] << 8);
                channels = f[at + 10] | (f[at + 11] << 8);
                rate = f[at + 12] | (f[at + 13] << 8) | (f[at + 14] << 16) | ((unsigned)f[at + 15] << 24);
                bits = f[at + 22] | (f[at + 23] << 8);
                if (format == 0xFFFE && size >= 26 && at + 34 <= f.size()) format = f[at + 32] | (f[at + 33] << 8);
            } else if (memcmp(&f[at], "data", 4) == 0) {
                data = at + 8;
                data_size = (size < f.size() - data) ? size : f.size() - data;
                break;
            }
            at += 8 + size + (size & 1);
        }
        const unsigned bytes = bits / 8;
        if (data == 0 || channels == 0 || rate < 4000 || bytes == 0 || bytes > 4
            || !((format == 1 && bits % 8 == 0) || (format == 3 && bits == 32))) return false;
        const size_t frame = (size_t)bytes * channels;
        const size_t count = data_size / frame;
        if (count == 0) return false;

        // Samples, mixed to one channel
        std::vector<float> s(count);
        float peak = 0;
        for (size_t i = 0; i < count; i++) {
            float sum = 0;
            for (unsigned c = 0; c < channels; c++) {
                const uint8_t *p = &f[data + i * frame + c * bytes];
                float v;
                if (format == 3) { uint32_t u = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); memcpy(&v, &u, 4); }
                else if (bytes == 1) v = (p[0] - 128) / 128.0f;
                else {
                    int32_t x = 0;
                    for (unsigned k = 0; k < bytes; k++) x |= (int32_t)p[k] << (8 * (4 - bytes + k));
                    v = x / 2147483648.0f;
                }
                sum += v;
            }
            s[i] = sum / channels;
        }
        // The DC out: a high-pass of some 50 Hz, far below the tones
        const float a = 1.0f / (1.0f + 2.0f * 3.14159265f * 50.0f / rate);
        float prev_in = s[0], prev_out = 0;
        for (size_t i = 0; i < count; i++) {
            const float y = a * (prev_out + s[i] - prev_in);
            prev_in = s[i];
            prev_out = y;
            s[i] = y;
            if (y > peak) peak = y;
            if (-y > peak) peak = -y;
        }
        if (peak <= 0) return false;
        // The comparator: it flips past a tenth of the loudest swing either
        // way; where it flips, between the two samples
        const float h = peak * 0.1f;
        std::vector<double> flips;      // seconds
        int state = 0;
        for (size_t i = 1; i < count; i++) {
            const float th = state ? -h : h;
            const bool cross = state ? (s[i] < th) : (s[i] > th);
            if (!cross) continue;
            const float d = s[i] - s[i - 1];
            const double frac = (d != 0) ? (th - s[i - 1]) / d : 0.0;
            flips.push_back((i - 1 + frac) / rate);
            state ^= 1;
        }
        const size_t steps = (size_t)((double)count / rate * STEPS_PER_SECOND) + 1;
        out.reserve(out.size() + steps / 8 + 1);
        size_t next = 0;
        unsigned level = 0;
        uint8_t b = 0;
        for (size_t i = 0; i < steps; i++) {
            const double t = (double)i / STEPS_PER_SECOND;
            while (next < flips.size() && flips[next] <= t) { level ^= 1; next++; }
            b = (uint8_t)((b << 1) | level);
            if ((i & 7) == 7) { out.push_back(b); b = 0; }
        }
        if (steps & 7) out.push_back((uint8_t)(b << (8 - (steps & 7))));
        return true;
    }

    // The signal the machine writes back to a PTP file. It gets the length of
    // every half period, in clocks of the machine. Header half periods open a
    // record, the first short one after them is the sync pulse (it and the
    // next), then pairs of halves are the bits. A header half period, or the
    // end of the recording, closes the record
    class Decoder
    {
    public:
        // clock: of the machine, Hz
        void reset(uint32_t clock)
        {
            m_short = (uint32_t)((uint64_t)clock * 375 / 1000000);     // 0 from 1
            m_long = (uint32_t)((uint64_t)clock * 575 / 1000000);      // 1 from the header
            m_file.clear();
            m_state = IDLE;
            m_headers = 0;
        }

        void add_half(uint32_t length)
        {
            if (length >= m_long) {
                if (m_state == DATA) end_record();
                m_state = IDLE;
                m_headers++;
                return;
            }
            const int h = (length >= m_short) ? 1 : 0;
            switch (m_state) {
                case IDLE:
                    // A header long enough to be one, then the sync pulse
                    if (m_headers >= 64 && h == 0) m_state = SYNC;
                    m_headers = 0;
                    return;
                case SYNC:
                    // The second half of the sync pulse
                    m_state = DATA;
                    m_bytes.clear();
                    m_byte = 0;
                    m_bits = 0;
                    m_half = -1;
                    return;
                case DATA:
                    if (m_half < 0) { m_half = h; return; }
                    if (m_half != h) { m_half = h; return; }    // out of step: the pair starts here
                    m_half = -1;
                    add_bit((unsigned)h);
                    return;
            }
        }

        // The recording stopped: whatever record is open is complete
        void finish() { if (m_state == DATA) end_record(); m_state = IDLE; }

        const std::vector<uint8_t> * file() const { return &m_file; }
        // Records whose checksum did not come out
        unsigned bad() const { return m_bad; }

    private:
        enum { IDLE, SYNC, DATA } m_state = IDLE;
        uint32_t m_short = 1;
        uint32_t m_long = 1;
        unsigned m_headers = 0;
        std::vector<uint8_t> m_file;
        std::vector<uint8_t> m_bytes;
        uint8_t m_byte = 0;
        unsigned m_bits = 0;
        int m_half = -1;
        unsigned m_bad = 0;

        void add_bit(unsigned bit)
        {
            m_byte = (uint8_t)((m_byte << 1) | (bit & 1));
            if (++m_bits == 8) {
                m_bytes.push_back(m_byte);
                m_byte = 0;
                m_bits = 0;
            }
        }

        void end_record()
        {
            // The last half period of a record has no edge after it: WRITE
            // stops toggling there. Its pair is what it would have been
            if (m_half >= 0) { add_bit((unsigned)m_half); m_half = -1; }
            m_state = IDLE;
            if (m_bytes.size() < 2) return;
            uint8_t sum = 0xFF;
            for (size_t i = 0; i < m_bytes.size(); i++) sum ^= m_bytes[i];
            if (sum != 0) m_bad++;
            const size_t len = m_bytes.size() - 1;
            m_file.push_back((uint8_t)len);
            m_file.push_back((uint8_t)(len >> 8));
            m_file.insert(m_file.end(), m_bytes.begin(), m_bytes.end() - 1);
        }
    };
}
