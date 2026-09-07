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
    , m_screen_line(0)
    , m_line_counter(0)
    , m_hsync_counter(0)
    , m_hsync_active(false)
{}

emulator::Result RasterDisplay::load_config(SystemData *sd)
{
    m_clocked = true;   //clock() is overridden here
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    if (m_standart == "625/50") {
        m_lines = 625;
        m_half_frame_lines = m_lines / 2;
        m_frame_rate = 50;
        m_interlaced = true;
        m_top_blank = 23;
        m_bottom_blank = 4;
        m_hsync_length_ms = 12;
    } else {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{RasterDisplay|" + std::string(QT_TRANSLATE_NOOP("RasterDisplay", "Unknown video standard")) + "}");
    }

    m_counts_per_line = (m_interlaced?2:1) * m_system_clock / m_lines / m_frame_rate;
    m_counts_hsync = m_system_clock * m_hsync_length_ms / 1000000;

    return emulator::Result::ok();
}

void RasterDisplay::clock(unsigned int counter)
{
    m_line_counter += counter;
    if (m_line_counter >= m_counts_per_line) {
        m_line_counter -= m_counts_per_line;
        if (m_interlaced) {
            m_screen_line = (m_current_line <= 312) ? (2 * m_current_line) : (2 * (m_current_line - 313) + 1);
        } else {
            im->dm->error(this, "Non-interlaced mode is not supported yet");
        }

        if (m_current_line == 0)
            FRAME_SYNC();
        if (m_current_line == m_lines - m_bottom_blank || m_current_line == m_half_frame_lines - m_bottom_blank)
            VSYNC(0);
        if (m_current_line == m_top_blank || m_current_line == m_half_frame_lines + m_top_blank)
            VSYNC(1);

        HSYNC(m_screen_line, 0);
        m_hsync_counter = 0;
        m_hsync_active = true;

        if (m_current_line++ > 624) m_current_line = 0;
    }

    m_hsync_counter += counter;
    if (m_hsync_active && m_hsync_counter >= m_counts_hsync) {
        HSYNC(m_screen_line, 1);
        m_hsync_active = false;
    }

}

std::vector<DeviceFieldInfo> RasterDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"standard",    "Name of the raster standard from the config",  false});
    r.push_back({"frame_rate",  "Frames per second",                            false});
    r.push_back({"lines",       "Lines in a frame, blanking included",          false});
    r.push_back({"line",        "Raster line the beam is on right now",         false});
    r.push_back({"screen_line", "Visible line it corresponds to",               false});
    r.push_back({"hsync",       "1 while the horizontal sync pulse is active",  false});
    r.push_back({"interlaced",  "1 for an interlaced standard",                 false});
    return r;
}

bool RasterDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //Where the beam is, for the machines that hang their timing off it
    if (field == "standard")
    {
        out.numeric = false;
        out.text = m_standart;
        return true;
    }

    if (field == "frame_rate" || field == "lines" || field == "line" ||
        field == "screen_line" || field == "hsync" || field == "interlaced")
    {
        out.numeric = true;
        if (field == "frame_rate")       out.values.push_back(m_frame_rate);
        else if (field == "lines")       out.values.push_back(m_lines);
        else if (field == "line")        out.values.push_back(m_current_line);
        else if (field == "screen_line") out.values.push_back(m_screen_line);
        else if (field == "hsync")       out.values.push_back(m_hsync_active ? 1 : 0);
        else                             out.values.push_back(m_interlaced ? 1 : 0);
        return true;
    }

    return GenericDisplay::get_field(field, from, to, out);
}
