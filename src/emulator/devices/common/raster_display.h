// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract class for raster displays, which need frame/line events for working

#pragma once

#include "emulator/core.h"

class RasterDisplay:public GenericDisplay
{
private:
    std::string m_standart;
    unsigned m_frame_rate;
    unsigned m_lines;
    unsigned m_half_frame_lines;
    bool m_interlaced;
    unsigned m_line_counter;
    unsigned m_current_line;
    unsigned m_counts_per_line;
    unsigned m_screen_line;
    unsigned m_hsync_counter;
    unsigned m_hsync_length_ms;
    unsigned m_counts_hsync;
    bool m_hsync_active;

protected:
    unsigned m_top_blank;
    unsigned m_bottom_blank;

    virtual void FRAME_SYNC() {};
    virtual void VSYNC(unsigned sync_val) {};
    virtual void HSYNC(unsigned line, unsigned sync_val) {};
public:
    RasterDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);
    void load_config(SystemData *sd) override;
    void clock(unsigned int counter) override;
};
