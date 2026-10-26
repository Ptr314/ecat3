// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Frame blending, the mix of the pictures a program alternates,
//              and phosphor persistence

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

//Light of a byte value at the gamma of 2.2, 0..65535, and back
struct LightGamma {
    uint16_t light[256];
    uint8_t value[65536];
    LightGamma()
    {
        for (int i = 0; i < 256; i++)
            light[i] = static_cast<uint16_t>(std::pow(i / 255.0, 2.2) * 65535 + 0.5);
        for (int i = 0; i < 65536; i++)
            value[i] = static_cast<uint8_t>(std::pow(i / 65535.0, 1 / 2.2) * 255 + 0.5);
    }
    static const LightGamma & get()
    {
        static const LightGamma g;
        return g;
    }
};

//Frame blending: a program that alternates two pictures to mix their
//colours (the 4k intro of the БК) is shown as the mix, steady instead of
//flickering: the host's screen holds each frame whole, and 50 frames on a
//60 Hz one break the alternation. Every point is shown as the mean of its
//value and the last different value it had, while that one is no older
//than BLEND_AGE frames: so a picture changed every frame (A B A B) and
//one changed every second frame (A A B B: Lion on the БК0010, whose copy
//of the picture takes longer than a frame) both come out steady, and a
//picture that changed once keeps the old one for two frames only. The
//mean is one of light, not of the values: half a frame of a full colour
//is half its light, which is 186 of 255 at the gamma of 2.2, not 128
//(the 10-colour palette of Lion by its author: 149..198 for such mixes;
//the plain mean of values came out about 40% darker). The mean is not a
//full one: the other value weighs BLEND_WEIGHT / 256, 95% of a half, so
//a little of the alternation stays, as on a real monitor, and the light
//of both frames together is the same as of the full mean.
//The window has one (VideoRenderer, on the render thread), a video
//recorded at the rate of the machine another (on the emulation thread):
//the ages count the frames each of them gets
class FrameBlender
{
private:
    static const uint8_t BLEND_AGE = 2;
    static const uint32_t BLEND_WEIGHT = 122;  //0.5 * 0.95 * 256

    bool m_started = false;
    bool m_settling = false;
    std::vector<uint32_t> m_last;     //The value of the frame before
    std::vector<uint32_t> m_other;    //The last value that differed from it
    std::vector<uint8_t> m_age;       //Frames since the point had that one

public:
    //n pixels of 4 bytes from src, the mix into dst (it may be src). The mix
    //is taken per byte, so it does not depend on the byte order of the surface
    void blend(const uint8_t * src, uint8_t * dst, size_t n)
    {
        const uint32_t * cur = reinterpret_cast<const uint32_t *>(src);
        uint32_t * out = reinterpret_cast<uint32_t *>(dst);
        //The first frame, or one of another size, has nothing to mix with
        if (!m_started || m_last.size() != n) {
            m_last.assign(cur, cur + n);
            m_other.assign(cur, cur + n);
            m_age.assign(n, BLEND_AGE + 1);
            m_started = true;
        }
        uint32_t * last = m_last.data();
        uint32_t * other = m_other.data();
        uint8_t * age = m_age.data();
        const LightGamma & g = LightGamma::get();
        bool mixing = false;
        for (size_t i = 0; i < n; i++) {
            const uint32_t b = cur[i];
            if (b != last[i]) {
                other[i] = last[i];
                age[i] = 1;
                last[i] = b;
            } else if (age[i] <= BLEND_AGE) {
                age[i]++;
            }
            if (age[i] <= BLEND_AGE) {
                const uint32_t a = other[i];
                uint32_t mix = 0;
                for (int k = 0; k < 32; k += 8) {
                    const uint32_t l = (g.light[(b >> k) & 0xFF] * (256 - BLEND_WEIGHT)
                                        + g.light[(a >> k) & 0xFF] * BLEND_WEIGHT) >> 8;
                    mix |= static_cast<uint32_t>(g.value[l]) << k;
                }
                out[i] = mix;
                mixing = true;
            } else {
                out[i] = b;
            }
        }
        m_settling = mixing;
    }

    //The frames before are forgotten, so the next blend() mixes in no stale ones
    void reset()
    {
        m_started = false;
        m_settling = false;
        std::vector<uint32_t>().swap(m_last);
        std::vector<uint32_t>().swap(m_other);
        std::vector<uint8_t>().swap(m_age);
    }

    bool started() const { return m_started; }
    //The mix last made is not the frame itself
    bool settling() const { return m_settling; }
};

//Phosphor persistence: the screen of a picture tube does not go dark the
//moment the beam has left a point, its light fades away exponentially
//(tens of ms on a common green monitor, P1; long, P39, about 150 ms), and
//a moving cursor or scrolled text leaves a trail. Every byte keeps its light,
//at the gamma of 2.2 as FrameBlender: a point lights up at once to the
//frame's value and, when that is darker, fades from what it had by
//exp(-dt / tau), dt being the time since the frame before. Each byte on its
//own, so a point fades in its own colour. A fading byte that comes within
//two of the frame's value is taken as faded out (the smallest light above
//black already reads 2). The fourth byte of a pixel (alpha) is not light
//and is copied. A pixel none of whose bytes fades and whose value is that
//of the frame before is copied whole: on a usual frame that is nearly all
//of them, so a pass costs about the points that change or fade
class PhosphorDecay
{
private:
    bool m_started = false;
    bool m_settling = false;
    std::vector<uint16_t> m_light;    //The light every byte shows
    std::vector<uint32_t> m_prev;     //Every pixel of the frame before
    std::vector<uint8_t> m_fading;    //1: a byte of the pixel is fading

public:
    //n pixels of 4 bytes from src, the picture into dst (it may be src);
    //tau and dt in seconds
    void apply(const uint8_t * src, uint8_t * dst, size_t n, double tau, double dt)
    {
        const size_t bytes = n * 4;
        const LightGamma & g = LightGamma::get();
        if (!m_started || m_light.size() != bytes) {
            m_light.resize(bytes);
            m_prev.resize(n);
            m_fading.assign(n, 0);
            for (size_t i = 0; i < bytes; i++) m_light[i] = g.light[src[i]];
            std::copy(src, src + bytes, reinterpret_cast<uint8_t *>(m_prev.data()));
            if (dst != src) std::copy(src, src + bytes, dst);
            m_started = true;
            m_settling = false;
            return;
        }
        //What is left of the light after dt, in 1/65536
        const double k = (tau > 0 && dt > 0) ? std::exp(-dt / tau) : 0;
        const uint32_t keep = static_cast<uint32_t>(k * 65536);
        uint16_t * light = m_light.data();
        bool fading = false;
        for (size_t p = 0; p < n; p++) {
            const uint8_t * s = src + p * 4;
            uint8_t * d = dst + p * 4;
            uint32_t now32;
            std::copy(s, s + 4, reinterpret_cast<uint8_t *>(&now32));
            if (!m_fading[p] && now32 == m_prev[p]) {
                //Its light is that of the frame already
                if (d != s) std::copy(s, s + 4, d);
                continue;
            }
            m_prev[p] = now32;
            bool pixel_fading = false;
            for (size_t c = 0; c < 3; c++) {
                const size_t i = p * 4 + c;
                const uint8_t b = s[c];
                const uint32_t now = g.light[b];
                const uint32_t left = (light[i] * keep) >> 16;
                if (left <= now) {
                    light[i] = static_cast<uint16_t>(now);
                    d[c] = b;
                } else {
                    const uint8_t v = g.value[left];
                    if (v <= b + 2) {
                        light[i] = static_cast<uint16_t>(now);
                        d[c] = b;
                    } else {
                        light[i] = static_cast<uint16_t>(left);
                        pixel_fading = true;
                        d[c] = v;
                    }
                }
            }
            light[p * 4 + 3] = g.light[s[3]];
            d[3] = s[3];
            m_fading[p] = pixel_fading ? 1 : 0;
            fading = fading || pixel_fading;
        }
        m_settling = fading;
    }

    //The light before is forgotten
    void reset()
    {
        m_started = false;
        m_settling = false;
        std::vector<uint16_t>().swap(m_light);
        std::vector<uint32_t>().swap(m_prev);
        std::vector<uint8_t>().swap(m_fading);
    }

    bool started() const { return m_started; }
    //Some points are still fading: the picture is not the frame itself
    bool settling() const { return m_settling; }
};
