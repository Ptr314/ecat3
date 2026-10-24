// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple II / //e video (Правец-82/8М/8А/8С)

#include <cstring>

#include "apple2_video.h"
#include "apple2_mmu.h"
#include "emulator/utils.h"

#define A2V_OPTION_MONITOR  0

// The 16 colours of the Apple, as the lores codes name them (the Правец-8А
// book, table 2.8: black, dark purple-red, dark blue, violet, dark green,
// grey, blue, light blue, brown, orange, grey, pink, light green, yellow,
// aquamarine, white)
static const uint8_t A2_COLORS[16][3] = {
    {  0,   0,   0}, {157,   9, 102}, { 42,  42, 229}, {199,  52, 255},
    {  0, 128,   0}, {128, 128, 128}, { 13, 161, 255}, {170, 170, 255},
    { 85,  85,   0}, {242,  94,   0}, {192, 192, 192}, {255, 137, 229},
    { 56, 203,   0}, {213, 213,  26}, { 98, 246, 153}, {255, 255, 255}
};

// The monochrome monitors: dark and lit
static const uint8_t A2_MONO[3][2][3] = {
    {{0, 0, 0}, { 51, 255,  51}},   // ВММ 3107, green
    {{0, 0, 0}, {255, 255, 255}},
    {{0, 0, 0}, {255, 176,   0}},
};

// Text address of each row of 40 bytes
static inline unsigned text_row(unsigned row)
{
    return ((row & 7) << 7) + (row >> 3) * 40;
}

Apple2Video::Apple2Video(InterfaceManager *im, EmulatorConfigDevice *cd):
    RasterDisplay(im, cd)
    , m_monitor(MON_GREEN)
{
    m_clocked = true;
    sx = 560;
    sy = 192;
    std::memset(m_dots, 0, sizeof(m_dots));
    std::memset(m_late, 0, sizeof(m_late));
}

emulator::Result Apple2Video::load_config(SystemData *sd)
{
    m_standart = read_confg_value(cd, "standard", false, std::string("apple-pal"));
    emulator::Result res = RasterDisplay::load_config(sd);
    if (!res) return res;

    m_mmu = dynamic_cast<Apple2MMU*>(im->dm->get_device_by_name(cd->get_parameter("mmu").value));
    m_iou = dynamic_cast<Apple2IOU*>(im->dm->get_device_by_name(cd->get_parameter("iou").value));
    m_font = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));
    if (m_mmu == nullptr || m_iou == nullptr || m_font == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Video|" + std::string(QT_TRANSLATE_NOOP("Apple2Video", "The mmu, the iou and the font are expected")) + "} " + name);
    m_iie = m_mmu->is_iie();

    const std::string cs = str_tolower(read_confg_value(cd, "charset", false, std::string("iie")));
    if (cs == "iie") m_charset = CS_IIE;
    else if (cs == "ii") m_charset = CS_II;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Video|" + std::string(QT_TRANSLATE_NOOP("Apple2Video", "Unknown charset, iie or ii expected")) + "} " + cs);

    m_font_reverse = read_confg_value(cd, "font_reverse", false, false);
    for (unsigned v = 0; v < 128; v++) {
        unsigned r = 0;
        for (unsigned k = 0; k < 7; k++) if (v & (1u << k)) r |= 1u << (6 - k);
        m_reverse[v] = static_cast<uint8_t>(m_font_reverse ? r : v);
    }

    // A //e character ROM is 4K: 2K of glyphs and 2K the graphics go
    // through. The Правец-8А holds two such in a 2764, one of them chosen by
    // the jumper X20
    const unsigned half = read_confg_value(cd, "font_half", false, (unsigned)0);
    const unsigned set_size = (m_charset == CS_IIE) ? 0x1000 : 0x800;
    m_font_base = half * set_size;
    if (m_font_base + 0x800 > m_font->get_size())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Video|" + std::string(QT_TRANSLATE_NOOP("Apple2Video", "The font ROM is too small for font_half")) + "} " + name);

    const std::string rgb_rom = cd->get_parameter("rgb_rom", false).value;
    if (!rgb_rom.empty()) {
        m_rgb_rom = dynamic_cast<ROM*>(im->dm->get_device_by_name(rgb_rom));
        if (m_rgb_rom == nullptr || m_rgb_rom->get_size() < 32)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Apple2Video|" + std::string(QT_TRANSLATE_NOOP("Apple2Video", "The rgb_rom is expected to be a ROM of 32 bytes or more")) + "} " + rgb_rom);
    }

    const std::string mon = str_tolower(read_confg_value(cd, "monitor", false, std::string("green")));
    static const char * names[] = {"green", "white", "amber", "rgb", "rgb-stock", "rgb-variant"};
    for (unsigned i = 0; i < MON_COUNT; i++)
        if (mon == names[i]) m_monitor = i;
    if (m_monitor == MON_RGB_VARIANT && m_rgb_rom == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Video|" + std::string(QT_TRANSLATE_NOOP("Apple2Video", "The monitor rgb-variant needs rgb_rom")) + "} " + name);

    return emulator::Result::ok();
}

void Apple2Video::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    for (unsigned m = 0; m < MON_RGB; m++) vr.FillRGB(A2_MONO[m], m_mono[m], 2);
    vr.FillRGB(A2_COLORS, m_rgb, 16);
    // The stock card: of the four dots the 74LS175 latches on each colour
    // clock, the first goes to green, then blue, red and the intensity
    // (Правец-8С, fig. 5.8). The lores code is those four dots
    for (unsigned c = 0; c < 16; c++) {
        const unsigned g = c & 1, b = (c >> 1) & 1, r = (c >> 2) & 1, i = (c >> 3) & 1;
        m_rgb_stock[c] = vr.MapRGB(r * 0xAA + i * 0x55, g * 0xAA + i * 0x55, b * 0xAA + i * 0x55);
    }
    // A CGA monitor: bit 0 blue, 1 green, 2 red, 3 intensity (D0-D3 of the
    // PROM of the «Вариант» card), dark yellow turned brown
    for (unsigned c = 0; c < 16; c++) {
        const unsigned b = c & 1, g = (c >> 1) & 1, r = (c >> 2) & 1, i = (c >> 3) & 1;
        const unsigned gv = (c == 6) ? 0x55 : g * 0xAA + i * 0x55;
        m_cga[c] = vr.MapRGB(r * 0xAA + i * 0x55, gv, b * 0xAA + i * 0x55);
    }
}

void Apple2Video::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

// Seven dots of a character, bit 0 the leftmost, 1 lit
unsigned Apple2Video::glyph(unsigned code, unsigned row, bool altchar) const
{
    const uint8_t * font = m_font->get_buffer() + m_font_base;
    if (m_charset == CS_IIE) {
        // The //e ROM holds the inverse glyphs as they are shown and the
        // normal ones inverted. The primary set flashes $40-$7F by turning the
        // inverse glyph of $00-$3F over
        bool flip = false;
        if (!altchar && code >= 0x40 && code < 0x80) {
            code &= 0x3F;
            flip = m_flash;
        }
        unsigned v = m_reverse[~font[(code << 3) | row] & 0x7F];
        if (flip) v ^= 0x7F;
        return v;
    }
    // The ][: every glyph normal, inverse below $40 and flashing below $80
    // made by the hardware from the two top bits of the code
    unsigned v = m_reverse[font[(code << 3) | row] & 0x7F];
    if (code < 0x40 || (code < 0x80 && m_flash)) v ^= 0x7F;
    return v;
}

void Apple2Video::build_line(unsigned line, unsigned flags)
{
    const uint8_t * main = m_mmu->video_main();
    const uint8_t * aux = m_mmu->video_aux();
    const bool text = (flags & Apple2IOU::VF_TEXT) || ((flags & Apple2IOU::VF_MIXED) && line >= 160);
    const bool col80 = m_iie && aux != nullptr && (flags & Apple2IOU::VF_80COL);
    // 80STORE takes PAGE2 over for the memory switch, the display stays on page 1
    const bool page2 = (flags & Apple2IOU::VF_PAGE2) && !(flags & Apple2IOU::VF_80STORE);
    uint8_t * d = m_dots;
    std::memset(m_late, 0, sizeof(m_late));

    if (text) {
        const unsigned row = line >> 3, ln = line & 7;
        const unsigned a = (page2 ? 0x800 : 0x400) + text_row(row);
        const bool alt = (flags & Apple2IOU::VF_ALTCHAR) != 0;
        for (unsigned x = 0; x < 40; x++) {
            if (col80) {
                const unsigned ga = glyph(aux[a + x], ln, alt), gm = glyph(main[a + x], ln, alt);
                for (unsigned k = 0; k < 7; k++) *d++ = (ga >> k) & 1;
                for (unsigned k = 0; k < 7; k++) *d++ = (gm >> k) & 1;
            } else {
                const unsigned g = glyph(main[a + x], ln, alt);
                for (unsigned k = 0; k < 7; k++) { const uint8_t v = (g >> k) & 1; *d++ = v; *d++ = v; }
            }
        }
        return;
    }

    // Double graphics: 80 columns with AN3 off
    const bool dbl = col80 && !(flags & Apple2IOU::VF_AN3);
    if (flags & Apple2IOU::VF_HIRES) {
        const unsigned a = (page2 ? 0x4000 : 0x2000) + ((line & 7) << 10) + text_row(line >> 3);
        if (dbl) {
            for (unsigned x = 0; x < 40; x++) {
                const unsigned ba = aux[a + x], bm = main[a + x];
                for (unsigned k = 0; k < 7; k++) *d++ = (ba >> k) & 1;
                for (unsigned k = 0; k < 7; k++) *d++ = (bm >> k) & 1;
            }
        } else {
            // A byte with bit 7 comes half a dot (one of the 14) late; its
            // first dot is the last one of the byte before, held
            uint8_t last = 0;
            for (unsigned x = 0; x < 40; x++) {
                const unsigned b = main[a + x];
                uint8_t * p = d + x * 14;
                if (b & 0x80) {
                    m_late[x] = 1;
                    p[0] = last;
                    for (unsigned k = 0; k < 7; k++) {
                        const uint8_t v = (b >> k) & 1;
                        p[2 * k + 1] = v;
                        if (2 * k + 2 < 14) p[2 * k + 2] = v;
                    }
                } else {
                    for (unsigned k = 0; k < 7; k++) { const uint8_t v = (b >> k) & 1; p[2 * k] = v; p[2 * k + 1] = v; }
                }
                last = p[13];
            }
        }
        return;
    }

    // Lores: a nibble per block, its four bits repeated along the line in
    // step with the colour clock
    const unsigned a = (page2 ? 0x800 : 0x400) + text_row(line >> 3);
    const unsigned shift = ((line & 7) < 4) ? 0 : 4;
    for (unsigned x = 0; x < 40; x++) {
        if (dbl) {
            const unsigned na = (aux[a + x] >> shift) & 0x0F, nm = (main[a + x] >> shift) & 0x0F;
            // The auxiliary half is a colour clock dot ahead: a program writes
            // its colours there rotated right by one (the Правец-8А book, p.40)
            for (unsigned k = 0; k < 7; k++) { const unsigned p = x * 14 + k; d[p] = (na >> ((p + 3) & 3)) & 1; }
            for (unsigned k = 7; k < 14; k++) { const unsigned p = x * 14 + k; d[p] = (nm >> (p & 3)) & 1; }
        } else {
            const unsigned n = (main[a + x] >> shift) & 0x0F;
            for (unsigned k = 0; k < 14; k++) { const unsigned p = x * 14 + k; d[p] = (n >> (p & 3)) & 1; }
        }
    }
}

void Apple2Video::render_line(unsigned line)
{
    compat_lock_guard guard(m_surface_mutex);
    render_line_unlocked(line);
    screen_valid = true;
}

void Apple2Video::render_line_unlocked(unsigned line)
{
    if (!has_valid_renderer() || render_pixels == nullptr || line >= sy) return;
    const unsigned flags = m_iou->video_flags();
    build_line(line, flags);
    uint32_t * out = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(render_pixels) + line * line_bytes);
    const unsigned mon = m_monitor.load();
    const bool text = (flags & Apple2IOU::VF_TEXT) || ((flags & Apple2IOU::VF_MIXED) && line >= 160);

    if (mon < MON_RGB || text) {
        // Text carries no colour: the card puts the dots out white
        const uint32_t * c = (mon < MON_RGB) ? m_mono[mon] : m_mono[1];
        for (unsigned p = 0; p < 560; p++) out[p] = c[m_dots[p]];
        return;
    }
    if (mon == MON_RGB_STOCK) {
        // Latched every four dots, on the colour clock
        for (unsigned p = 0; p < 560; p += 4) {
            const unsigned n = m_dots[p] | (m_dots[p + 1] << 1) | (m_dots[p + 2] << 2) | (m_dots[p + 3] << 3);
            out[p] = out[p + 1] = out[p + 2] = out[p + 3] = m_rgb_stock[n];
        }
        return;
    }
    if (mon == MON_RGB_VARIANT) {
        // Four dots latched on the colour clock and taken through the PROM,
        // in its address A0 the first dot, A2, A3, A1 the next ones. Its
        // second table (A4) is the first one for the four dots starting a
        // dot earlier; the hand drawn schematic does not tell its source, it
        // is taken here as the half dot delay of a hires byte, which the
        // table puts right
        const uint8_t * prom = m_rgb_rom->get_buffer();
        for (unsigned p = 0; p < 560; p += 4) {
            const unsigned late = m_late[p / 14];
            const uint8_t * w = (late && p > 0) ? m_dots + p - 1 : m_dots + p;
            const unsigned a = w[0] | (w[3] << 1) | (w[1] << 2) | (w[2] << 3) | (late << 4);
            out[p] = out[p + 1] = out[p + 2] = out[p + 3] = m_cga[prom[a] & 0x0F];
        }
        return;
    }
    // The colour of a dot is the code of the four last dots, each in its
    // place of the colour clock
    unsigned n = 0;
    for (unsigned p = 0; p < 560; p++) {
        const unsigned bit = 1u << (p & 3);
        n = m_dots[p] ? (n | bit) : (n & ~bit);
        out[p] = m_rgb[n];
    }
}

void Apple2Video::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid && has_valid_renderer() && render_pixels != nullptr)
        for (unsigned line = 0; line < 192 && line < sy; line++)
            render_line_unlocked(line);
    screen_valid = true;
    was_updated = true;
}

void Apple2Video::HSYNC(const unsigned line, const unsigned sync_val)
{
    if (sync_val == 0) {
        m_line = line;
        if (line == 0) {
            // The //e flashes at about 2 Hz: a phase every 16 frames
            if ((++m_frames & 15) == 0) m_flash = !m_flash;
        }
        return;
    }
    if (sync_val == 1 && line < 192) {
        render_line(line);
        if (line == 191) frame_complete();
    }
}

// The half of the character ROM, when the config names its halves
// (font_half_title_0, _1, ...): jumper X20 of the Правец-8А
ConfigFields Apple2Video::get_config_fields()
{
    ConfigField f;
    f.name = "font_half";
    const std::string label = str_trim(cd->get_parameter("font_half_label", false).value);
    f.title = label.empty() ? std::string(QT_TRANSLATE_NOOP("ConfigFields", "Character set")) : label;
    f.type = CONFIG_FIELD_CHOICE;
    f.def = "0";
    for (unsigned n = 0; ; n++) {
        const std::string title = str_trim(cd->get_parameter("font_half_title_" + std::to_string(n), false).value);
        if (title.empty()) break;
        f.values.push_back({std::to_string(n), title});
    }
    if (f.values.empty()) return {};
    return {f};
}

DeviceOptions Apple2Video::get_device_options()
{
    DeviceOptions o = {
        {
            A2V_OPTION_MONITOR, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Video output"), "kscreensaver.png",
            {
                {MON_GREEN, QT_TRANSLATE_NOOP("DeviceOptions", "Green monitor")},
                {MON_WHITE, QT_TRANSLATE_NOOP("DeviceOptions", "White monitor")},
                {MON_AMBER, QT_TRANSLATE_NOOP("DeviceOptions", "Amber monitor")},
                {MON_RGB, QT_TRANSLATE_NOOP("DeviceOptions", "RGB card, Apple colours")},
                {MON_RGB_STOCK, QT_TRANSLATE_NOOP("DeviceOptions", "RGB card, Pravetz stock")}
            },
            m_monitor.load()
        }
    };
    if (m_rgb_rom != nullptr)
        o[0].values.push_back({MON_RGB_VARIANT, QT_TRANSLATE_NOOP("DeviceOptions", "RGB card, Variant")});
    return o;
}

void Apple2Video::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id == A2V_OPTION_MONITOR && value_id < MON_COUNT
        && (value_id != MON_RGB_VARIANT || m_rgb_rom != nullptr)) {
        m_monitor = value_id;
        screen_valid = false;
    }
}

std::vector<DeviceFieldInfo> Apple2Video::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = RasterDisplay::get_device_fields();
    r.push_back({"monitor", "0 green, 1 white, 2 amber, 3 RGB, 4 stock Правец RGB, 5 RGB of «Вариант»", false});

    r.push_back({"flash",   "Phase of the flashing characters",                    false});
    r.push_back({"vbl",     "1 while the beam is in the vertical blanking",        false});
    return r;
}

bool Apple2Video::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "monitor" || field == "flash" || field == "vbl") {
        out.numeric = true;
        if (field == "monitor") out.values.push_back(m_monitor.load());
        else if (field == "flash") out.values.push_back(m_flash ? 1 : 0);
        else out.values.push_back(in_vbl() ? 1 : 0);
        return true;
    }
    return RasterDisplay::get_field(field, from, to, out);
}

void Apple2Video::save_state(StateWriter &w)
{
    RasterDisplay::save_state(w);
    w.n("line", m_line);
    w.n("frames", m_frames);
    w.b("flash", m_flash);
}

emulator::Result Apple2Video::load_state(const StateReader &r)
{
    emulator::Result res = RasterDisplay::load_state(r);
    if (!res) return res;
    r.u("line", m_line);
    r.u("frames", m_frames);
    r.b("flash", m_flash);
    return emulator::Result::ok();
}

ComputerDevice * create_apple2_video(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Apple2Video(im, cd);
}
