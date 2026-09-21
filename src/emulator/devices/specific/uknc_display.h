// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

#define UKNC_OPTION_COLORS  1
#define UKNC_COLORS_RGB     0
#define UKNC_COLORS_GRB     1
#define UKNC_COLORS_GRAY    2

// The video controller of the УК-НЦ walks a linked list of per-line descriptors
// («таблица строк») in plane 0 starting at address 0000270. Each line names the
// address of its own pixels, and four-word descriptors change the palette, the
// horizontal scale, the brightness and the cursor as the beam goes down the
// screen - so every line can have its own eight colours and its own width.
//
// A pixel is three bits, one from each plane, read from the same address in all
// three: plane 0 is the low bit, plane 2 the high one, and bit 0 of every byte
// is the leftmost dot of its octet.
//
// The list is walked one entry per line, the way the hardware does it, rather
// than all at once: a program that rewrites the table mid-frame is then seen
// exactly as the beam saw it.
class UKNCDisplay: public RasterDisplay
{
private:
    RAM * m_plane[3]{};

    // Frame pulse at the top of every frame: the EVNT interrupt (vector 100)
    // of both processors comes from it, so it keeps step with the beam
    Interface i_frame;

    // ---- state carried along the list while a frame is drawn ----
    unsigned int m_tag_address = 0;     // next descriptor
    bool     m_tag_four = false;        // the next descriptor is four words
    bool     m_tag_palette = false;     // ... and it is a palette one rather than params
    uint32_t m_palette = 0;             // eight colours, four bits each
    unsigned m_pbpgpr = 0;              // brightness modifier, the high nibble of a colour index
    unsigned m_scale = 1;               // 1, 2, 4 or 8 screen dots per pixel
    bool     m_cursor_on = false;       // toggled by bit 0 of a link word
    unsigned m_cursor_color = 0;        // YRGB of the cursor
    bool     m_cursor_graphic = false;  // one dot rather than the whole octet
    unsigned m_cursor_pos = 0;          // octet the cursor sits in
    unsigned m_cursor_bit = 0;          // dot inside that octet, for a graphic cursor

    // The eight colours of the current line, already as RGBA
    uint32_t m_line_colors[8]{};

    unsigned m_entries = 0;             // descriptors walked so far in this frame
    unsigned m_entries_done = 0;        // ... and in the last completed one
    unsigned m_frames = 0;

    // Scale each line of the last completed frame was drawn with, for scripts
    uint8_t m_line_scale[288]{};

    // A palette change requested from the interface thread, applied by the
    // render one - the same dance BKDisplay does for its colour switch
    volatile bool m_mode_pending = false;
    unsigned m_pending_colors = UKNC_COLORS_RGB;
    unsigned m_colors = UKNC_COLORS_RGB;

    unsigned int plane_word(unsigned int address) const;
    void reset_walk();                          // back to the top of the list
    void apply_four_word(unsigned int tag1, unsigned int tag2);
    void rebuild_line_colors();
    void next_line(unsigned int raster_line);   // one descriptor, and the line it draws

    void render_line(unsigned int y, unsigned int bits_address);
    void render_line_blank(unsigned int y);

protected:
    void render_all(bool force_render) override;
    void FRAME_SYNC() override;
    void HSYNC(unsigned line, unsigned sync_val) override;

public:
    UKNCDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    ConfigFields get_config_fields() override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_uknc_display(InterfaceManager *im, EmulatorConfigDevice *cd);
