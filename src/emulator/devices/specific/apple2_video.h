// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple II / //e video (Правец-82/8М/8А/8С)

#pragma once

#include <atomic>

#include "emulator/core.h"
#include "emulator/devices/common/raster_display.h"
#include "apple2_iou.h"

class Apple2MMU;

// The picture is built as the machine builds it: a line of 560 dots out of the
// shift register (a 40 column byte gives 14 of them, an 80 column one 7), and
// a colour stage after it chosen as the monitor. The Правец came with a green
// monochrome monitor; colour needed an RGB card in slot 7, and the stock
// Bulgarian one took four dots of the shift register straight to R, G, B and
// intensity, which is why its colours were not Apple's. The card of СП
// «Вариант» passed the same four dots through a PROM to a CGA monitor
class Apple2Video: public RasterDisplay, public Apple2VideoSource
{
public:
    enum Monitor { MON_GREEN = 0, MON_WHITE, MON_AMBER, MON_RGB, MON_RGB_STOCK, MON_RGB_VARIANT, MON_COUNT };
    enum Charset { CS_IIE = 0, CS_II };

private:
    Apple2MMU * m_mmu = nullptr;
    Apple2IOU * m_iou = nullptr;
    ROM * m_font = nullptr;
    unsigned m_font_base = 0;   // the half of the character ROM jumper X20 picks
    Charset m_charset = CS_IIE;
    bool m_font_reverse = false;   // the leftmost dot in bit 6 (Правец-82)
    uint8_t m_reverse[128];
    bool m_iie = true;

    std::atomic<unsigned> m_monitor;
    unsigned m_line = 0;        // raster line the beam is on
    unsigned m_frames = 0;
    bool m_flash = false;

    uint32_t m_mono[MON_RGB][2];
    uint32_t m_rgb[16];         // the colours of the Apple
    uint32_t m_rgb_stock[16];   // what the stock RGB card makes of the same dots
    // The card of СП «Вариант»: its PROM, 32 colours IRGB (two tables of
    // 16, A4 the phase), and the CGA colours they stand for
    ROM * m_rgb_rom = nullptr;
    uint32_t m_cga[16];

    uint8_t m_dots[560 + 4];
    uint8_t m_late[40];         // a hires byte of this line shifted half a dot

    void build_line(unsigned line, unsigned flags);
    unsigned glyph(unsigned code, unsigned row, bool altchar) const;
    void render_line(unsigned line);
    void render_line_unlocked(unsigned line);
    void render_all(bool force_render) override;

public:
    Apple2Video(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void set_renderer(VideoRenderer &vr) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    bool native_frame_rate(uint64_t &num, uint64_t &den) override { return raster_frame_rate(num, den, 1); }

    void HSYNC(const unsigned line, const unsigned sync_val) override;
    bool in_vbl() const override { return m_line >= 192; }

    ConfigFields get_config_fields() override;
    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_apple2_video(InterfaceManager *im, EmulatorConfigDevice *cd);
