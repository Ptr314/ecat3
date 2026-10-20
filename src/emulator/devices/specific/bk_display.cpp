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

// The black and white output is the same memory read as one bit per pixel
uint8_t BK_Mono_2[2][3] = {
    {  0,   0,   0},    // Black
    {255, 255, 255}     // White
};

uint32_t BK_RGBA2[2];

// The colour picture on a monochrome monitor: the brightness is the code of
// the dot, 0-3, in even steps, whatever the palette. This is the picture of
// the Mona Lisa intro in palette 4 (magenta 55, cyan AA, white FF). A sum of
// the palette's components fits it too, but the weights of a specialist's
// figures for pure colours made a picture too far from it.
uint8_t BK_Grey_4[4][3] = {
    {0x00, 0x00, 0x00},
    {0x55, 0x55, 0x55},
    {0xAA, 0xAA, 0xAA},
    {0xFF, 0xFF, 0xFF}
};

uint32_t BK_GREY4[4];

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
    , m_mode(BK_MODE_RGB)
    , m_mode_pending(false)
    , m_pending_mode(BK_MODE_RGB)
{
    m_standart = "vp1-037";
    // The frame interrupt passes the register D11 the processor's clock latches
    // (see BK_FRAME_PULSE_LINE)
    m_sync_end_delay = 1;
    // Video memory holds 256 lines of 64 bytes. Read as one bit per pixel that
    // is 512 dots across, read as two bits per pixel it is 256.
    sy = m_video_lines;
    sx = 256;
    m_precise_available = true;
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
    m_cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu", false));

    m_scroll_base = read_confg_value(cd, "scroll_base", false, (unsigned int)0330);
    m_line_bytes = read_confg_value(cd, "line_bytes", false, (unsigned int)64);
    m_video_lines = read_confg_value(cd, "lines", false, (unsigned int)256);

    // Colour or black and white is a switch on the case of a real БК rather
    // than a register, and the monitor is what the user has, so the mode
    // starts from the configuration and is changed from the toolbar
    // afterwards. "mono" is the old name of the black and white mode.
    const std::string mode = str_tolower(read_confg_value(cd, "mode", false, std::string("color")));
    if (mode == "bw" || mode == "mono") m_mode = BK_MODE_BW;
    else if (mode == "gray") m_mode = BK_MODE_MONO;
    else m_mode = BK_MODE_RGB;

    sy = m_video_lines;
    sx = mode_width(m_mode);
    // Wide enough for either mode: a mode switch may land mid line
    m_beam_buf.assign(m_line_bytes * 8, 0);

    latch_scroll();

    // The pulse line is wired by now: settle it low, so the first frame is
    // already an edge for whatever takes the interrupt from it
    i_frame.change(0);

    return emulator::Result::ok();
}

// A mode switch may change the width, so it waits for the end of the
// picture rather than landing in the middle of a frame. The emulation thread
// applies it there (HSYNC), and the render thread here, when it asks for the
// size: a stopped machine draws no frames, and a switch from the toolbar must
// still show. Applied by the render thread only, it left the width unchanged
// for as long as that thread had not come round - at the maximum speed longer
// than the WAIT of a script, which read the old resolution.
// Both under the surface lock, so the switch cannot land inside a line.
void BKDisplay::apply_mode_switch()
{
    if (!m_mode_pending) return;
    m_mode_pending = false;
    if (m_pending_mode == m_mode) return;
    m_mode = m_pending_mode;
    // The surface is replaced by the render thread; until then nothing is
    // drawn into the old one. Between RGB and Mono the size stays, and the
    // whole screen is only drawn again
    change_resolution(mode_width(m_mode), sy);
    screen_valid = false;
    was_updated = true;
}

void BKDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    if (m_mode_pending) {
        lock_surface();
        apply_mode_switch();
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
                {BK_MODE_RGB,  QT_TRANSLATE_NOOP("DeviceOptions", "RGB")},
                {BK_MODE_BW,   QT_TRANSLATE_NOOP("DeviceOptions", "B/W")},
                {BK_MODE_MONO, QT_TRANSLATE_NOOP("DeviceOptions", "Mono")}
            },
            // The mode of the config; a switch not yet taken over by the
            // render thread already counts
            m_mode_pending ? m_pending_mode : m_mode
        }
    };
}

void BKDisplay::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id != BK_OPTION_COLORS || value_id > BK_MODE_MONO) return;

    // May come from the interface thread (the page calls it directly), so only
    // the request is recorded here; apply_mode_switch() makes it
    m_pending_mode = value_id;
    m_mode_pending = true;
}

void BKDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    for (unsigned i = 0; i < 16; i++)
        vr.FillRGB(BK_Palettes[i], BK_RGBA4[i], 4);
    vr.FillRGB(BK_Grey_4, BK_GREY4, 4);
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
    // register holds bits 0-7 and 9 (the port's mask in the config), so a
    // carry out of the low byte is lost, as in ВП1-037. Bit 9, the full
    // screen, still rides along, so the subtraction below has to come
    // before the wrap.
    m_offset = (m_scroll - m_scroll_base) % m_video_lines;
}

// The frame interrupt of the БК0011М (sheet 1 of its schematic) is a detector
// of missing WTI strobes: WTI with CLC clears the counter D28 (К555ИЕ5), the
// rising edge of SYNCO (the 037's composite sync) sets its first stage and
// clocks it into D3:B (К555ТМ2). So the request rises at the end of the
// horizontal sync pulse of the second line without picture, the first service
// line (HSYNC(line, 2), 68 CLKIN in). Through a buffer it goes to the
// register D11 (К555ТМ9), latched by the processor's clock like every other
// interrupt input, and the ВМ1 samples it on the next edge: m_sync_end_delay.
// Not at the vertical sync pulse (SYNC2 of the reconstruction, the 21st to
// 23rd service lines), as it was taken before: the demo Babylona, timed on a
// real БК0011М, then had its palette program 20 lines too low. With the
// latch, its calibration screen and picture come right together on two of the
// three power-on phases, as its text says they should

static const unsigned BK_FRAME_PULSE_LINE = 1;

void BKDisplay::HSYNC(const unsigned line, const unsigned sync_val)
{
    if (sync_val == 0) {
        // The line before, if the beam laid it out: before the frame is
        // reported whole below
        beam_commit();
        if (line == m_video_lines) {
            for (unsigned i = 0; i < m_video_lines && i < 256; i++)
                m_frame_palette[i] = m_line_palette[i];
            // The last picture line is on the surface: the frame is whole
            frame_complete();
            if (m_mode_pending) {
                lock_surface();
                apply_mode_switch();
                unlock_surface();
            }
        }
        if (line == m_video_lines + BK_SCROLL_LATCH_LINE) {
            latch_scroll();
        }
    } else if (sync_val == 2) {
        if (line == m_video_lines + BK_FRAME_PULSE_LINE) {
            // The frame interrupt, gated by bit 14 of 0177662 outside the
            // device
            m_frames++;
            i_frame.change(1);
            i_frame.change(0);
        }
    } else if (line < m_video_lines && !beam_active()) {
        render_line(line);
    }
}

// The high precision mode. A line is 48 words of the video clock (see
// RasterDisplay, "vp1-037"). By va_037.v of the reconstruction it starts where
// the picture of the line before ends: TV51 at the last picture word steps the
// line counter. 16 words of blanking follow (HGATE, the horizontal sync in
// words 5-8 of them), then the 32 words
// of the picture, so the beam is past clocks * 48 * 2 / line_length() - 32
// bytes of it. A call comes after each processor instruction, and the devices
// do not see when inside it a write happened. The vm1 timing knows it in
// finish(): a register that changed takes effect from the DOUT of the last
// write of the instruction (CPU::last_write_clocks()). Without it, from the
// middle of the instruction, where MOV R,(R) strobes it when it runs 32 clocks
// (15.5 on the chip model). The middle is late when the next fetch waits for a
// window: MOV R,(R0) after MOV #n,R runs 37 clocks with DOUT where it was, and
// the palette columns of Babylona came a byte off the calibration screen
void BKDisplay::BEAM(const unsigned line, const unsigned clocks)
{
    // The register before the instruction just run, and where it began
    const unsigned before = (m_beam_at_line != NO_LINE) ? m_beam_ctl : control();
    // An instruction begun on the line before starts at a negative clock of
    // this one (beam_commit() has laid out its part there)
    int start = 0;
    if (line == m_beam_at_line && clocks >= m_beam_at) start = (int)m_beam_at;
    else if (m_beam_at_line != NO_LINE) start = (int)m_beam_at - (int)m_beam_len;
    m_beam_ctl = control();
    m_beam_at_line = line;
    m_beam_at = clocks;
    m_beam_len = line_length();

    if (line >= m_video_lines) return;
    if (line != m_beam_line) {
        m_beam_line = line;
        m_beam_byte = 0;
    }
    if (m_beam_byte >= m_line_bytes) return;
    if (before != m_beam_ctl) {
        const int w = (m_cpu != nullptr) ? m_cpu->last_write_clocks() : -1;
        const int at = (w >= 0 && start + w <= (int)clocks) ? start + w : (start + (int)clocks) / 2;
        if (at > 0) beam_draw(beam_byte((unsigned)at), before);
    }
    beam_draw(beam_byte(clocks), m_beam_ctl);
}

// Bytes of the picture the beam has passed: the first third of the line is
// blank, the picture begins 16 words after the line does
unsigned BKDisplay::beam_byte(const unsigned clocks) const
{
    const unsigned passed = clocks * m_line_bytes * 3 / (2 * line_length());
    if (passed <= m_line_bytes / 2) return 0;
    const unsigned to = passed - m_line_bytes / 2;
    return (to > m_line_bytes) ? m_line_bytes : to;
}

void BKDisplay::beam_draw(const unsigned to, const unsigned ctl)
{
    if (to <= m_beam_byte) return;

    RAM * vmem = vram[(vram[1] != nullptr) ? ((ctl >> 15) & 1) : 0];
    const unsigned line = m_beam_line;
    const bool blank = vmem == nullptr || (quarter() && line >= m_video_lines / 4);
    const unsigned palette = (ctl >> 8) & 0x0F;
    // For scripts, the palette the line starts with
    if (m_beam_byte == 0) {
        if (line < 256) m_line_palette[line] = blank ? 16 : static_cast<uint8_t>(palette);
        // A mode switch comes from the render thread at any moment: the line
        // keeps the mode it started in
        m_beam_mode = m_mode;
    }

    const uint8_t * video = blank ? nullptr
        : vmem->get_buffer() + ((line + m_offset) % m_video_lines) * m_line_bytes;
    render_bytes(m_beam_buf.data(), video, m_beam_byte, to, m_beam_mode, blank, palette);
    m_beam_byte = to;
}

// The surface lock is taken once a line, as without the mode
void BKDisplay::beam_commit()
{
    if (m_beam_line >= m_video_lines) return;
    // The instruction that ran over the end of the line began at m_beam_at of
    // it: a register it changed counts from its write, and a write past the
    // end leaves the rest of the line as it was. Taking control() for all of
    // it put a byte or more of the next colour at the end of the line above a
    // palette stripe
    if (control() != m_beam_ctl && m_beam_at_line == m_beam_line && m_cpu != nullptr) {
        const int w = m_cpu->last_write_clocks();
        if (w >= 0) beam_draw(beam_byte(m_beam_at + (unsigned)w), m_beam_ctl);
    }
    beam_draw(m_line_bytes, control());
    {
        compat_lock_guard guard(m_surface_mutex);
        // A line laid out in the mode left behind is not copied: the switch
        // has the whole screen drawn again anyway (screen_valid)
        const unsigned width = mode_width(m_mode);
        if (m_beam_mode == m_mode && has_valid_renderer() && render_pixels != nullptr && (int)(width * 4) <= line_bytes) {
            memcpy(static_cast<uint8_t*>(render_pixels) + m_beam_line * line_bytes, m_beam_buf.data(), width * 4);
            // Drawn by the beam: see render_line()
            screen_valid = true;
        }
    }
    if (m_beam_line + 1 == m_video_lines) was_updated = true;
    m_beam_line = NO_LINE;
}

// 320 lines of 64 us: 20.48 ms, 3125/64 = 48.828125 frames a second at both
// 3 and 4 MHz
bool BKDisplay::native_frame_rate(uint64_t &num, uint64_t &den)
{
    return raster_frame_rate(num, den);
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
// surface that has never been drawn - the first frame, a mode switch -
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
    // The beam has drawn on this surface, so render_all() must not paint the
    // whole of it again from one palette: a render thread whose first pass
    // after a load came only with the screenshot of a script wiped the
    // stripes a program had changed the palette for (work-bk-0011m-raster)
    if (has_valid_renderer() && render_pixels != nullptr && (int)(mode_width(m_mode) * 4) <= line_bytes)
        screen_valid = true;
}

void BKDisplay::render_line_unlocked(const unsigned line)
{
    if (!has_valid_renderer() || render_pixels == nullptr) return;

    // The width is checked against the surface that is actually there: a mode
    // switch reaches the surface only on the next frame of the render thread
    const unsigned mode = m_mode;
    const unsigned width = mode_width(mode);
    if ((int)(width * 4) > line_bytes) return;

    uint8_t * base = static_cast<uint8_t*>(render_pixels) + line * line_bytes;

    const unsigned ctl = control();
    RAM * vmem = vram[(vram[1] != nullptr) ? ((ctl >> 15) & 1) : 0];

    const bool blank = vmem == nullptr || (quarter() && line >= m_video_lines / 4);
    const unsigned palette = (ctl >> 8) & 0x0F;
    if (line < 256) m_line_palette[line] = blank ? 16 : static_cast<uint8_t>(palette);

    // The quarter mode does not change the addressing: the firmware itself
    // points the scroll register at the last quarter of the video memory
    // (0070000-0077777), which is what the manuals describe as the extended
    // memory mode
    const uint8_t * video = blank ? nullptr
        : vmem->get_buffer() + ((line + m_offset) % m_video_lines) * m_line_bytes;
    render_bytes(reinterpret_cast<uint32_t*>(base), video, 0, m_line_bytes, mode, blank, palette);

    if (line + 1 == m_video_lines) was_updated = true;
}

// The video memory is read straight from its buffer: a virtual get_direct() on
// every byte of every line, 50 times a second, costs more than the drawing.
// A byte is 4 dots in colour (RGB and Mono) and 8 in the black and white mode
void BKDisplay::render_bytes(uint32_t * out, const uint8_t * video, const unsigned from, const unsigned to,
                             const unsigned mode, const bool blank, const unsigned palette) const
{
    if (blank) {
        // Below the quarter screen the beam draws nothing
        const unsigned dots = wide(mode) ? 8 : 4;
        for (unsigned i = from * dots; i < to * dots; i++)
            out[i] = BK_RGBA2[0];
    } else if (!wide(mode)) {
        const uint32_t * colors = (mode == BK_MODE_MONO) ? BK_GREY4 : BK_RGBA4[palette];
        for (unsigned i = from; i < to; i++) {
            const uint8_t b = video[i];
            // The lowest pair of bits is the leftmost dot
            for (unsigned k = 0; k < 4; k++)
                out[i * 4 + k] = colors[(b >> (k * 2)) & 3];
        }
    } else {
        for (unsigned i = from; i < to; i++) {
            const uint8_t b = video[i];
            // Bit 0 is the leftmost dot
            for (unsigned k = 0; k < 8; k++)
                out[i * 8 + k] = BK_RGBA2[(b >> k) & 1];
        }
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
    f.values.push_back({"bw",    QT_TRANSLATE_NOOP("DeviceOptions", "B/W")});
    f.values.push_back({"gray",  QT_TRANSLATE_NOOP("DeviceOptions", "Mono")});
    return {f};
}

void BKDisplay::save_state(StateWriter &w)
{
    RasterDisplay::save_state(w);
    w.u("scroll", m_scroll);
    w.n("offset", m_offset);
    w.n("frames", m_frames);
    // "color" is what snapshots without Mono had: 0 was the black and white mode
    w.b("color", !wide(m_mode));
    w.n("mode", m_mode);
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
    // The mode of the snapshot is not taken as it is: the surface still has the
    // width of the configuration, and a mono snapshot of a colour machine drew
    // nothing into it (a black screen). It goes through the switch instead. The
    // option restored before has already asked for it; without one, the mode
    // saved is the switch
    bool color = !wide(m_mode);
    r.b("color", color);
    unsigned mode = color ? BK_MODE_RGB : BK_MODE_BW;
    r.u("mode", mode);
    if (mode > BK_MODE_MONO) mode = BK_MODE_RGB;
    if (!m_mode_pending) {
        m_pending_mode = mode;
        m_mode_pending = (mode != m_mode);
    }
    r.hex("line_palette", m_line_palette, sizeof(m_line_palette));
    r.hex("frame_palette", m_frame_palette, sizeof(m_frame_palette));
    // A line the beam had half laid out is not in the snapshot: the next
    // BEAM() lays out what the beam has passed from the state as it is
    m_beam_line = NO_LINE;
    m_beam_at_line = NO_LINE;
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
    r.push_back({"color",   "1 for colour (RGB or Mono), 0 for black and white", false});
    r.push_back({"mode",    "Video output: 0 RGB, 1 black and white, 2 Mono",  false});
    r.push_back({"palettes", "Palette each line of the last completed frame was drawn with (in the high precision mode, the one at its first byte), 16 for a blank line", true});
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
        out.values.push_back(((field == "quarter") ? quarter() : !wide(m_mode)) ? 1 : 0);
        return true;
    }

    if (field == "mode")
    {
        out.numeric = true;
        out.values.push_back(m_mode);
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
