// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

class Agat9Display : public RasterDisplay
{
protected:
    unsigned int mode;
    unsigned int previous_mode;
    unsigned int base_address;
    unsigned int page_size;
    bool blinker;
    unsigned int clock_counter;
    unsigned int blink_ticks;

    Port * m_port_mode;
    RAM * m_memory;
    ROM * m_font;

    Interface i_50hz;
    Interface i_500hz;
    Interface i_ints_en;

    unsigned m_irq_val;
    unsigned m_nmi_val;
    unsigned m_512_mode;

    void render_line(unsigned screen_line);
    virtual void render_all(bool force_render) override;

    void set_mode(unsigned int new_mode);

public:
    Agat9Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;

    void clock(unsigned int counter) override;
    void load_config(SystemData *sd) override;
    void interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value) override;

    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    void VSYNC(const unsigned sync_val) override;
    void HSYNC(const unsigned line, const unsigned sync_val) override;
};

ComputerDevice * create_agat_9_display(InterfaceManager *im, EmulatorConfigDevice *cd);
