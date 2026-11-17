// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A recording of a cassette (WAV) as the levels the input of a machine sees
//
// Shared by every tape that reads such recordings: the Apple ][ and Oric ones
// (Правец-82, 8М, 8Д) and the БК. What differs is only the grid the levels
// are put on: 50 us is enough for the Apple ][ and the Oric, the БК, whose
// half periods are 105 us and less, gets a finer one.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace wav_tape
{
    // A recording of a real cassette, a WAV file (PCM 8, 16, 24 or 32 bits,
    // or 32-bit float; any rate, channels mixed): the level the input of the
    // machine sees, a comparator with some hysteresis after the DC is taken
    // out. The moments it flips are found between samples and put on a grid
    // of steps_per_second, so a rate of 11 kHz still places them to a step of
    // 50 us. The levels are packed 8 to a byte, most significant first. False:
    // not a WAV file
    inline bool wav_to_steps(const std::vector<uint8_t> &f, std::vector<uint8_t> &out, unsigned steps_per_second)
    {
        if (steps_per_second == 0) return false;
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
            // A chunk longer than the file is the end of it. With a 32-bit
            // size_t (the XP kit, the web) a size near 4 GB would otherwise
            // wrap the offset back onto the same chunk, and the walk would
            // never end
            if (size > f.size() - at - 8) break;
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
        const size_t steps = (size_t)((double)count / rate * steps_per_second) + 1;
        out.reserve(out.size() + steps / 8 + 1);
        size_t next = 0;
        unsigned level = 0;
        uint8_t b = 0;
        for (size_t i = 0; i < steps; i++) {
            const double t = (double)i / steps_per_second;
            while (next < flips.size() && flips[next] <= t) { level ^= 1; next++; }
            b = (uint8_t)((b << 1) | level);
            if ((i & 7) == 7) { out.push_back(b); b = 0; }
        }
        if (steps & 7) out.push_back((uint8_t)(b << (8 - (steps & 7))));
        return true;
    }
}
