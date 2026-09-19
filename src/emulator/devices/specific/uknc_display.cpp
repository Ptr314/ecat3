// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ display controller device

#include "uknc_display.h"
#include "emulator/utils.h"

// The list starts here in plane 0 and is walked for this many lines; the first
// nineteen fall into the blanking above the picture
#define UKNC_TAGS_START     0000270
#define UKNC_TAGS_TOTAL     307
#define UKNC_TOP_BLANK      19
#define UKNC_LINES          288
#define UKNC_WIDTH          640

// Colour is four bits, YRGB, and the brightness modifier of a params
// descriptor picks one of eight blocks of sixteen: a cleared bit of 177054...
// no - of the descriptor - dims that channel, 0x80 to 0x60 and 0xFF to 0xDF.
// Taken from UKNCBTL (ScreenView_StandardRGBColors), which matches the machine.
static const uint32_t UKNC_RGB[128] = {
    0x000000,0x000080,0x008000,0x008080,0x800000,0x800080,0x808000,0x808080,
    0x000000,0x0000FF,0x00FF00,0x00FFFF,0xFF0000,0xFF00FF,0xFFFF00,0xFFFFFF,
    0x000000,0x000060,0x008000,0x008060,0x800000,0x800060,0x808000,0x808060,
    0x000000,0x0000DF,0x00FF00,0x00FFDF,0xFF0000,0xFF00DF,0xFFFF00,0xFFFFDF,
    0x000000,0x000080,0x006000,0x006080,0x800000,0x800080,0x806000,0x806080,
    0x000000,0x0000FF,0x00DF00,0x00DFFF,0xFF0000,0xFF00FF,0xFFDF00,0xFFDFFF,
    0x000000,0x000060,0x006000,0x006060,0x800000,0x800060,0x806000,0x806060,
    0x000000,0x0000DF,0x00DF00,0x00DFDF,0xFF0000,0xFF00DF,0xFFDF00,0xFFDFDF,
    0x000000,0x000080,0x008000,0x008080,0x600000,0x600080,0x608000,0x608080,
    0x000000,0x0000FF,0x00FF00,0x00FFFF,0xDF0000,0xDF00FF,0xDFFF00,0xDFFFFF,
    0x000000,0x000060,0x008000,0x008060,0x600000,0x600060,0x608000,0x608060,
    0x000000,0x0000DF,0x00FF00,0x00FFDF,0xDF0000,0xDF00DF,0xDFFF00,0xDFFFDF,
    0x000000,0x000080,0x006000,0x006080,0x600000,0x600080,0x606000,0x606080,
    0x000000,0x0000FF,0x00DF00,0x00DFFF,0xDF0000,0xDF00FF,0xDFDF00,0xDFDFFF,
    0x000000,0x000060,0x006000,0x006060,0x600000,0x600060,0x606000,0x606060,
    0x000000,0x0000DF,0x00DF00,0x00DFDF,0xDF0000,0xDF00DF,0xDFDF00,0xDFDFDF
};

// Eight grey levels, repeated over the sixteen colours
static const uint32_t UKNC_GRAY_LEVELS[8] = {
    0x000000,0x242424,0x484848,0x6C6C6C,0x909090,0xB4B4B4,0xD8D8D8,0xFFFFFF
};

// Filled in set_renderer() for the palette in use, in the renderer's own order
static uint8_t  UKNC_TABLE[128][3];
static uint32_t UKNC_RGBA[128];

// A plane byte spread over eight nibbles, bit N into bit 0 of nibble N. Three
// of them OR-ed together, shifted by the plane number, give the colour index
// of every dot of an octet at once
static uint32_t UKNC_SPREAD[256];

static void build_spread()
{
    for (unsigned b = 0; b < 256; b++) {
        uint32_t v = 0;
        for (unsigned bit = 0; bit < 8; bit++)
            if (b & (1u << bit)) v |= 1u << (bit * 4);
        UKNC_SPREAD[b] = v;
    }
}

UKNCDisplay::UKNCDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
      RasterDisplay(im, cd)
    , i_frame(this, im, 1, "frame", MODE_W)
{
    m_standart = "uknc";
    sx = UKNC_WIDTH;
    sy = UKNC_LINES;
    if (UKNC_SPREAD[1] == 0) build_spread();
}

emulator::Result UKNCDisplay::load_config(SystemData *sd)
{
    emulator::Result res = RasterDisplay::load_config(sd);
    if (!res) return res;

    static const char * names[3] = {"plane0", "plane1", "plane2"};
    for (unsigned i = 0; i < 3; i++) {
        m_plane[i] = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter(names[i]).value, false));
        if (m_plane[i] == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCDisplay|" + std::string(QT_TRANSLATE_NOOP("UKNCDisplay", "Plane must be a RAM device")) + "} "
                + name + ": " + names[i]);
    }

    const std::string mode = str_tolower(read_confg_value(cd, "colors", false, std::string("rgb")));
    if (mode == "grb") m_colors = UKNC_COLORS_GRB;
    else if (mode == "gray" || mode == "grey") m_colors = UKNC_COLORS_GRAY;
    else m_colors = UKNC_COLORS_RGB;
    m_pending_colors = m_colors;

    sx = UKNC_WIDTH;
    sy = UKNC_LINES;

    reset_walk();

    // Settle the pulse line low, so the first frame is already an edge
    i_frame.change(0);

    return emulator::Result::ok();
}

void UKNCDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);

    for (unsigned i = 0; i < 128; i++) {
        uint32_t c = UKNC_RGB[i];
        if (m_colors == UKNC_COLORS_GRB) {
            // Some machines have the red and green outputs transposed
            c = (c & 0x0000FF) | ((c & 0xFF0000) >> 8) | ((c & 0x00FF00) << 8);
        } else if (m_colors == UKNC_COLORS_GRAY) {
            c = UKNC_GRAY_LEVELS[i & 7];
        }
        UKNC_TABLE[i][0] = (uint8_t)((c >> 16) & 0xFF);
        UKNC_TABLE[i][1] = (uint8_t)((c >> 8) & 0xFF);
        UKNC_TABLE[i][2] = (uint8_t)(c & 0xFF);
    }
    vr.FillRGB(UKNC_TABLE, UKNC_RGBA, 128);

    rebuild_line_colors();
}

void UKNCDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    // The palette switch is applied here, on the render thread, so it cannot
    // land in the middle of a line the emulation thread is drawing
    if (m_mode_pending) {
        lock_surface();
        m_mode_pending = false;
        if (m_pending_colors != m_colors) {
            m_colors = m_pending_colors;
            if (has_valid_renderer()) {
                for (unsigned i = 0; i < 128; i++) {
                    uint32_t c = UKNC_RGB[i];
                    if (m_colors == UKNC_COLORS_GRB)
                        c = (c & 0x0000FF) | ((c & 0xFF0000) >> 8) | ((c & 0x00FF00) << 8);
                    else if (m_colors == UKNC_COLORS_GRAY)
                        c = UKNC_GRAY_LEVELS[i & 7];
                    UKNC_TABLE[i][0] = (uint8_t)((c >> 16) & 0xFF);
                    UKNC_TABLE[i][1] = (uint8_t)((c >> 8) & 0xFF);
                    UKNC_TABLE[i][2] = (uint8_t)(c & 0xFF);
                }
                renderer->FillRGB(UKNC_TABLE, UKNC_RGBA, 128);
                rebuild_line_colors();
            }
            screen_valid = false;
            was_updated = true;
        }
        unlock_surface();
    }

    *sx = this->sx;
    *sy = this->sy;
}

DeviceOptions UKNCDisplay::get_device_options()
{
    return {
        {
            UKNC_OPTION_COLORS, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Video output"), "kscreensaver.png",
            {
                {UKNC_COLORS_RGB,  QT_TRANSLATE_NOOP("DeviceOptions", "RGB")},
                {UKNC_COLORS_GRB,  QT_TRANSLATE_NOOP("DeviceOptions", "GRB")},
                {UKNC_COLORS_GRAY, QT_TRANSLATE_NOOP("DeviceOptions", "Mono")}
            },
            m_mode_pending? m_pending_colors : m_colors
        }
    };
}

void UKNCDisplay::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id != UKNC_OPTION_COLORS) return;
    m_pending_colors = value_id;
    m_mode_pending = true;
}

void UKNCDisplay::reset(const bool cold)
{
    RasterDisplay::reset(cold);
    reset_walk();
    m_frames = 0;
    m_entries = 0;
}

//--------------------------- Walking the list ------------------------------//

unsigned int UKNCDisplay::plane_word(unsigned int address) const
{
    // The table lives in plane 0 and is read as words
    const uint8_t * p = m_plane[0]->get_buffer();
    const unsigned int a = address & 0xFFFF;
    return p[a] | (p[(a + 1) & 0xFFFF] << 8);
}

void UKNCDisplay::reset_walk()
{
    m_tag_address = UKNC_TAGS_START;
    // The first descriptor of a frame is always a two-word one
    m_tag_four = false;
    m_tag_palette = false;
    // Курсор включается не значением, а ПЕРЕКЛЮЧЕНИЕМ: разряд 0 слова связи
    // переворачивает триггер, и строка курсора очерчена двумя такими словами.
    // Триггер гасится кадровым импульсом, и это не мелочь: убирая курсор, ПЗУ
    // просто снимает оба разряда, и триггер, переживший кадр во включённом
    // состоянии, красил бы своим цветом весь столбец до низа экрана
    m_cursor_on = false;
    m_entries = 0;
}

void UKNCDisplay::apply_four_word(unsigned int tag1, unsigned int tag2)
{
    if (m_tag_palette) {
        // Eight colours of four bits, colour 0 in the lowest nibble
        m_palette = (uint32_t)tag1 | ((uint32_t)tag2 << 16);
    } else {
        // Bits 4-5 are the scale exponent, bits 0-2 the brightness, active low.
        // The cursor position is shifted by the exponent while it is still an
        // exponent - the order matters and is the hardware's
        const unsigned scale_exp = (tag2 >> 4) & 3;
        m_pbpgpr = (7 - (tag2 & 7)) << 4;
        m_cursor_color   = tag1 & 15;
        m_cursor_graphic = (tag1 & 16) != 0;
        m_cursor_pos     = ((tag1 >> 8) >> scale_exp) & 0x7F;
        m_cursor_bit     = (tag1 >> 5) & 7;
        m_scale          = 1u << scale_exp;
    }
    // Both kinds of descriptor settle the colours of the following lines: a
    // params one changes the brightness block the same palette is read through
    rebuild_line_colors();
}

void UKNCDisplay::rebuild_line_colors()
{
    for (unsigned c = 0; c < 8; c++) {
        const unsigned yrgb = (m_palette >> (c * 4)) & 15;
        m_line_colors[c] = UKNC_RGBA[(m_pbpgpr | yrgb) & 127];
    }
}

void UKNCDisplay::next_line(unsigned int raster_line)
{
    unsigned int tag1 = 0, tag2 = 0;

    if (m_tag_four) {
        tag1 = plane_word(m_tag_address);
        tag2 = plane_word(m_tag_address + 2);
        m_tag_address += 4;
    }

    const unsigned int bits_address = plane_word(m_tag_address);
    const unsigned int tagB = plane_word(m_tag_address + 2);

    if (m_tag_four) apply_four_word(tag1, tag2);

    // The link word says where the next descriptor is and what shape it has
    m_tag_four = (tagB & 2) != 0;
    if (m_tag_four) {
        m_tag_address = tagB & ~7u;         // four-word descriptors are 8-byte aligned
        m_tag_palette = (tagB & 4) != 0;
    } else {
        m_tag_address = tagB & ~3u;         // two-word ones, 4-byte aligned
    }
    // Bit 0 flips the cursor rather than setting it
    if ((tagB & 1) != 0) m_cursor_on = !m_cursor_on;

    m_entries++;

    if (raster_line >= UKNC_TOP_BLANK) {
        const unsigned y = raster_line - UKNC_TOP_BLANK;
        if (y < UKNC_LINES) render_line(y, bits_address);
    }
}

//--------------------------- Drawing --------------------------------------//

void UKNCDisplay::render_line_blank(unsigned int y)
{
    if (!has_valid_renderer() || render_pixels == nullptr) return;
    uint8_t * base = static_cast<uint8_t*>(render_pixels) + y * line_bytes;
    for (unsigned i = 0; i < UKNC_WIDTH; i++)
        *reinterpret_cast<uint32_t*>(base + i * 4) = UKNC_RGBA[0];
}

void UKNCDisplay::render_line(unsigned int y, unsigned int bits_address)
{
    compat_lock_guard guard(m_surface_mutex);

    if (!has_valid_renderer() || render_pixels == nullptr) return;
    if ((int)(UKNC_WIDTH * 4) > line_bytes) return;

    uint8_t * base = static_cast<uint8_t*>(render_pixels) + y * line_bytes;

    // Read straight from the buffers: three bytes per octet, 288 lines, 50
    // times a second is too many virtual calls to spend on get_value()
    const uint8_t * p0 = m_plane[0]->get_buffer();
    const uint8_t * p1 = m_plane[1]->get_buffer();
    const uint8_t * p2 = m_plane[2]->get_buffer();

    // 1, 2, 4 or 8 screen dots per pixel, so a line is a whole number of
    // octets and no dot needs a check against the width
    const unsigned scale = m_scale;
    const unsigned octets = UKNC_WIDTH / (8 * scale);
    const uint32_t cursor_color = UKNC_RGBA[m_cursor_color & 127];
    unsigned addr = bits_address & 0xFFFF;
    uint32_t * dst = reinterpret_cast<uint32_t*>(base);

    if (y < UKNC_LINES) m_line_scale[y] = (uint8_t)scale;

    for (unsigned pos = 0; pos < octets; pos++) {
        // The same address in all three planes; plane 0 is the low bit of the
        // colour, and bit 0 of a byte is the leftmost dot of its octet
        uint32_t dots = UKNC_SPREAD[p0[addr]] | (UKNC_SPREAD[p1[addr]] << 1) | (UKNC_SPREAD[p2[addr]] << 2);
        addr = (addr + 1) & 0xFFFF;

        uint32_t colors[8];
        for (unsigned bit = 0; bit < 8; bit++, dots >>= 4)
            colors[bit] = m_line_colors[dots & 7];

        // The cursor sits in one octet, the whole of it or one dot
        if (m_cursor_on && pos == m_cursor_pos) {
            if (m_cursor_graphic) colors[m_cursor_bit & 7] = cursor_color;
            else for (unsigned bit = 0; bit < 8; bit++) colors[bit] = cursor_color;
        }

        if (scale == 1) {
            for (unsigned bit = 0; bit < 8; bit++) *dst++ = colors[bit];
        } else {
            for (unsigned bit = 0; bit < 8; bit++)
                for (unsigned k = 0; k < scale; k++) *dst++ = colors[bit];
        }
    }

    if (y + 1 == UKNC_LINES) was_updated = true;
}

// Only a surface that has never been drawn is filled here: the picture is laid
// out line by line from the emulation thread, and repainting a whole frame from
// the state of one moment would flatten everything the list changes mid-screen
void UKNCDisplay::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid) {
        for (unsigned y = 0; y < UKNC_LINES; y++) render_line_blank(y);
        screen_valid = true;
        was_updated = true;
    }
}

//--------------------------- Raster events --------------------------------//

void UKNCDisplay::FRAME_SYNC()
{
    // What the frame just ended walked, kept so that a script reading the field
    // sees a whole frame rather than however far the beam has got
    m_entries_done = m_entries;
    // A frame always starts at the top of the table
    reset_walk();
    m_frames++;
    i_frame.change(1);
    i_frame.change(0);
}

void UKNCDisplay::HSYNC(const unsigned line, const unsigned sync_val)
{
    // One descriptor per line, taken after the sync pulse, so the line is drawn
    // from the table as it stands at that moment
    if (sync_val == 0) return;
    if (line >= UKNC_TAGS_TOTAL) return;
    next_line(line);
}

//--------------------------- Introspection --------------------------------//

ConfigFields UKNCDisplay::get_config_fields()
{
    ConfigField f;
    f.name = "colors";
    f.title = QT_TRANSLATE_NOOP("DeviceOptions", "Video output");
    f.type = CONFIG_FIELD_CHOICE;
    f.def = "rgb";
    f.values.push_back({"rgb",  QT_TRANSLATE_NOOP("DeviceOptions", "RGB")});
    f.values.push_back({"grb",  QT_TRANSLATE_NOOP("DeviceOptions", "GRB")});
    f.values.push_back({"gray", QT_TRANSLATE_NOOP("DeviceOptions", "Mono")});
    return {f};
}

std::vector<DeviceFieldInfo> UKNCDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = RasterDisplay::get_device_fields();
    r.push_back({"palette",  "Eight colours of the current line, YRGB each",     false});
    r.push_back({"scale",    "Screen dots per pixel on the current line",        false});
    r.push_back({"scales",   "Scale each line of the last frame was drawn with", true});
    r.push_back({"cursor",   "Cursor state as the table left it",                false});
    r.push_back({"entries",  "Descriptors walked in the last completed frame",   false});
    r.push_back({"frames",   "Frames started since the machine came up",         false});
    r.push_back({"list",     "The table of lines as the controller walks it",    true});
    return r;
}

bool UKNCDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "palette") {
        out.numeric = true;
        out.width = 8;
        for (unsigned c = 0; c < 8; c++)
            out.values.push_back((m_palette >> (c * 4)) & 15);
        return true;
    }

    if (field == "scale") {
        out.numeric = true;
        out.values.push_back(m_scale);
        return true;
    }

    if (field == "scales") {
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        const unsigned last = (to > from)? to : from;
        for (unsigned i = from; i <= last && i < UKNC_LINES; i++)
            out.values.push_back(m_line_scale[i]);
        return true;
    }

    if (field == "cursor") {
        out.numeric = false;
        out.text = std::string(m_cursor_on? "on" : "off")
                 + ", " + (m_cursor_graphic? "graphic" : "symbolic")
                 + ", pos=" + std::to_string(m_cursor_pos)
                 + ", bit=" + std::to_string(m_cursor_bit)
                 + ", color=" + std::to_string(m_cursor_color);
        return true;
    }

    if (field == "entries") {
        out.numeric = true;
        out.values.push_back(m_entries_done);
        return true;
    }

    if (field == "frames") {
        out.numeric = true;
        out.values.push_back(m_frames);
        return true;
    }

    // The single most useful thing when a picture comes out wrong: the table as
    // the controller reads it, one line per descriptor
    if (field == "list") {
        out.numeric = false;
        unsigned address = UKNC_TAGS_START;
        bool four = false, pal = false;
        const unsigned count = (to > from)? (to - from + 1) : ((from > 0)? from : 8);

        for (unsigned i = 0; i < count && i < UKNC_TAGS_TOTAL; i++) {
            std::string extra;
            if (four) {
                const unsigned t1 = plane_word(address);
                const unsigned t2 = plane_word(address + 2);
                if (pal) {
                    extra = " палитра=" + oct_str(t1, 6) + "," + oct_str(t2, 6);
                } else {
                    extra = " растяжка=" + std::to_string(1u << ((t2 >> 4) & 3))
                          + " яркость=" + std::to_string(t2 & 7)
                          + " курсор=" + oct_str(t1, 6);
                }
                address += 4;
            }
            const unsigned bits = plane_word(address);
            const unsigned tagB = plane_word(address + 2);

            if (!out.text.empty()) out.text += "\n";
            out.text += "        " + std::to_string(i) + ": точки=" + oct_str(bits, 6)
                      + " связь=" + oct_str(tagB, 6) + extra;

            four = (tagB & 2) != 0;
            if (four) { address = tagB & ~7u; pal = (tagB & 4) != 0; }
            else address = tagB & ~3u;
        }
        return true;
    }

    return RasterDisplay::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCDisplay(im, cd);
}
