// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

static const uint8_t Agat_9_base_colors[16][3]  = {
    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217},
    { 38,  38, 	38}, {255,  38,  38}, { 38, 255,  38}, {255, 255,  38},
    { 38,  38, 255}, {255,  38, 255}, { 38, 255, 255}, {255, 255, 255}
};

static const uint8_t Agat_4_index[4][4]  = {
    // 0    1   2   3
    { 0,  1,  2,  4},    // Palette 1
    {15,  1,  2,  4},    // Palette 2
    { 0,  0,  2,  4},    // Palette 3
    { 0,  1,  0,  4}     // Palette 4
};

static const uint8_t Agat_2_index[4][2] =  {
    //  0   1
    { 0, 15},     // Palette 1
    {15,  0},     // Palette 2
    { 0,  2},     // Palette 3
    { 2,  0}      // Palette 4
};

class Agat9Display : public RasterDisplay
{
protected:
    unsigned mode;
    unsigned previous_mode;
    unsigned base_address;
    bool blinker;
    unsigned clock_counter;
    unsigned blink_ticks;

    Port * m_port_mode;
    RAM * m_memory;
    ROM * m_font;
    Port * m_pal1;
    Port * m_pal2;

    Interface i_50hz;
    Interface i_500hz;
    Interface i_ints_en;

    unsigned m_irq_val;
    unsigned m_nmi_val;
    unsigned m_512_mode;

    unsigned _memory_bank;

    uint32_t Agat_RGBA16[16];

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
