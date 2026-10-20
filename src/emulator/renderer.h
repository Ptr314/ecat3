// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Renderer abstract class

#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class VideoRenderer
{
private:
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
    //Set from the GUI thread, taken up by the render thread
    static const uint8_t BLEND_AGE = 2;
    static const uint32_t BLEND_WEIGHT = 122;  //0.5 * 0.95 * 256

    //Light of a byte value at the gamma of 2.2, 0..65535, and back
    struct BlendGamma {
        uint16_t light[256];
        uint8_t value[65536];
        BlendGamma()
        {
            for (int i = 0; i < 256; i++)
                light[i] = static_cast<uint16_t>(std::pow(i / 255.0, 2.2) * 65535 + 0.5);
            for (int i = 0; i < 65536; i++)
                value[i] = static_cast<uint8_t>(std::pow(i / 65535.0, 1 / 2.2) * 255 + 0.5);
        }
    };
    static const BlendGamma & blend_gamma()
    {
        static const BlendGamma g;
        return g;
    }
    std::atomic<bool> m_blend_wanted{false};
    bool m_blending = false;
    bool m_blend_settling = false;
    std::vector<uint32_t> m_blend_last;     //The value of the frame before
    std::vector<uint32_t> m_blend_other;    //The last value that differed from it
    std::vector<uint8_t> m_blend_age;       //Frames since the point had that one

protected:
    int screen_x = 0;
    int screen_y = 0;
    double screen_ss = 1;
    double screen_ps = 1;

    //Called by render() on the render thread with the surface, screen_x *
    //screen_y pixels of 4 bytes. True: dst has the picture to show; false:
    //blending is off, the surface is shown as it is. The mix is taken per
    //byte, so it does not depend on the byte order of the surface
    bool blend_frame(const uint8_t * src, uint8_t * dst)
    {
        if (!m_blend_wanted.load(std::memory_order_relaxed)) {
            blend_off();
            return false;
        }
        const size_t n = static_cast<size_t>(screen_x) * screen_y;
        const uint32_t * cur = reinterpret_cast<const uint32_t *>(src);
        uint32_t * out = reinterpret_cast<uint32_t *>(dst);
        //The first frame, or one of another size, has nothing to mix with
        if (!m_blending || m_blend_last.size() != n) {
            m_blend_last.assign(cur, cur + n);
            m_blend_other.assign(cur, cur + n);
            m_blend_age.assign(n, BLEND_AGE + 1);
            m_blending = true;
        }
        uint32_t * last = m_blend_last.data();
        uint32_t * other = m_blend_other.data();
        uint8_t * age = m_blend_age.data();
        const BlendGamma & g = blend_gamma();
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
        m_blend_settling = mixing;
        return true;
    }

    //For a render() that skips blend_frame() while blending is off: the frames
    //before are forgotten, so turning it on again does not mix in stale ones
    void blend_off()
    {
        m_blending = false;
        m_blend_settling = false;
        std::vector<uint32_t>().swap(m_blend_last);
        std::vector<uint32_t>().swap(m_blend_other);
        std::vector<uint8_t>().swap(m_blend_age);
    }

public:
    void set_frame_blending(bool on) { m_blend_wanted.store(on, std::memory_order_relaxed); }
    bool frame_blending() const { return m_blend_wanted.load(std::memory_order_relaxed); }
    //The mix last shown is not the frame itself: a display that is not
    //changing any more still needs one more render() to show it clean
    bool blend_settling() const { return m_blend_settling; }

    VideoRenderer() {};
    virtual ~VideoRenderer() {};
    virtual std::string get_name() = 0;
    virtual void init_screen(void *p, int sx, int sy, double ss, double ps)
    {
        screen_x = sx;
        screen_y = sy;
        screen_ss = ss;
        screen_ps = ps;
    };
    virtual void stop() = 0;
    virtual void set_filtering(int value) {};
    //Lines the beam draws per frame (GenericDisplay::get_scan_lines()), asked
    //for every frame; only a renderer that draws scan lines needs it
    virtual void set_scan_lines(int lines) {};
    //The size of the surface, as get_screenshot() returns it
    int width() const { return screen_x; }
    int height() const { return screen_y; }
    virtual uint8_t * get_buffer() = 0;
    virtual int get_line_bytes() = 0;
    virtual void fill(uint32_t c) = 0;
    virtual void resize(int sx, int sy, double ss, double ps) = 0;
    virtual void render() = 0;
    virtual uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) = 0;
    virtual void FillRGB(const uint8_t colors[][3], uint32_t * RGBA, int len)
    {
        for (int i=0; i<len; i++)
            RGBA[i] = MapRGB(colors[i][0], colors[i][1], colors[i][2]);
    }
    //The screen as raw RGBA, exactly screen_x * screen_y * 4 bytes, R first.
    //Not an encoded image: Emulator::store_screenshot() makes the PNG itself,
    //and a smaller buffer is dropped. Renderers whose surface keeps
    //another byte order (the Qt ones) undo it here, so that the screenshots of
    //every renderer stay comparable with each other
    virtual std::vector<uint8_t> get_screenshot() = 0;
};
