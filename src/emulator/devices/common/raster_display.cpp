// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract class for raster displays, which need frame/line events for working


#include "raster_display.h"
#include <iostream>

RasterDisplay::RasterDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , m_current_line(0)
    , m_standart("625/50")
{}

void RasterDisplay::load_config(SystemData *sd)
{
    GenericDisplay::load_config(sd);

    if (m_standart == "625/50") {
        m_lines = 625;
        m_half_frame_lines = m_lines / 2;
        m_frame_rate = 50;
        m_interlaced = true;
        m_top_blank = 23;
        m_bottom_blank = 4;
    } else {
        im->dm->error(this, "Unknown video standard");
    }

    m_counts_per_line = (m_interlaced?2:1) * m_system_clock / m_lines / m_frame_rate;
}

void RasterDisplay::clock(unsigned int counter)
{
    unsigned screen_line = 0;
    m_line_counter += counter;
    if (m_line_counter >= m_counts_per_line) {
        m_line_counter -= m_counts_per_line;
        if (m_interlaced) {
            screen_line = (m_current_line <= 312) ? (2 * m_current_line) : (2 * (m_current_line - 313) + 1);
        } else {
            im->dm->error(this, "Non-interlaced mode is not supported yet");
        }

        if (m_current_line == 0)
            FRAME_SYNC();
        if (m_current_line == m_lines - m_bottom_blank || m_current_line == m_half_frame_lines - m_bottom_blank)
            VSYNC(0);
        if (m_current_line == m_top_blank || m_current_line == m_half_frame_lines + m_top_blank)
            VSYNC(1);

        HSYNC(screen_line, 0);

        if (m_current_line++ > 624) m_current_line = 0;
    }
}

