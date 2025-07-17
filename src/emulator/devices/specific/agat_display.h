// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

class AgatDisplay : public RasterDisplay
{
protected:
    unsigned int mode;
    unsigned int previous_mode;
    unsigned int module;
    unsigned int base_address;
    unsigned int page_size;
    bool blinker;
    unsigned int clock_counter;
    unsigned int system_clock;
    unsigned int blink_ticks;

    Port * port_mode;
    RAM * memory[2];
    ROM * font;

    Interface i_50hz;
    Interface i_500hz;
    Interface i_ints_en;

    unsigned m_irq_val;

    void render_byte(unsigned int address);
    void render_line(unsigned line);
    virtual void render_all(bool force_render) override;

    void set_mode(unsigned int new_mode);

public:
    AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    // virtual void set_surface(SURFACE * surface) override;
    void set_renderer(VideoRenderer &vr) override;

    // virtual void memory_callback(unsigned int callback_id, unsigned int address) override;

    virtual void clock(unsigned int counter) override;
    virtual void load_config(SystemData *sd) override;

    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    void VSYNC(unsigned sync_val) override;
    void HSYNC(unsigned line, unsigned sync_val) override;
};

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd);
