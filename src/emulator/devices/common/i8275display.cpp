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

//Цвета ZX Spectrum: те же RGBI, что и у самого Арго, порядок разрядов в
//атрибуте - синий, красный, зелёный, яркость восьмым
static const uint8_t ZX_16Colors[16][3] = { {  0,   0,   0}, {  0,   0, 192}, {192,   0,   0}, {192,   0, 192},
                                            {  0, 192,   0}, {  0, 192, 192}, {192, 192,   0}, {192, 192, 192},
                                            {  0,   0,   0}, {  0,   0, 255}, {255,   0,   0}, {255,   0, 255},
                                            {  0, 255,   0}, {  0, 255, 255}, {255, 255,   0}, {255, 255, 255}
                                          };

I8275Display::I8275Display(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , i_high(this, im, 1, "high", MODE_R)
    , i_palette_page(this, im, 1, "palette_page", MODE_R)
    , i_zx(this, im, 1, "zx", MODE_R)
    , Palette(nullptr)
    , m_have_frame(false)
    , m_frame_cpl(78)
    , m_frame_lps(30)
    , m_frame_h(10)
    , m_surface_sy(0)
{
    sx = 78*6;
    sy = 30*10;
    memset(&rgba_colors, 0, sizeof(rgba_colors));
    memset(&m_font_code, 0, sizeof(m_font_code));
    memset(&m_font_line, 0, sizeof(m_font_line));
    memset(&m_rows, 0, sizeof(m_rows));
    reset_attr();
}

//Порядок разрядов в ответе палитры свой у каждой машины: у Юниора 1 - это
//красный, 2 - зелёный, 4 - синий (сверено с COLOR.ATR), а у рендерера
//наоборот. Параметр rgb называет позиции R, G и B в байте цвета
//Адрес точки в знакогенераторе. Слагаемые не пересекаются по разрядам,
//поэтому сложение здесь - это сборка адреса, а не арифметика
unsigned int I8275Display::font_address(unsigned int code, unsigned int line, unsigned int bank) const
{
    if (!m_font_mapped) return m_font_base + bank + code*8 + line;
    return m_font_base + m_font_code[code & 0xFF] + m_font_line[line & 0x0F] + ((bank != 0)? m_font_high : 0);
}

unsigned int I8275Display::map_color(unsigned int v)
{
    if (RGB[0] > 8) return v & 0x07;
    return  ((v >> RGB[2]) & 0x01)
         | (((v >> RGB[1]) & 0x01) << 1)
         | (((v >> RGB[0]) & 0x01) << 2);
}

void I8275Display::set_attr(uint8_t v, unsigned int palette_base)
{
    //С таблицей цветов атрибутные биты - это адрес, а не набор признаков:
    //подчеркивание и инверсия здесь такие же разряды цвета, как остальные.
    //Мигание у Юниора стоит на разряде 1 (COLOR.ATR: $82 - "белый на черном
    //миг."), а сама палитра его игнорирует - оттого в дампе половина адресов
    //и повторяется
    if (Palette != nullptr)
    {
        FAReverse = false;
        FAUnder = false;
        FABlink = (v & 0x02) != 0;
        const unsigned int idx = (v & 0x01) | ((v >> 1) & 0x0E);
        const unsigned int size = Palette->get_size();
        const unsigned int mask = (size > 0)?(size - 1):0;
        FAColor   = map_color(Palette->get_direct(((idx << 1) | palette_base) & mask));
        FAColorBg = map_color(Palette->get_direct(((idx << 1) + 1 + palette_base) & mask));
        return;
    }

    FAReverse = (v & 0x10) != 0;
    FAUnder = (v & 0x20) != 0;
    FABlink = (v & 0x02) != 0;
    FAColorBg = 0;
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
    FAColorBg = 0;
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

    //Размер объявляется до проверки поверхности, а не после: поверхность
    //меняет размер по этим самым sx/sy (Emulator::render_screen), и машина
    //шире стартовых 78 знакомест - Юниор с его 80 - иначе не получила бы её
    //никогда: рисовать нельзя, потому что поверхность мала, а вырасти она не
    //может, потому что мы не сказали, до чего
    //В режиме ZX знакоместо не 6 точек, а 8, и берёт оно из потока два байта:
    //80 знакомест кадра дают 40 видимых по 8 точек - те самые 320 на 256, в
    //которых 256 на 192 занимает экран Спектрума
    const unsigned int w = m_frame_zx?((cpl/2)*8):(cpl*6);

    if ((w != sx) || (lps*h != sy))
    {
        sx = w;
        sy = lps*h;
        screen_valid = false;
        was_updated = true;
        return false;
    }

    //Поверхность ещё прежнего размера: кадр рисуется на следующем проходе
    if (w > (unsigned int)line_bytes / 4) return false;

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
    vr.FillRGB(ZX_16Colors, zx_colors, 16);
    m_surface_sy = sy;
}

emulator::Result I8275Display::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;
    Memory = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    Font = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(cd->get_parameter("font").value));

    m_font_invert = cd->get_parameter("font_invert", false).value != "0";
    m_font_base = read_confg_value(cd, "font_base", false, (unsigned int)0);
    m_zx_bitmap = read_confg_value(cd, "zx_bitmap", false, (unsigned int)0);
    m_zx_attr   = read_confg_value(cd, "zx_attr", false, (unsigned int)0);
    m_cursor_left = read_confg_value(cd, "cursor_left", false, (unsigned int)0);

    const std::string font_map = cd->get_parameter("font_address", false).value;
    if (!font_map.empty())
    {
        if (font_map.size() != 11)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{I8275Display|" + std::string(QT_TRANSLATE_NOOP("I8275Display", "font_address must be 11 characters long")) + "} " + font_map);
        for (unsigned int i = 0; i < 11; i++)
        {
            const unsigned int bit = 1u << (10 - i);
            const char c = font_map[i];
            if (c >= '0' && c <= '7')
            {
                for (unsigned int v = 0; v < 256; v++)
                    if ((v >> (c - '0')) & 1) m_font_code[v] |= bit;
            } else
            if (c >= 'a' && c <= 'd')
            {
                for (unsigned int v = 0; v < 16; v++)
                    if ((v >> (c - 'a')) & 1) m_font_line[v] |= bit;
            } else
            if (c == 'B')
                m_font_high = bit;
            else
            if (c != '-')
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{I8275Display|" + std::string(QT_TRANSLATE_NOOP("I8275Display", "Unknown font_address character")) + "} " + font_map);
        }
        m_font_mapped = true;
    }
    const std::string palette_name = cd->get_parameter("palette", false).value;
    if (!palette_name.empty())
        Palette = dynamic_cast<ROM*>(im->dm->get_device_by_name(palette_name));
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

void I8275Display::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    //A whole frame as the controller delivered it, row by row. It is rebuilt
    //within 20 ms of running, but a screenshot taken right after a restore
    //would otherwise come out blank
    w.b("have_frame", m_have_frame);
    w.n("frame_cpl", m_frame_cpl);
    w.n("frame_lps", m_frame_lps);
    w.n("frame_h", m_frame_h);
    w.b("attr_delay", AttrDelay);
    w.b("fa_reverse", FAReverse);
    w.b("fa_under", FAUnder);
    w.b("fa_blink", FABlink);
    w.n("fa_color", FAColor);
    w.n("fa_color_bg", FAColorBg);
    w.u("next_attr", NextAttr, 8);

    if (!m_have_frame) return;
    const unsigned int rows = (m_frame_lps < I8275D_MAX_ROWS) ? m_frame_lps : I8275D_MAX_ROWS;
    for (unsigned int i = 0; i < rows; i++)
    {
        const RowSnapshot &s = m_rows[i];
        w.push(("row" + std::to_string(i)).c_str());
        w.n("len", s.len);
        w.n("font_bank", s.font_bank);
        w.n("palette_base", s.palette_base);
        w.n("cursor_col", s.cursor_col);
        w.n("cursor_row", s.cursor_row);
        w.n("cursor_mode", s.cursor_mode);
        w.n("add_mode", s.add_mode);
        w.b("transparent", s.transparent);
        w.b("blink", s.blink);
        w.b("blink_char", s.blink_char);
        if (s.len > 0) w.hex("data", s.data, s.len);
        w.pop();
    }
}

emulator::Result I8275Display::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.b("have_frame", m_have_frame);
    r.u("frame_cpl", m_frame_cpl);
    r.u("frame_lps", m_frame_lps);
    r.u("frame_h", m_frame_h);
    r.b("attr_delay", AttrDelay);
    r.b("fa_reverse", FAReverse);
    r.b("fa_under", FAUnder);
    r.b("fa_blink", FABlink);
    r.u("fa_color", FAColor);
    r.u("fa_color_bg", FAColorBg);
    r.u("next_attr", NextAttr);

    if (!m_have_frame) return emulator::Result::ok();
    const unsigned int rows = (m_frame_lps < I8275D_MAX_ROWS) ? m_frame_lps : I8275D_MAX_ROWS;
    for (unsigned int i = 0; i < rows; i++)
    {
        RowSnapshot &s = m_rows[i];
        const StateReader sr = r.sub(("row" + std::to_string(i)).c_str());
        sr.u("len", s.len);
        sr.u("font_bank", s.font_bank);
        sr.u("palette_base", s.palette_base);
        sr.u("cursor_col", s.cursor_col);
        sr.u("cursor_row", s.cursor_row);
        sr.u("cursor_mode", s.cursor_mode);
        sr.u("add_mode", s.add_mode);
        sr.b("transparent", s.transparent);
        sr.b("blink", s.blink);
        sr.b("blink_char", s.blink_char);
        if (s.len > 0 && s.len <= I8275_MAX_ROW) sr.hex("data", s.data, s.len);
    }
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
    //Обе модовые линии снимаются здесь же, на потоке эмуляции: страница
    //палитры - это разряд 7 её адреса, банк знакогенератора - разряд 6
    r.palette_base = ((i_high.linked > 0)?((i_high.value & 1) << 6):0)
                   | ((i_palette_page.linked > 0)?((i_palette_page.value & 1) << 7):0);
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
    m_frame_zx  = (i_zx.linked > 0) && ((i_zx.value & 1) != 0) && (m_zx_bitmap != m_zx_attr);
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
    //Кадр начинается с "атрибутов нет", а это нулевая ячейка палитры текущей
    //страницы, а не безусловно белое по черному
    if (Palette != nullptr) set_attr(0x80, m_rows[0].palette_base);
    for (unsigned int row = 0; row < m_frame_lps; row++)
    {
        if (m_frame_zx) draw_row_zx(row); else draw_row(row);
    }
}

// Строка кадра в режиме ZX Spectrum.
//
// Как это устроено - разобрано по ZX.COM и проверено работающим ПЗУ Спектрума;
// листа схемы с этим режимом нет.
//
// ZX.COM выкладывает для ВГ75 строку из 82 байт: восемь нулей, атрибут поля,
// 32 пары, атрибут поля, восемь нулей. Атрибуты поля позиций не занимают
// (прозрачный режим), так что на строку приходится 80 байт потока. Берутся они
// по два: получается 40 знакомест по 8 точек - 4 бордюра, 32 столбца Спектрума
// и снова 4 бордюра, то есть 256 точек в поле шириной 320. Строк 16 по 16
// растровых - 12 из них рабочие, это 192 строки Спектрума.
//
// Пара - это младший и старший байты адреса атрибута Спектрума. Старший это
// $58, $59 или $5A, то есть треть экрана; младший, точнее его разряды, машина
// раскладывает тем же проводом, которым адресует знакогенератор в обычном
// режиме ($60 в разряды 7-6, $1F в разряды 4-0), а свободный разряд 5 берёт
// себе разряд 3 счётчика растровых строк. Тот же младший байт плюс разряды
// 0-2 счётчика строк и треть дают адрес байта битовой карты. Отсюда и всё
// остальное сходится: 4 знакоместа ВГ75 на строку, 32 пары на 32 столбца,
// 16 растровых строк на два знакоместа Спектрума
void I8275Display::draw_row_zx(unsigned int Lin)
{
    const RowSnapshot & r = m_rows[Lin];
    const unsigned int CPL = m_frame_cpl;
    const unsigned int H = m_frame_h;
    const unsigned int cells = CPL / 2;

    //Байты потока без атрибутов поля: их в прозрачном режиме на экране нет
    uint8_t s[I8275_MAX_ROW];
    unsigned int n = 0;
    for (unsigned int p = 0; p < r.len && n < CPL; p++)
    {
        const uint8_t v = r.data[p];
        if ((v & 0x80) != 0 && (v & 0x40) == 0) continue;   //Field attribute
        s[n++] = v;
    }
    while (n < CPL) s[n++] = 0;

    for (unsigned int c = 0; c < cells; c++)
    {
        const unsigned int A = s[c*2];
        const unsigned int B = s[c*2 + 1];
        //Бордюр: пара, которая не указывает в область атрибутов
        const bool border = (B < 0x58) || (B > 0x5A);
        const unsigned int T = border?0:(B - 0x58);
        const unsigned int Ofs = c*32;

        for (unsigned int i = 0; i < H; i++)
        {
            const unsigned int low = ((A & 0x60) << 1) | ((i >> 3) << 5) | (A & 0x1F);
            uint8_t V = 0;
            uint32_t fg = zx_colors[0], bg = zx_colors[0];
            if (!border)
            {
                V = (uint8_t)Font->get_value(m_zx_bitmap + (T << 11) + ((i & 7) << 8) + low);
                const unsigned int att = Font->get_value(m_zx_attr + (T << 8) + low);
                const unsigned int bright = (att & 0x40)?8:0;
                fg = zx_colors[(att & 0x07) | bright];
                bg = zx_colors[((att >> 3) & 0x07) | bright];
                if ((att & 0x80) != 0 && r.blink_char) { const uint32_t t = fg; fg = bg; bg = t; }
            }
            uint8_t * base = static_cast<uint8_t *>(render_pixels) + (Lin*H + i)*line_bytes + Ofs;
            for (unsigned int k = 0; k < 8; k++)
                *(uint32_t*)(base + (7-k)*4) = ((V >> k) & 1)?fg:bg;
        }
    }
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
        //Знакоместо, на котором видео погашено: позиция атрибута поля в
        //непрозрачном режиме, хвост строки после специального кода и всё, что
        //ПДП не доставил. Рисовать его кодом 0 знакогенератора нельзя: у
        //Юниора там настоящая буква (русские буквы лежат ниже $20), и она
        //вылезала на экран в начале строки состояния МОНИТОРа
        bool blanked = false;
        if (Blk || (P >= r.len)) {
            sign = 0x80;
            blanked = true;
        } else {
            sign = r.data[P++];
            if ((sign & 0x80) != 0)
            {
                if (sign == 0xF1)
                {
                    Blk = true;
                    sign = 0x80;
                    blanked = true;
                    P++;
                } else {
                    if ((sign & 0x40) == 0)
                    {
                        //Field Attributes
                        if (!AttrDelay)
                        {
                            set_attr(sign, r.palette_base);
                            NextAttr = 0;
                        } else {
                            NextAttr = sign;
                        }
                        sign = 0x80;
                        blanked = true;
                    }
                    if (r.transparent)
                    {
                        //В прозрачном режиме атрибут позиции не занимает:
                        //место отдаётся следующему символу
                        blanked = (P >= r.len);
                        sign = blanked?0x80:r.data[P++];
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
            if ((ii < 8) && !blanked) {
                V = Font->get_value(font_address(C, static_cast<unsigned int>(ii), r.font_bank));
                if (m_font_invert) V = ~V;
            } else {
                V = 0;
            }
            if
            (
                (
                    (Col + m_cursor_left == r.cursor_col) && (Lin == r.cursor_row)
                    &&
                    (
                        ((r.cursor_mode == 0x00) && r.blink) ||             //Blinking block
                        ((r.cursor_mode == 0x10) && (i>7) && r.blink) ||    //Blinking underline
                        (r.cursor_mode == 0x20) ||                          //Non-blinking block
                        ((r.cursor_mode == 0x30) && (i>7))                  //Non-blinking underline
                    )
                )
                || (FAReverse && !blanked)
                || (FABlink && r.blink_char && !blanked)                    //Погашенные знакоместа не мигают и не инвертируются
                || (FAUnder && (i>7))
            ) V = ~V;
            //Index 0 of the palette is black, which is what a zero bit produced
            //before through MapRGB(0,0,0)
            const uint32_t fg = rgba_colors[FAColor];
            const uint32_t bg = rgba_colors[FAColorBg];
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
            set_attr(NextAttr, r.palette_base);
            NextAttr = 0;
        }
    }
}

ComputerDevice * create_i8275display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8275Display(im, cd);
}
