// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Renderer abstract class

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "frame_blender.h"

//Screen > Persistence: [Video] persistence
#define SCREEN_PERSISTENCE_OFF      0
#define SCREEN_PERSISTENCE_SHORT    1
#define SCREEN_PERSISTENCE_LONG     2

//The time the light of the phosphor takes to fall to 1/e, s
inline double persistence_tau(int persistence)
{
    switch (persistence) {
        case SCREEN_PERSISTENCE_SHORT: return 0.030;    //A common green monitor, P1
        case SCREEN_PERSISTENCE_LONG:  return 0.150;    //A long one, P39
        default: return 0;
    }
}

class VideoRenderer
{
private:
    //Frame blending (FrameBlender) and phosphor persistence (PhosphorDecay).
    //Set from the GUI thread, taken up by the render thread
    std::atomic<bool> m_blend_wanted{false};
    std::atomic<int> m_persistence{SCREEN_PERSISTENCE_OFF};
    FrameBlender m_blender;
    PhosphorDecay m_phosphor;
    std::chrono::steady_clock::time_point m_phosphor_time;

protected:
    int screen_x = 0;
    int screen_y = 0;
    double screen_ss = 1;
    double screen_ps = 1;

    //Called by render() on the render thread with the surface, screen_x *
    //screen_y pixels of 4 bytes. True: dst has the picture to show, the
    //frames blended and then faded by the time that has passed since the
    //render() before; false: both are off, the surface is shown as it is
    bool blend_frame(const uint8_t * src, uint8_t * dst)
    {
        const bool blend = m_blend_wanted.load(std::memory_order_relaxed);
        const int persistence = m_persistence.load(std::memory_order_relaxed);
        if (!blend && m_blender.started()) m_blender.reset();
        if (persistence == SCREEN_PERSISTENCE_OFF && m_phosphor.started()) m_phosphor.reset();
        if (!blend && persistence == SCREEN_PERSISTENCE_OFF) return false;
        const size_t n = static_cast<size_t>(screen_x) * screen_y;
        if (blend) {
            m_blender.blend(src, dst, n);
            src = dst;
        }
        if (persistence != SCREEN_PERSISTENCE_OFF) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            const double dt = m_phosphor.started()
                ? std::chrono::duration<double>(now - m_phosphor_time).count() : 0;
            m_phosphor_time = now;
            m_phosphor.apply(src, dst, n, persistence_tau(persistence), dt);
        }
        return true;
    }

    //For a render() that skips blend_frame() while both are off: the frames
    //before are forgotten, so turning it on again does not mix in stale ones
    void blend_off()
    {
        if (m_blender.started()) m_blender.reset();
        if (m_phosphor.started()) m_phosphor.reset();
    }

public:
    void set_frame_blending(bool on) { m_blend_wanted.store(on, std::memory_order_relaxed); }
    bool frame_blending() const { return m_blend_wanted.load(std::memory_order_relaxed); }
    void set_persistence(int value) { m_persistence.store(value, std::memory_order_relaxed); }
    int persistence() const { return m_persistence.load(std::memory_order_relaxed); }
    //blend_frame() has something to do: the picture shown is not the surface
    bool frame_processing() const { return frame_blending() || persistence() != SCREEN_PERSISTENCE_OFF; }
    //The mix last shown is not the frame itself, or points are still fading:
    //a display that is not changing any more still needs more render() calls
    //to show it clean
    bool blend_settling() const { return m_blender.settling() || m_phosphor.settling(); }

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
