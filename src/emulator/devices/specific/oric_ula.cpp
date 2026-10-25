// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Video of the ULA HCS10017 (Oric Atmos, Правец-8Д)

#include "oric_ula.h"
#include "emulator/utils.h"

#define MODE_HIRES  4
#define MODE_50HZ   2
#define ATTR_ALT    1
#define ATTR_DOUBLE 2
#define ATTR_BLINK  4

// Bit 0 red, 1 green, 2 blue
static const uint8_t ORIC_COLORS[8][3] = {
    {  0,   0,   0}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
    {  0,   0, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255}
};

OricULA::OricULA(InterfaceManager *im, EmulatorConfigDevice *cd):
    RasterDisplay(im, cd)
{
    m_clocked = true;
    sx = 240;
    sy = 224;
}

emulator::Result OricULA::load_config(SystemData *sd)
{
    m_standart = "oric";
    emulator::Result res = RasterDisplay::load_config(sd);
    if (!res) return res;
    m_ram = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    if (m_ram == nullptr || m_ram->get_size() < 0x10000)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{OricULA|" + std::string(QT_TRANSLATE_NOOP("OricULA", "A RAM of 64K is expected")) + "} " + name);
    return emulator::Result::ok();
}

// The ULA has no reset input: the mode survives the RESET key
void OricULA::reset(bool cold)
{
    RasterDisplay::reset(cold);
    if (cold) {
        m_mode = 0x1A & 7;
        m_frames = 0;
    }
}

void OricULA::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(ORIC_COLORS, m_colors, 8);
}

void OricULA::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void OricULA::render_line(unsigned line)
{
    compat_lock_guard guard(m_surface_mutex);
    render_line_unlocked(line, m_mode);
    screen_valid = true;
}

// One line as the ULA fetches it: a byte a cell, the attributes taking effect
// from the cell after them. The mode is the ULA's own state, the colours and
// the character set start over on every line
void OricULA::render_line_unlocked(unsigned line, unsigned &mode)
{
    const uint8_t * mem = m_ram->get_buffer();
    unsigned ink = 7, paper = 0, attr = 0;
    // Blinking dots are paper half of the time: 16 frames shown, 16 not
    const bool blink_off = (m_frames & 0x10) == 0;
    const bool draw = has_valid_renderer() && render_pixels != nullptr && line < sy;
    uint32_t * out = draw ? reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(render_pixels) + line * line_bytes) : nullptr;
    const unsigned text_row = 0xBB80 + (line >> 3) * 40;

    for (unsigned x = 0; x < 40; x++) {
        const bool hires = (mode & MODE_HIRES) && line < 200;
        const unsigned c = hires ? mem[0xA000 + line * 40 + x] : mem[text_row + x];
        unsigned dots = 0;
        if ((c & 0x60) == 0) {
            switch (c & 0x18) {
            case 0x00: ink = c & 7; break;
            case 0x08: attr = c & 7; break;
            case 0x10: paper = c & 7; break;
            case 0x18: mode = c & 7; break;
            }
        } else if (hires) {
            dots = c & 0x3F;
        } else {
            const unsigned row = ((attr & ATTR_DOUBLE) ? (line >> 1) : line) & 7;
            unsigned base = (mode & MODE_HIRES) ? 0x9800 : 0xB400;
            if (attr & ATTR_ALT) base += 0x400;
            dots = mem[base + ((c & 0x7F) << 3) + row] & 0x3F;
        }
        if ((attr & ATTR_BLINK) && blink_off) dots = 0;
        if (out == nullptr) continue;
        const unsigned inv = (c & 0x80) ? 7 : 0;
        const uint32_t fg = m_colors[ink ^ inv], bg = m_colors[paper ^ inv];
        uint32_t * p = out + x * 6;
        for (unsigned k = 0; k < 6; k++) p[k] = (dots & (0x20 >> k)) ? fg : bg;
    }
}

// The surface is wiped: draw the frame again from the memory. The mode the
// lines would take on the way is the one now, so a split screen redrawn here
// is approximate until the next frame. This runs on the render thread: the
// mode it walks is a copy, m_mode is the emulation thread's
void OricULA::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid && has_valid_renderer() && render_pixels != nullptr) {
        unsigned mode = m_mode;
        for (unsigned line = 0; line < sy; line++) render_line_unlocked(line, mode);
    }
    screen_valid = true;
    was_updated = true;
}

void OricULA::HSYNC(const unsigned line, const unsigned sync_val)
{
    if (sync_val == 0) {
        m_line = line;
        if (line == 0) {
            m_frames++;
            // The frame the 50/60 Hz bit asks for, taken at its start
            set_frame_lines((m_mode & MODE_50HZ) ? 312 : 264, 224);
        }
        return;
    }
    if (sync_val == 1 && line < 224) {
        render_line(line);
        //The window shows a frame only when this is set
        if (line == 223) { was_updated = true; frame_complete(); }
    }
}

std::vector<DeviceFieldInfo> OricULA::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = RasterDisplay::get_device_fields();
    r.push_back({"mode",   "The mode attribute: bit 2 HIRES, bit 1 50 Hz",  false});
    r.push_back({"frames", "Frames shown since power-up",                   false});
    return r;
}

bool OricULA::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "mode" || field == "frames") {
        out.numeric = true;
        out.values.push_back(field == "mode" ? m_mode : m_frames);
        return true;
    }
    return RasterDisplay::get_field(field, from, to, out);
}

void OricULA::save_state(StateWriter &w)
{
    RasterDisplay::save_state(w);
    w.n("mode", m_mode);
    w.n("line", m_line);
    w.n("frames", m_frames);
    w.n("lines", m_lines);
}

emulator::Result OricULA::load_state(const StateReader &r)
{
    emulator::Result res = RasterDisplay::load_state(r);
    if (!res) return res;
    r.u("mode", m_mode);
    r.u("line", m_line);
    r.u("frames", m_frames);
    // The length the frame took at its start, not what the mode says now
    unsigned lines = (m_mode & MODE_50HZ) ? 312 : 264;
    r.u("lines", lines);
    set_frame_lines(lines, 224);
    return emulator::Result::ok();
}

ComputerDevice * create_oric_ula(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new OricULA(im, cd);
}
