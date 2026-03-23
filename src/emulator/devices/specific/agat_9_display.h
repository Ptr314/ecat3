// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 display controller device

#pragma once

#include <atomic>

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"
#include "emulator/devices/common/register.h"

// https://agatcomp.ru/agat/Hardware/useful/ColorSet.shtml
// Standard palette
static const uint8_t Agat_9_base_colors[16][3]  = {
    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217},
    { 38,  38, 	38}, {255,  38,  38}, { 38, 255,  38}, {255, 255,  38},
    { 38,  38, 255}, {255,  38, 255}, { 38, 255, 255}, {255, 255, 255}
};

// Palette for a monochrome output
static const uint8_t Agat_9_gray_colors[16][3]  = {
    {  0,   0,   0}, {130, 130, 130}, { 89,  89,  89}, {221, 221, 221},
    { 65,  65,  65}, {194, 194, 194}, {151, 151, 151}, {241, 241, 241},
    { 39,  39, 	39}, {185, 185, 185}, {148, 148, 148}, {244, 244, 244},
    {108, 108, 108}, {229, 229, 229}, {197, 197, 197}, {255, 255, 255}
};

// Palette without an I bit
static const uint8_t Agat_9_8_colors[16][3]  = {
    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217},
    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217},
};

// Palette with an inverted I bit
static const uint8_t Agat_9_inverted_colors[16][3]  = {
    { 38,  38, 	38}, {255,  38,  38}, { 38, 255,  38}, {255, 255,  38},
    { 38,  38, 255}, {255,  38, 255}, { 38, 255, 255}, {255, 255, 255},
    {  0,   0,   0}, {217,   0,   0}, {  0, 217,   0}, {217, 217,   0},
    {  0,   0, 217}, {217,   0, 217}, {  0, 217, 217}, {217, 217, 217}
};

static const uint8_t Agat_9_ex_colors[16][3]  = {
    {  0,   0,   0}, {167,  87,  52}, {  0, 115,  16}, {242,  91,   0},
    {  0,   0, 218}, {202,  20, 255}, { 90, 212, 199}, {217, 217, 217},
    { 38,  38,  38}, {254,  39,  37}, { 37, 255,  37}, {255, 255,  37},
    { 35, 151, 254}, {255, 118, 170}, { 72, 252, 253}, {255, 255, 255}
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

static const uint8_t Agat_Apple_index[2][2] =  {
    // Hi   0   1
          { 5,  4},     // Even
          { 2,  1}      // Odd
};

#define APPLE_MODE_CALLBACK     1

class Agat9Display : public RasterDisplay
{
protected:
    unsigned mode;
    unsigned previous_mode;
    unsigned base_address;
    bool blinker;
    unsigned clock_counter;
    unsigned blink_ticks;

    Port * m_mode_agat;
    RAM * m_mode_apple;
    Register * m_pa;
    RAM * m_memory;
    ROM * m_font;
    RAM * m_pal;

    Interface i_50hz;
    Interface i_500hz;
    Interface i_ints_en;

    unsigned m_irq_val;
    unsigned m_nmi_val;
    unsigned m_512_mode;
    unsigned m_a2_text;
    unsigned m_a2_mixed;
    unsigned m_a2_page;

    unsigned _memory_bank;

    std::atomic<unsigned> m_color_mode;

    uint32_t Agat_RGBA16[16];
    uint32_t Agat_RGBA16gray[16];
    uint32_t Agat_RGBA16i[16];
    uint32_t Agat_RGBA16_8[16];
    uint32_t Agat_RGBA16ex[16];

    void render_line(unsigned screen_line);
    virtual void render_all(bool force_render) override;

    void set_mode(unsigned int new_mode);

public:
    Agat9Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;

    void clock(unsigned int counter) override;
    void load_config(SystemData *sd) override;
    void interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    void memory_callback(unsigned int callback_id, unsigned int address) override;

    void VSYNC(const unsigned sync_val) override;
    void HSYNC(const unsigned line, const unsigned sync_val) override;
};

ComputerDevice * create_agat_9_display(InterfaceManager *im, EmulatorConfigDevice *cd);
