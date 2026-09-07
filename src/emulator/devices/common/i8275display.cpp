// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common i8275(КР580ВГ75)-based display controller device

#include <cstring>

#include "i8275display.h"
#include "i8257.h"
#include "emulator/utils.h"

static const uint8_t VG75_8Colors[8][3] = { {  0,   0,   0}, {  0,   0, 255}, {  0, 255,   0}, {  0, 255, 255},
                                            {255,   0,   0}, {255,   0, 255}, {255, 255,   0}, {255, 255, 255}
                                          };

I8275Display::I8275Display(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , i_high(this, im, 1, "high", MODE_R)
    , m_have_frame(false)
    , m_frame_cpl(78)
    , m_frame_lps(30)
    , m_frame_h(10)
    , m_surface_sy(0)
{
    sx = 78*6;
    sy = 30*10;
    memset(&rgba_colors, 0, sizeof(rgba_colors));
    memset(&m_rows, 0, sizeof(m_rows));
    reset_attr();
}

void I8275Display::set_attr(uint8_t v)
{
    FAReverse = (v & 0x10) != 0;
    FAUnder = (v & 0x20) != 0;
    FABlink = (v & 0x02) != 0;
    if (RGB[0] > 8) {
        FAColor = 7;
    } else {
        FAColor =  ((v >> RGB[2]) & 0x01) +                     //B
                  (((v >> RGB[1]) & 0x01) << 1) +               //G
                  (((v >> RGB[0]) & 0x01) << 2);                //R
        if (RGBInv) FAColor ^= 0x07;
    }
}

void I8275Display::reset_attr()
{
    FAReverse = false;
    FAUnder = false;
    FABlink = false;
    FAColor = 7;
    NextAttr = 0;
}

void I8275Display::clear_rows()
{
    for (unsigned int i = 0; i < I8275D_MAX_ROWS; i++) m_rows[i].len = 0;
}

//Follows the geometry of the controller. Returns false while the renderer
//still holds a surface of the previous size - the resize happens on the
//render thread, one frame later
bool I8275Display::follow_resolution()
{
    const unsigned int cpl = m_frame_cpl;
    const unsigned int lps = m_frame_lps;
    const unsigned int h = m_frame_h;

    if ((cpl < 10) || (lps < 10) || (lps > I8275D_MAX_ROWS)) return false;
    if (cpl*6 > (unsigned int)line_bytes / 4) return false;

    if ((cpl*6 != sx) || (lps*h != sy))
    {
        sx = cpl*6;
        sy = lps*h;
        screen_valid = false;
        was_updated = true;
        return false;
    }
    return true;
}

//Blacks the surface out without going through VideoRenderer::fill(). With the
//OpenGL back end that call reaches QImage::fill(), which detaches the
//implicitly shared image the widget is still holding: the renderer would carry
//on with a fresh buffer while render_pixels, taken once in set_renderer(), kept
//pointing at the copy the widget frees underneath us
void I8275Display::clear_surface()
{
    if (render_pixels == nullptr || line_bytes <= 0) return;
    const uint32_t bg = rgba_colors[0];
    const unsigned int words = (unsigned int)line_bytes / 4;
    for (unsigned int y = 0; y < m_surface_sy; y++)
    {
        uint32_t * p = reinterpret_cast<uint32_t *>(
            static_cast<uint8_t *>(render_pixels) + (size_t)y * line_bytes);
        for (unsigned int x = 0; x < words; x++) p[x] = bg;
    }
}

void I8275Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(VG75_8Colors, rgba_colors, 8);
    m_surface_sy = sy;
}

emulator::Result I8275Display::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;
    Memory = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    Font = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));
    VG75 = dynamic_cast<I8275*>(im->dm->get_device_by_name(cd->get_parameter("i8275").value));
    DMA = dynamic_cast<I8257*>(im->dm->get_device_by_name(cd->get_parameter("dma").value));
    Channel = parse_numeric_value(cd->get_parameter("channel").value);
    AttrDelay = cd->get_parameter("attr_delay", false).value == "1";
    std::string s = cd->get_parameter("rgb", false).value;
    if (s.empty())
    {
        memset(&RGB, 0xFF, sizeof(RGB));
    } else {
        RGBInv = s[0] == '^';
        if (RGBInv) s = s.substr(1);

        for (unsigned int i=0; i<3; i++)
            RGB[i] = s[i] - '0';
    }

    //The controller does the fetching from now on, and it is this device that
    //the config names the memory, the DMA and the channel on
    VG75->attach(this, DMA, Channel, Memory);

    return emulator::Result::ok();
}

void I8275Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void I8275Display::reset(bool cold)
{
    GenericDisplay::reset(cold);
    m_have_frame = false;
    clear_rows();
    reset_attr();
}

//--------------------- I8275RowSink, on the emulation thread -------------

void I8275Display::row_complete(unsigned int row, const uint8_t * data, unsigned int count)
{
    if (row >= I8275D_MAX_ROWS) return;

    RowSnapshot & r = m_rows[row];
    r.len = (count > I8275_MAX_ROW)?I8275_MAX_ROW:count;
    if (r.len > 0) memcpy(r.data, data, r.len);

    r.font_bank   = (i_high.linked > 0)?((i_high.value & 1) << 10):0;
    r.cursor_col  = VG75->RegCursor[0];
    r.cursor_row  = VG75->RegCursor[1];
    r.cursor_mode = VG75->RegMode[3] & 0x30;
    r.add_mode    = VG75->RegMode[3] >> 7;
    r.transparent = VG75->transparent_attributes();
    r.blink       = VG75->Blinker;
    r.blink_char  = VG75->BlinkerChar;
}

void I8275Display::frame_complete()
{
    //Taken here, not while drawing: from now on the frame is described by these
    //three numbers and the row snapshots, and nothing else
    m_frame_cpl = VG75->chars_per_row();
    m_frame_lps = VG75->rows_per_screen();
    m_frame_h   = VG75->char_height();
    m_have_frame = true;
    screen_valid = false;       //The render thread draws the frame it has
    was_updated = true;
}

void I8275Display::display_blanked()
{
    clear_rows();
    m_have_frame = false;
    screen_valid = false;
    was_updated = true;
}

void I8275Display::render_all(bool force_render)
{
    if (screen_valid && !force_render) return;

    screen_valid = true;
    was_updated = true;

    if (!m_have_frame || !follow_resolution() || sy != m_surface_sy)
    {
        clear_surface();
        return;
    }

    reset_attr();
    for (unsigned int row = 0; row < m_frame_lps; row++) draw_row(row);
}

//Draws one character row from the bytes DMA delivered for it. The special
//codes are read exactly as I8275::dma_tick() counted them, or the row would be
//drawn to a different length than it was fetched
void I8275Display::draw_row(unsigned int Lin)
{
    const RowSnapshot & r = m_rows[Lin];
    const unsigned int CPL = m_frame_cpl;
    const unsigned int H = m_frame_h;

    if (r.len == 0)
    {
        //DMA delivered nothing for this row, so there is no video at all - not
        //even the cursor, which the controller draws over the picture rather
        //than instead of it
        const uint32_t bg = rgba_colors[0];
        for (unsigned int i = 0; i < H; i++)
        {
            uint32_t * p = reinterpret_cast<uint32_t *>(
                static_cast<uint8_t *>(render_pixels) + (Lin*H + i)*line_bytes);
            for (unsigned int x = 0; x < CPL*6; x++) p[x] = bg;
        }
        return;
    }

    unsigned int P = 0;
    bool Blk = false;                                       //Blacking symbols after a special code until line end
    uint8_t sign;

    for (unsigned int Col = 0; Col < CPL; Col++)
    {
        if (Blk || (P >= r.len)) {
            sign = 0x80;
        } else {
            sign = r.data[P++];
            if ((sign & 0x80) != 0)
            {
                if (sign == 0xF1)
                {
                    Blk = true;
                    sign = 0x80;
                    P++;
                } else {
                    if ((sign & 0x40) == 0)
                    {
                        //Field Attributes
                        if (!AttrDelay)
                        {
                            set_attr(sign);
                            NextAttr = 0;
                        } else {
                            NextAttr = sign;
                        }
                        sign = 0x80;
                    }
                    if (r.transparent)
                    {
                        sign = (P < r.len)?r.data[P++]:0x80;
                    }
                }
            }
        }
        uint8_t C = sign & 0x7F;
        unsigned int Ofs = Col*24;
        for (unsigned int i = 0; i < H; i++)
        {
            uint8_t V;
            unsigned int Adr = Lin*H + i;
            int ii = (int)i - (int)r.add_mode;
            if (ii < 0) ii += (int)H;
            if (ii < 8) {
                V = ~(Font->get_value(r.font_bank + C*8 + static_cast<unsigned int>(ii)));
            } else {
                V = 0;
            }
            if
            (
                (
                    (Col == r.cursor_col) && (Lin == r.cursor_row)
                    &&
                    (
                        ((r.cursor_mode == 0x00) && r.blink) ||             //Blinking block
                        ((r.cursor_mode == 0x10) && (i>7) && r.blink) ||    //Blinking underline
                        (r.cursor_mode == 0x20) ||                          //Non-blinking block
                        ((r.cursor_mode == 0x30) && (i>7))                  //Non-blinking underline
                    )
                )
                || (FAReverse && (sign != 0x80))
                || (FABlink && r.blink_char && (sign != 0x80))              //Attributes are always black?
                || (FAUnder && (i>7))
            ) V = ~V;
            //Index 0 of the palette is black, which is what a zero bit produced
            //before through MapRGB(0,0,0)
            const uint32_t fg = rgba_colors[FAColor];
            const uint32_t bg = rgba_colors[0];
            for (unsigned int k = 0; k <6; k++)
            {
                uint8_t c1 = (V >> k) & 1;
                unsigned int p1 = Ofs + (5-k)*4;
                uint8_t * base = static_cast<uint8_t *>(render_pixels) + Adr*line_bytes + p1;
                *(uint32_t*)base = c1 ? fg : bg;
            }
        }
        if (AttrDelay && (NextAttr != 0))
        {
            set_attr(NextAttr);
            NextAttr = 0;
        }
    }
}

ComputerDevice * create_i8275display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8275Display(im, cd);
}
