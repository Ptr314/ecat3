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
// Palettes 6-10 use two intermediate levels of the red component only (C0 and
// 8E), green and blue stay full or off. BKBTL and the Chipwiki table agree on
// that (Chipwiki has 90 for 8E), the gid emulator has the same structure with
// darker levels. Web colour names (DarkRed, BlueViolet...) are not the machine.
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

// Raster of К1801ВП1-037, as reconstructed from the chip (github.com/1801BM1/k1801,
// 037/rtl/va_037.v; the БК emulator by gid follows the same counters): 256
// picture lines, then 64 service ones counted down from 077. The scroll
// register is loaded into the line address while the counter passes 051 and
// 050, the 22nd and 23rd service lines (0-based).
static const unsigned BK_SCROLL_LATCH_LINE = 23;

BKDisplay::BKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    RasterDisplay(im, cd)
    , i_frame(this, im, 1, "frame", MODE_W)
    , m_scroll_base(0330)
    , m_scroll(0330)
    , m_offset(0)
    , m_line_bytes(64)
    , m_video_lines(256)
    , m_color(true)
    , m_mode_pending(false)
    , m_pending_color(true)
{
    m_standart = "vp1-037";
    // Video memory holds 256 lines of 64 bytes. Read as one bit per pixel that
    // is 512 dots across, read as two bits per pixel it is 256.
    sy = m_video_lines;
    sx = m_color? 256 : 512;
}

emulator::Result BKDisplay::load_config(SystemData *sd)
{
    emulator::Result res = RasterDisplay::load_config(sd);
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
    m_video_lines = read_confg_value(cd, "lines", false, (unsigned int)256);

    // Colour or monochrome is a switch on the case of a real БК rather than a
    // register, so it starts from the configuration and is changed from the
    // toolbar afterwards.
    m_color = str_tolower(read_confg_value(cd, "mode", false, std::string("color"))) != "mono";

    sy = m_video_lines;
    sx = m_color? (m_line_bytes * 4) : (m_line_bytes * 8);

    latch_scroll();

    // The pulse line is wired by now: settle it low, so the first frame is
    // already an edge for whatever takes the interrupt from it
    i_frame.change(0);

    return emulator::Result::ok();
}

void BKDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    // The switch is applied here because the render thread asks for the size
    // before every frame. Changing the mode straight from the interface thread
    // could land in the middle of a frame, leaving the width and the buffer
    // describing different resolutions.
    if (m_mode_pending) {
        // Under the surface lock, so the switch cannot land inside a line
        // that the emulation thread is drawing
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
            BK_OPTION_COLORS, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Video output"), "kscreensaver.png",
            {
                {BK_COLOR_ON,  QT_TRANSLATE_NOOP("DeviceOptions", "RGB")},
                {BK_COLOR_OFF, QT_TRANSLATE_NOOP("DeviceOptions", "Mono")}
            },
            // "mode = mono" of the config starts on the second entry; a switch
            // not yet taken over by the render thread already counts
            static_cast<unsigned>((m_mode_pending ? m_pending_color : m_color) ? BK_COLOR_ON : BK_COLOR_OFF)
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

void BKDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    for (unsigned i = 0; i < 16; i++)
        vr.FillRGB(BK_Palettes[i], BK_RGBA4[i], 4);
    vr.FillRGB(BK_Mono_2, BK_RGBA2, 2);
}

// Bits 8-11 hold the palette number, bit 15 picks the video page
unsigned BKDisplay::control() const
{
    return (port_control != nullptr) ? port_control->get_direct(0) : 0;
}

// Bit 9 cleared leaves only the top quarter of the screen (64 lines) on the air.
// The controller looks at it on every word, not once a frame.
bool BKDisplay::quarter() const
{
    return (port_scroll != nullptr) && (port_scroll->get_direct(0) & 0x200) == 0;
}

void BKDisplay::latch_scroll()
{
    if (port_scroll == nullptr) return;
    m_scroll = port_scroll->get_direct(0);
    // Only the low byte of the register matters: it names the video line
    // shown at the top of the screen, counted from the base value. The
    // firmware keeps adding to the register without masking it, so the
    // upper bits are just an unused carry and the subtraction below has
    // to come before the wrap.
    m_offset = (m_scroll - m_scroll_base) % m_video_lines;
}

// The chip has no frame output of its own: the board takes the frame interrupt
// from the vertical sync pulse inside its composite sync (pin 28), SYNC2 of the
// reconstruction, active while the service line counter is 052..050 - the
// 21st to 23rd service lines. Its start is the interrupt, 43 lines before the
// first picture line.
static const unsigned BK_FRAME_PULSE_LINE = 21;

void BKDisplay::HSYNC(const unsigned line, const unsigned sync_val)
{
    if (sync_val == 0) {
        if (line == m_video_lines) {
            for (unsigned i = 0; i < m_video_lines && i < 256; i++)
                m_frame_palette[i] = m_line_palette[i];
        }
        if (line == m_video_lines + BK_FRAME_PULSE_LINE) {
            // Vertical sync: the frame interrupt, gated by bit 14 of 0177662
            // outside the device
            m_frames++;
            i_frame.change(1);
            i_frame.change(0);
        }
        if (line == m_video_lines + BK_SCROLL_LATCH_LINE) {
            latch_scroll();
        }
    } else if (line < m_video_lines) {
        render_line(line);
    }
}

void BKDisplay::reset(const bool cold)
{
    RasterDisplay::reset(cold);
    // The ports are reset before the display, so the frame starts from the
    // scroll register the machine really has rather than from the one taken
    // before the reset
    latch_scroll();
}

// The picture is laid out line by line from the emulation thread. Only a
// surface that has never been drawn - the first frame, a colour/mono switch -
// is filled here at once, so that it shows even while the processor stands.
// A forced repaint is ignored: the main window asks for one on every paint
// event, and a whole frame drawn from the palette of that moment replaced a
// frame split by a mid-frame palette change with a single colour (the INSULT
// demo flickered). validate() already holds the surface lock.
void BKDisplay::render_all(MAYBE_UNUSED const bool force_render)
{
    if (!screen_valid)
        for (unsigned line = 0; line < m_video_lines; line++)
            render_line_unlocked(line);
    screen_valid = true;
    was_updated = true;
}

void BKDisplay::render_line(const unsigned line)
{
    compat_lock_guard guard(m_surface_mutex);
    render_line_unlocked(line);
}

void BKDisplay::render_line_unlocked(const unsigned line)
{
    if (!has_valid_renderer() || render_pixels == nullptr) return;

    // The width is checked against the surface that is actually there: a mode
    // switch reaches the surface only on the next frame of the render thread
    const bool color = m_color;
    const unsigned width = color? (m_line_bytes * 4) : (m_line_bytes * 8);
    if ((int)(width * 4) > line_bytes) return;

    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;

    const unsigned ctl = control();
    RAM * vmem = vram[(vram[1] != nullptr) ? ((ctl >> 15) & 1) : 0];

    const bool blank = vmem == nullptr || (quarter() && line >= m_video_lines / 4);
    if (line < 256) m_line_palette[line] = blank ? 16 : static_cast<uint8_t>((ctl >> 8) & 0x0F);

    if (blank) {
        render_line_blank(base, width);
    } else {
        // The quarter mode does not change the addressing: the firmware itself
        // points the scroll register at the last quarter of the video memory
        // (0070000-0077777), which is what the manuals describe as the extended
        // memory mode
        const unsigned src = ((line + m_offset) % m_video_lines) * m_line_bytes;
        if (color) render_line_color(base, src, vmem, (ctl >> 8) & 0x0F);
        else render_line_mono(base, src, vmem);
    }

    if (line + 1 == m_video_lines) was_updated = true;
}

// Below the quarter screen the beam draws nothing
void BKDisplay::render_line_blank(uint8_t * base, const unsigned width) const
{
    for (unsigned i = 0; i < width; i++)
        *reinterpret_cast<uint32_t*>(base + i * 4) = BK_RGBA2[0];
}

// The video memory is read straight from its buffer: a virtual get_direct() on
// every byte of every line, 50 times a second, costs more than the drawing
void BKDisplay::render_line_mono(uint8_t * base, const unsigned src, RAM * vmem) const
{
    const uint8_t * video = vmem->get_buffer() + src;
    for (unsigned i = 0; i < m_line_bytes; i++) {
        const uint8_t b = video[i];
        // Bit 0 is the leftmost dot
        for (unsigned k = 0; k < 8; k++)
            *reinterpret_cast<uint32_t*>(base + (i * 8 + k) * 4) = BK_RGBA2[(b >> k) & 1];
    }
}

void BKDisplay::render_line_color(uint8_t * base, const unsigned src, RAM * vmem, const unsigned palette) const
{
    const uint32_t * colors = BK_RGBA4[palette];
    const uint8_t * video = vmem->get_buffer() + src;

    for (unsigned i = 0; i < m_line_bytes; i++) {
        const uint8_t b = video[i];
        // The lowest pair of bits is the leftmost dot
        for (unsigned k = 0; k < 4; k++)
            *reinterpret_cast<uint32_t*>(base + (i * 4 + k) * 4) = colors[(b >> (k * 2)) & 3];
    }
}

ConfigFields BKDisplay::get_config_fields()
{
    ConfigField f;
    f.name = "mode";
    f.title = QT_TRANSLATE_NOOP("DeviceOptions", "Video output");
    f.type = CONFIG_FIELD_CHOICE;
    f.def = "color";
    f.values.push_back({"color", QT_TRANSLATE_NOOP("DeviceOptions", "RGB")});
    f.values.push_back({"mono",  QT_TRANSLATE_NOOP("DeviceOptions", "Mono")});
    return {f};
}

void BKDisplay::save_state(StateWriter &w)
{
    RasterDisplay::save_state(w);
    w.u("scroll", m_scroll);
    w.n("offset", m_offset);
    w.n("frames", m_frames);
    w.b("color", m_color);
    w.b("pending_color", m_pending_color);
    //The palette each line of the picture was drawn with. A snapshot taken
    //mid frame is finished from the line it stopped on, and a program that
    //changes the palette per line (every БК demo) keeps the lines above it
    w.hex("line_palette", m_line_palette, sizeof(m_line_palette));
    w.hex("frame_palette", m_frame_palette, sizeof(m_frame_palette));
}

emulator::Result BKDisplay::load_state(const StateReader &r)
{
    emulator::Result res = RasterDisplay::load_state(r);
    if (!res) return res;
    r.u("scroll", m_scroll);
    r.u("offset", m_offset);
    r.u("frames", m_frames);
    r.b("color", m_color);
    r.b("pending_color", m_pending_color);
    r.hex("line_palette", m_line_palette, sizeof(m_line_palette));
    r.hex("frame_palette", m_frame_palette, sizeof(m_frame_palette));
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> BKDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = RasterDisplay::get_device_fields();
    r.push_back({"scroll",  "Scroll register 0177664 as taken for this frame", false});
    r.push_back({"offset",  "First video line shown at the top of the screen", false});
    r.push_back({"quarter", "1 when only the top quarter of the screen is shown", false});
    r.push_back({"control", "Palette and page register 0177662",               false});
    r.push_back({"palette", "Palette number, 0 without the register",          false});
    r.push_back({"page",    "Video RAM shown, 0 on a machine with only one",   false});
    r.push_back({"vram",    "Name of the video RAM device being shown",        false});
    r.push_back({"color",   "1 for colour, 0 for the monochrome mode",         false});
    r.push_back({"palettes", "Palette each line of the last completed frame was drawn with, 16 for a blank line", true});
    r.push_back({"frames",   "Frame pulses given since the start",             false});
    return r;
}

bool BKDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    // The whole last frame, taken when its picture lines ended, so that a
    // mid-frame palette change is seen as it was drawn
    if (field == "palettes")
    {
        const unsigned last = m_video_lines - 1;
        if (to < from) to = from;
        if (from > last) from = last;
        if (to > last) to = last;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        for (unsigned int line = from; line <= to; line++)
            out.values.push_back(m_frame_palette[line]);
        return true;
    }

    if (field == "frames")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_frames & 0xFFFF);
        return true;
    }

    const unsigned ctl = control();
    const unsigned page = (vram[1] != nullptr) ? ((ctl >> 15) & 1) : 0;

    if (field == "scroll" || field == "control")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back((field == "scroll") ? m_scroll : ctl);
        return true;
    }

    if (field == "offset" || field == "palette" || field == "page")
    {
        out.numeric = true;
        if (field == "offset")       out.values.push_back(m_offset);
        else if (field == "palette") out.values.push_back((ctl >> 8) & 0x0F);
        else                         out.values.push_back(page);
        return true;
    }

    if (field == "quarter" || field == "color")
    {
        out.numeric = true;
        out.values.push_back(((field == "quarter") ? quarter() : m_color) ? 1 : 0);
        return true;
    }

    if (field == "vram")
    {
        out.numeric = false;
        out.text = (vram[page] != nullptr) ? vram[page]->name : std::string("-");
        return true;
    }

    return RasterDisplay::get_field(field, from, to, out);
}

ComputerDevice * create_bk_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new BKDisplay(im, cd);
}
