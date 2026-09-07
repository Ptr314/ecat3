// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common i8275(КР580ВГ75)-based display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/i8275.h"

class I8257;

//The tallest screen the 8275 can be programmed for is 64 character rows
#define I8275D_MAX_ROWS 64

class I8275Display: public GenericDisplay, public I8275RowSink
{
private:
    //One character row as the controller delivered it. Besides the bytes DMA
    //fetched, it carries everything outside them the row's appearance depends
    //on, sampled at the moment the beam reached the row: the font bank on the
    //~high line (the Apogey drives it from the CPU's INTE output and can change
    //it inside a frame), the cursor and the two blink phases. The pixels are
    //then drawn on the render thread, which is the only thread allowed to touch
    //the surface, and the picture still comes out as it looked row by row
    struct RowSnapshot {
        uint8_t         data[I8275_MAX_ROW];
        unsigned int    len;
        unsigned int    font_bank;
        unsigned int    cursor_col;
        unsigned int    cursor_row;
        unsigned int    cursor_mode;        //RegMode[3] & 0x30
        unsigned int    add_mode;           //Font line shift, RegMode[3] >> 7
        bool            transparent;        //Field attributes take no position
        bool            blink;              //Cursor blink phase
        bool            blink_char;         //Character blink phase
    };

    RAM * Memory;
    ROM * Font;
    I8275 * VG75;
    I8257 * DMA;
    unsigned int Channel;
    Interface i_high;             //Font select
    bool AttrDelay;
    unsigned int RGB[3];
    bool RGBInv;
    //The eight colours in the pixel format of the renderer
    uint32_t rgba_colors[8];

    //Field attribute state. It carries from row to row down the screen, so it
    //lives here and is reset once per frame rather than once per row
    bool FAReverse, FAUnder, FABlink;
    unsigned int FAColor;
    uint8_t NextAttr;

    RowSnapshot m_rows[I8275D_MAX_ROWS];
    bool m_have_frame;

    //The geometry the finished frame was fetched with. The controller runs on
    //the emulation thread and may be reprogrammed at any moment, so the render
    //thread must never read its registers while laying a frame out - it would
    //draw one row with the width of one raster and the height of another
    unsigned int m_frame_cpl;
    unsigned int m_frame_lps;
    unsigned int m_frame_h;

    //Height of the surface set_renderer() was handed. The renderer is resized
    //on the interface thread, and drawing must stop at the buffer it built
    unsigned int m_surface_sy;

    void set_attr(uint8_t v);
    void reset_attr();
    void clear_rows();
    bool follow_resolution();
    void clear_surface();

public:
    I8275Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;
    emulator::Result load_config(SystemData *sd) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    void reset(bool cold) override;

    //--------------------- I8275RowSink, on the emulation thread -------------
    void row_complete(unsigned int row, const uint8_t * data, unsigned int count) override;
    void frame_complete() override;
    void display_blanked() override;

protected:
    void render_all(bool force_render = false) override;
    virtual void draw_row(unsigned int row);
};

ComputerDevice * create_i8275display(InterfaceManager *im, EmulatorConfigDevice *cd);
