// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-7 display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

class Agat7Display : public RasterDisplay
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

    bool m_color_options = true;
    bool m_pal_card = false;
    bool m_pal_card_out = false;
    bool m_pal_builtin = false;

    RAM * m_pal_mem;
    PortAddress * m_pal_switch;
    PortAddress * m_pal_mode;
    RAM * m_pal_font;

    std::atomic<unsigned> m_color_mode;

    uint32_t Agat_RGBA16_palcard[16];
    uint32_t Agat_RGBA16_palcard_std[8][16];

    void render_line(unsigned screen_line);
    void render_all(bool force_render) override;

    void set_mode(unsigned int new_mode);

    uint32_t convert_rgba(unsigned c, const uint32_t rgba[]) const;
    uint8_t convert_font(unsigned chr, unsigned line) const;

public:
    Agat7Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    void clock(unsigned int counter) override;
    void load_config(SystemData *sd) override;
    void interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    void VSYNC(unsigned sync_val) override;
    void HSYNC(unsigned line, unsigned sync_val) override;
};

ComputerDevice * create_agat_7_display(InterfaceManager *im, EmulatorConfigDevice *cd);
