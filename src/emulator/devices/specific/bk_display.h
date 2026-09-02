// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК display controller device

#pragma once

#include "emulator/core.h"

#define BK_OPTION_COLORS    1
#define BK_COLOR_ON         0
#define BK_COLOR_OFF        1

class BKDisplay: public GenericDisplay
{
private:
    RAM * vram[2]{};
    Port * port_scroll{};
    Port * port_control{};

    unsigned m_scroll_base;         // scroll register value that means "not scrolled"
    unsigned m_scroll;              // last seen scroll register value
    unsigned m_offset;              // first video line shown at the top
    unsigned m_line_bytes;          // bytes of video memory per screen line
    unsigned m_lines;               // screen lines
    unsigned m_control;             // last seen value of the palette/page register
    unsigned m_palette;             // palette number, 0 on machines without the register
    unsigned m_page;                // video RAM shown, 0 on machines with a single one
    bool m_color;

    // A mode change requested from the interface thread, applied by the render one
    volatile bool m_mode_pending = false;
    bool m_pending_color = true;

    void render_line_mono(unsigned line, RAM * src) const;
    void render_line_color(unsigned line, RAM * src, unsigned palette) const;

protected:
    void render_all(bool force_render) override;

public:
    BKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;

    void clock(unsigned int counter) override;
    emulator::Result load_config(SystemData *sd) override;

    void memory_callback(unsigned int callback_id, unsigned int address) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
};

ComputerDevice * create_bk_display(InterfaceManager *im, EmulatorConfigDevice *cd);
