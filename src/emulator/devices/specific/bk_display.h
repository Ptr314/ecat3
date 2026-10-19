// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК display controller device

#pragma once

#include <vector>

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

#define BK_OPTION_COLORS    1
#define BK_COLOR_ON         0
#define BK_COLOR_OFF        1

// К1801ВП1-037 draws the picture along the raster: every line is laid out when
// the beam passes it, with the palette, the video page and the quarter screen
// bit as they are at that moment, the way Agat displays do it. The scroll
// register is taken once a frame, in the service lines. In the high precision
// mode a line is laid out a byte (4 colour dots) at a time as the beam passes
// it, for the demos that change the palette within a line.
class BKDisplay: public RasterDisplay
{
private:
    RAM * vram[2]{};
    Port * port_scroll{};
    Port * port_control{};
    CPU * m_cpu{};                  // tells when inside an instruction a register was written

    Interface i_frame;              // frame interrupt pulse, the 1st service line

    unsigned m_scroll_base;         // scroll register value that means "not scrolled"
    unsigned m_scroll;              // scroll register as taken for the current frame
    unsigned m_offset;              // first video line shown at the top
    unsigned m_line_bytes;          // bytes of video memory per screen line
    unsigned m_video_lines;         // picture lines
    bool m_color;

    // Palette each screen line was drawn with, for scripts: 16 marks a blank line.
    // The second array is the last frame completed, taken when the picture ends.
    uint8_t m_line_palette[256]{};
    uint8_t m_frame_palette[256]{};
    unsigned m_frames = 0;          // frame pulses given since the start

    // A mode change requested from the interface thread, applied at the end of
    // the picture by the emulation thread, or by the render one if that comes
    // first (a stopped machine draws no frames)
    volatile bool m_mode_pending = false;
    bool m_pending_color = true;
    void apply_mode_switch();                   // under the surface lock

    unsigned control() const;
    bool quarter() const;
    void latch_scroll();

    void render_line(unsigned line);            // takes the surface lock
    void render_line_unlocked(unsigned line);   // for callers already holding it
    // Bytes [from, to) of a line of video memory, from the line's start in
    // video and to out, the first dot of the line
    void render_bytes(uint32_t * out, const uint8_t * video, unsigned from, unsigned to,
                      bool color, bool blank, unsigned palette) const;

    // The high precision mode: the line the beam is on is laid out into a
    // buffer byte by byte as the beam passes, and goes onto the surface whole
    // when the next one starts
    static const unsigned NO_LINE = ~0u;
    std::vector<uint32_t> m_beam_buf;
    unsigned m_beam_line = NO_LINE;             // line in the buffer
    unsigned m_beam_byte = 0;                   // its bytes already laid out
    bool m_beam_color = true;                   // its mode, as at its first byte
    unsigned m_beam_ctl = 0;                    // the control register at the last BEAM()
    unsigned m_beam_at_line = NO_LINE;          // the raster line and clock of that call
    unsigned m_beam_at = 0;
    unsigned beam_byte(unsigned clocks) const;
    void beam_draw(unsigned to, unsigned ctl);  // with the control register ctl
    void beam_commit();

protected:
    void render_all(bool force_render) override;
    void HSYNC(unsigned line, unsigned sync_val) override;
    void BEAM(unsigned line, unsigned clocks) override;

public:
    BKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    ConfigFields get_config_fields() override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    bool native_frame_rate(uint64_t &num, uint64_t &den) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_bk_display(InterfaceManager *im, EmulatorConfigDevice *cd);
