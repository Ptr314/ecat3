// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК display controller device

#include <cstring>

#include "emulator/utils.h"
#include "bk_display.h"

// A pixel value of 0 is always the background, the rest select one of three
// colours. БК0010 has the single fixed palette below under number 0, БК0011М
// switches between all sixteen through the register 0177662.
uint8_t BK_Palettes[16][4][3] = {
    {{0,0,0}, {  0,  0,255}, {  0,255,  0}, {255,  0,  0}},     //  0 синий, зеленый, красный
    {{0,0,0}, {255,255,  0}, {255,  0,255}, {255,  0,  0}},     //  1 желтый, сиреневый, красный
    {{0,0,0}, {  0,255,255}, {  0,  0,255}, {255,  0,255}},     //  2 голубой, синий, сиреневый
    {{0,0,0}, {  0,255,  0}, {  0,255,255}, {255,255,  0}},     //  3 зеленый, голубой, желтый
    {{0,0,0}, {255,  0,255}, {  0,255,255}, {255,255,255}},     //  4 сиреневый, голубой, белый
    {{0,0,0}, {255,255,255}, {255,255,255}, {255,255,255}},     //  5 белый, белый, белый
    {{0,0,0}, {192,  0,  0}, {142,  0,  0}, {255,  0,  0}},     //  6 темно-красный, красно-коричневый, красный
    {{0,0,0}, {192,255,  0}, {142,255,  0}, {255,255,  0}},     //  7 салатовый, светло-зеленый, желтый
    {{0,0,0}, {192,  0,255}, {142,  0,255}, {255,  0,255}},     //  8 фиолетовый, фиолетово-синий, сиреневый
    {{0,0,0}, {142,255,  0}, {142,  0,255}, {142,  0,  0}},     //  9 светло-зеленый, фиолетово-синий, красно-коричневый
    {{0,0,0}, {192,255,  0}, {192,  0,255}, {192,  0,  0}},     // 10 салатовый, фиолетовый, темно-красный
    {{0,0,0}, {  0,255,255}, {255,255,  0}, {255,  0,  0}},     // 11 голубой, желтый, красный
    {{0,0,0}, {255,  0,  0}, {  0,255,  0}, {  0,255,255}},     // 12 красный, зеленый, голубой
    {{0,0,0}, {  0,255,255}, {255,255,  0}, {255,255,255}},     // 13 голубой, желтый, белый
    {{0,0,0}, {255,255,  0}, {  0,255,  0}, {255,255,255}},     // 14 желтый, зеленый, белый
    {{0,0,0}, {  0,255,255}, {  0,255,  0}, {255,255,255}}      // 15 голубой, зеленый, белый
};

uint32_t BK_RGBA4[16][4];

// Monochrome output is the same memory read as one bit per pixel
uint8_t BK_Mono_2[2][3] = {
    {  0,   0,   0},    // Black
    {255, 255, 255}     // White
};

uint32_t BK_RGBA2[2];

BKDisplay::BKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd)
    , m_scroll_base(0330)
    , m_scroll(0330)
    , m_offset(0)
    , m_quarter(false)
    , m_line_bytes(64)
    , m_lines(256)
    , m_control(0)
    , m_palette(0)
    , m_page(0)
    , m_color(true)
    , m_mode_pending(false)
    , m_pending_color(true)
{
    // Video memory holds 256 lines of 64 bytes. Read as one bit per pixel that
    // is 512 dots across, read as two bits per pixel it is 256.
    sy = m_lines;
    sx = m_color? 256 : 512;
}

emulator::Result BKDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    vram[0] = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("vram").value));

    // The second video page exists on БК0011М only
    std::string vram2_name = read_confg_value(cd, "vram2", false, std::string(""));
    if (!vram2_name.empty())
        vram[1] = dynamic_cast<RAM*>(im->dm->get_device_by_name(vram2_name));

    // The scroll register is optional; without it the screen never scrolls
    std::string scroll_name = read_confg_value(cd, "scroll", false, std::string(""));
    if (!scroll_name.empty())
        port_scroll = dynamic_cast<Port*>(im->dm->get_device_by_name(scroll_name));

    // The palette and video page register, БК0011М only
    std::string control_name = read_confg_value(cd, "control", false, std::string(""));
    if (!control_name.empty())
        port_control = dynamic_cast<Port*>(im->dm->get_device_by_name(control_name));

    m_scroll_base = read_confg_value(cd, "scroll_base", false, (unsigned int)0330);
    m_line_bytes = read_confg_value(cd, "line_bytes", false, (unsigned int)64);
    m_lines = read_confg_value(cd, "lines", false, (unsigned int)256);

    // Colour or monochrome is a switch on the case of a real БК rather than a
    // register, so it starts from the configuration and is changed from the
    // toolbar afterwards.
    m_color = str_tolower(read_confg_value(cd, "mode", false, std::string("color"))) != "mono";

    sy = m_lines;
    sx = m_color? (m_line_bytes * 4) : (m_line_bytes * 8);

    // Each video page reports writes under its own id, so a write to the page
    // that is not on the screen does not cost a redraw
    vram[0]->set_memory_callback(this, 1, MODE_W);
    if (vram[1] != nullptr) vram[1]->set_memory_callback(this, 2, MODE_W);

    return emulator::Result::ok();
}

void BKDisplay::memory_callback(unsigned int callback_id, MAYBE_UNUSED unsigned int address)
{
    if (callback_id - 1 != m_page) return;

    screen_valid = false;
    was_updated = true;
}

void BKDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    // The switch is applied here because the render thread asks for the size
    // before every frame. Changing the mode straight from the interface thread
    // could land in the middle of a frame, leaving the width and the buffer
    // describing different resolutions.
    if (m_mode_pending) {
        // Under the surface lock, so the switch cannot land inside a frame
        // that another thread is drawing
        lock_surface();
        m_mode_pending = false;
        if (m_pending_color != m_color) {
            m_color = m_pending_color;
            this->sx = m_color? (m_line_bytes * 4) : (m_line_bytes * 8);
            screen_valid = false;
            was_updated = true;
        }
        unlock_surface();
    }

    *sx = this->sx;
    *sy = this->sy;
}

DeviceOptions BKDisplay::get_device_options()
{
    return {
        {
            BK_OPTION_COLORS, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Output type"), "kscreensaver.png",
            {
                {BK_COLOR_ON,  QT_TRANSLATE_NOOP("DeviceOptions", "Color")},
                {BK_COLOR_OFF, QT_TRANSLATE_NOOP("DeviceOptions", "Monochrome")}
            }
        }
    };
}

void BKDisplay::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id != BK_OPTION_COLORS) return;

    // Called from the interface thread, so only the request is recorded here;
    // get_screen_constraints() applies it on the render thread.
    m_pending_color = (value_id == BK_COLOR_ON);
    m_mode_pending = true;
}

void BKDisplay::clock(MAYBE_UNUSED unsigned int counter)
{
    if (port_scroll != nullptr) {
        unsigned scroll = port_scroll->get_direct(0);
        if (scroll != m_scroll) {
            m_scroll = scroll;
            // Only the low byte of the register matters: it names the video line
            // shown at the top of the screen, counted from the base value. The
            // firmware keeps adding to the register without masking it, so the
            // upper bits are just an unused carry and the subtraction below has
            // to come before the wrap.
            m_offset = (scroll - m_scroll_base) % m_lines;
            // Bit 9 cleared leaves only the top quarter of the screen (64 lines)
            // on the air; the rest of the raster stays dark
            m_quarter = (scroll & 0x200) == 0;
            screen_valid = false;
            was_updated = true;
        }
    }

    if (port_control != nullptr) {
        unsigned control = port_control->get_direct(0);
        if (control != m_control) {
            m_control = control;
            // Bits 8-11 hold the palette number, bit 15 picks the video page
            m_palette = (control >> 8) & 0x0F;
            m_page = (vram[1] != nullptr)? ((control >> 15) & 1) : 0;
            screen_valid = false;
            was_updated = true;
        }
    }
}

void BKDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    for (unsigned i = 0; i < 16; i++)
        vr.FillRGB(BK_Palettes[i], BK_RGBA4[i], 4);
    vr.FillRGB(BK_Mono_2, BK_RGBA2, 2);
}

void BKDisplay::render_all(const bool force_render)
{
    if (screen_valid && !force_render) return;

    // The mode is sampled once, so a switch arriving in the middle of a frame
    // cannot draw half of it at the other resolution, and the width is checked
    // against the surface that is actually there.
    const bool color = m_color;
    const unsigned width = color? (m_line_bytes * 4) : (m_line_bytes * 8);
    if ((int)(width * 4) > line_bytes) return;

    // The palette and the page can be switched from the emulation thread at any
    // moment, so a frame is drawn entirely from one snapshot of them
    const unsigned palette = m_palette;
    RAM * vmem = vram[m_page];
    if (vmem == nullptr) return;

    const unsigned shown = m_quarter? (m_lines / 4) : m_lines;
    for (unsigned line = 0; line < m_lines; line++) {
        if (line >= shown) render_line_blank(line, color);
        else if (color) render_line_color(line, vmem, palette);
        else render_line_mono(line, vmem);
    }

    screen_valid = true;
    was_updated = true;
}

// Below the quarter screen the beam draws nothing
void BKDisplay::render_line_blank(const unsigned line, const bool color) const
{
    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;
    const unsigned width = color? (m_line_bytes * 4) : (m_line_bytes * 8);
    for (unsigned i = 0; i < width; i++)
        *reinterpret_cast<uint32_t*>(base + i * 4) = BK_RGBA2[0];
}

// Video line shown at a screen line. The quarter mode does not change the
// addressing: the firmware itself points the scroll register at the last
// quarter of the video memory (0070000-0077777), which is what the manuals
// describe as the extended memory mode
unsigned BKDisplay::source_line(const unsigned line) const
{
    return (line + m_offset) % m_lines;
}

void BKDisplay::render_line_mono(const unsigned line, RAM * vmem) const
{
    const unsigned src = source_line(line) * m_line_bytes;
    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;

    for (unsigned i = 0; i < m_line_bytes; i++) {
        const uint8_t b = vmem->get_direct(src + i);
        // Bit 0 is the leftmost dot
        for (unsigned k = 0; k < 8; k++)
            *reinterpret_cast<uint32_t*>(base + (i * 8 + k) * 4) = BK_RGBA2[(b >> k) & 1];
    }
}

void BKDisplay::render_line_color(const unsigned line, RAM * vmem, const unsigned palette) const
{
    const unsigned src = source_line(line) * m_line_bytes;
    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;
    const uint32_t * colors = BK_RGBA4[palette];

    for (unsigned i = 0; i < m_line_bytes; i++) {
        const uint8_t b = vmem->get_direct(src + i);
        // The lowest pair of bits is the leftmost dot
        for (unsigned k = 0; k < 4; k++)
            *reinterpret_cast<uint32_t*>(base + (i * 4 + k) * 4) = colors[(b >> (k * 2)) & 3];
    }
}

std::vector<DeviceFieldInfo> BKDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"scroll",  "Scroll register 0177664 as last seen",            false});
    r.push_back({"offset",  "First video line shown at the top of the screen", false});
    r.push_back({"quarter", "1 when only the top quarter of the screen is shown", false});
    r.push_back({"control", "Palette and page register 0177662 as last seen",  false});
    r.push_back({"palette", "Palette number, 0 without the register",          false});
    r.push_back({"page",    "Video RAM shown, 0 on a machine with only one",   false});
    r.push_back({"vram",    "Name of the video RAM device being shown",        false});
    r.push_back({"color",   "1 for colour, 0 for the monochrome mode",         false});
    return r;
}

bool BKDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //These are written by the render thread and read here from the emulation
    //one. They are single words describing what is on screen right now, so a
    //value one frame old is exactly as useful as a perfectly synchronised one
    if (field == "scroll" || field == "control")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back((field == "scroll") ? m_scroll : m_control);
        return true;
    }

    if (field == "offset" || field == "palette" || field == "page")
    {
        out.numeric = true;
        if (field == "offset")       out.values.push_back(m_offset);
        else if (field == "palette") out.values.push_back(m_palette);
        else                         out.values.push_back(m_page);
        return true;
    }

    if (field == "quarter" || field == "color")
    {
        out.numeric = true;
        out.values.push_back(((field == "quarter") ? m_quarter : m_color) ? 1 : 0);
        return true;
    }

    if (field == "vram")
    {
        out.numeric = false;
        out.text = (vram[m_page] != nullptr) ? vram[m_page]->name : std::string("-");
        return true;
    }

    return GenericDisplay::get_field(field, from, to, out);
}

ComputerDevice * create_bk_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new BKDisplay(im, cd);
}
