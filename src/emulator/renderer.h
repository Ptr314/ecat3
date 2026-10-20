// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Renderer abstract class

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "frame_blender.h"

class VideoRenderer
{
private:
    //Frame blending (FrameBlender). Set from the GUI thread, taken up by the
    //render thread
    std::atomic<bool> m_blend_wanted{false};
    FrameBlender m_blender;

protected:
    int screen_x = 0;
    int screen_y = 0;
    double screen_ss = 1;
    double screen_ps = 1;

    //Called by render() on the render thread with the surface, screen_x *
    //screen_y pixels of 4 bytes. True: dst has the picture to show; false:
    //blending is off, the surface is shown as it is
    bool blend_frame(const uint8_t * src, uint8_t * dst)
    {
        if (!m_blend_wanted.load(std::memory_order_relaxed)) {
            blend_off();
            return false;
        }
        m_blender.blend(src, dst, static_cast<size_t>(screen_x) * screen_y);
        return true;
    }

    //For a render() that skips blend_frame() while blending is off: the frames
    //before are forgotten, so turning it on again does not mix in stale ones
    void blend_off()
    {
        if (m_blender.started()) m_blender.reset();
    }

public:
    void set_frame_blending(bool on) { m_blend_wanted.store(on, std::memory_order_relaxed); }
    bool frame_blending() const { return m_blend_wanted.load(std::memory_order_relaxed); }
    //The mix last shown is not the frame itself: a display that is not
    //changing any more still needs one more render() to show it clean
    bool blend_settling() const { return m_blender.settling(); }

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
