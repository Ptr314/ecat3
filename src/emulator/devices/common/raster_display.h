// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract class for raster displays, which need frame/line events for working

#pragma once

#include "emulator/core.h"

class RasterDisplay:public GenericDisplay
{
private:
    unsigned m_frame_rate;
    unsigned m_half_frame_lines;
    bool m_interlaced;
    unsigned m_line_counter;
    unsigned m_current_line;
    unsigned m_counts_per_line;     // Clocks of a line, rounded down
    unsigned m_frame_clocks = 0;    // Clocks of a frame, exactly
    unsigned m_line_rem = 0;        // m_frame_clocks % m_lines, spread over the lines
    unsigned m_rem_acc = 0;         // Of m_line_rem, what has built up so far
    unsigned m_line_length = 0;     // Of the line being scanned: m_counts_per_line or one more
    unsigned m_screen_line;
    unsigned m_hsync_counter;
    unsigned m_hsync_length_ms;
    unsigned m_counts_hsync;
    bool m_hsync_active;

protected:
    // Chosen by the subclass before load_config(): "625/50" (the default) or
    // "vp1-037", the 320 line progressive raster of the БК video controller
    std::string m_standart;
    unsigned m_lines;
    unsigned m_top_blank;
    unsigned m_bottom_blank;

    virtual void FRAME_SYNC() {};
    virtual void VSYNC(const unsigned sync_val) {};
    virtual void HSYNC(const unsigned line, const unsigned sync_val) {};
    //Clocks of the display's own domain per frame, both fields of an
    //interlaced one
    unsigned frame_clocks() const { return m_frame_clocks; }
    //Pictures a second, reduced: the display's clock over frame_clocks(),
    //times the pictures of a frame (2 for an interlaced one drawn in both
    //fields). For a subclass that calls frame_complete() at each picture's end
    bool raster_frame_rate(uint64_t &num, uint64_t &den, unsigned pictures = 1) const;
public:
    RasterDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};
