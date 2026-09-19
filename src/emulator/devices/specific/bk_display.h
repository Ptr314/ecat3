// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

#define BK_OPTION_COLORS    1
#define BK_COLOR_ON         0
#define BK_COLOR_OFF        1

// К1801ВП1-037 draws the picture along the raster: every line is laid out when
// the beam passes it, with the palette, the video page and the quarter screen
// bit as they are at that moment, the way Agat displays do it. The scroll
// register is taken once a frame, in the service lines.
class BKDisplay: public RasterDisplay
{
private:
    RAM * vram[2]{};
    Port * port_scroll{};
    Port * port_control{};

    Interface i_frame;              // frame pulse at the end of the picture lines

    unsigned m_scroll_base;         // scroll register value that means "not scrolled"
    unsigned m_scroll;              // scroll register as taken for the current frame
    unsigned m_offset;              // first video line shown at the top
    unsigned m_line_bytes;          // bytes of video memory per screen line
    unsigned m_video_lines;         // picture lines
    bool m_color;

    // Palette each screen line was drawn with, for scripts: 16 marks a blank line.
    // The second array is the last frame completed, taken when the picture ends.
    uint8_t m_line_palette[256]{};
    uint8_t m_frame_palette[256]{};
    unsigned m_frames = 0;          // frame pulses given since the start

    // A mode change requested from the interface thread, applied by the render one
    volatile bool m_mode_pending = false;
    bool m_pending_color = true;

    unsigned control() const;
    bool quarter() const;
    void latch_scroll();

    void render_line(unsigned line);            // takes the surface lock
    void render_line_unlocked(unsigned line);   // for callers already holding it
    void render_line_blank(uint8_t * base, unsigned width) const;
    void render_line_mono(uint8_t * base, unsigned src, RAM * vmem) const;
    void render_line_color(uint8_t * base, unsigned src, RAM * vmem, unsigned palette) const;

protected:
    void render_all(bool force_render) override;
    void HSYNC(unsigned line, unsigned sync_val) override;

public:
    BKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    ConfigFields get_config_fields() override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
};

ComputerDevice * create_bk_display(InterfaceManager *im, EmulatorConfigDevice *cd);
