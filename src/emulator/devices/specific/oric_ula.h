// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Video of the ULA HCS10017 (Oric Atmos, Правец-8Д)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"

// 224 lines of 40 cells, a cell 6 dots: 240 x 224. The ULA has no registers:
// what it shows is chosen by "serial attributes", bytes of the picture itself
// with bits 6 and 5 at 0, each of which takes its cell (drawn as paper) and
// changes what follows it on the line:
//   $00-$07 ink, $08-$0F character set (bit 0 the alternate one, bit 1 double
//   height, bit 2 blinking), $10-$17 paper, $18-$1F mode (bit 2 HIRES,
//   bit 1 50 Hz)
// Bit 7 of any byte inverts its cell. Every line starts white on black with
// the standard set; the mode stays until another mode byte.
//   TEXT: the screen at $BB80, the characters at $B400 (alternate $B800)
//   HIRES: lines 0-199 from $A000, a byte six dots; lines 200-223 are text,
//   with the characters at $9800 (alternate $9C00)
// Both character sets are in the RAM: the ROM copies them there at start.
// The 50/60 Hz bit makes the frame 312 or 264 lines of 64 us.
class OricULA: public RasterDisplay
{
private:
    Memory * m_ram = nullptr;
    unsigned m_mode = 0x1A & 7;         // the mode attribute, power-up $1A
    unsigned m_line = 0;
    unsigned m_frames = 0;
    uint32_t m_colors[8];

    void render_line(unsigned line);
    // mode: the mode attribute the line starts with, left as the line ends
    void render_line_unlocked(unsigned line, unsigned &mode);
    void render_all(bool force_render) override;

public:
    OricULA(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void set_renderer(VideoRenderer &vr) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    bool native_frame_rate(uint64_t &num, uint64_t &den) override { return raster_frame_rate(num, den, 1); }

    void HSYNC(const unsigned line, const unsigned sync_val) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_oric_ula(InterfaceManager *im, EmulatorConfigDevice *cd);
