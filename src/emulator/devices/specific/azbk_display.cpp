// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the VGA picture

#include <cstring>

#include "azbk_display.h"
#include "azbk_vga.h"
#include "azbk.h"
#include "emulator/utils.h"

// Раз в столько кадров рисуется все, без сверки строк
static const unsigned int FULL_EVERY = 64;

AZBKDisplay::AZBKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd),
    m_frame(WIDTH * HEIGHT, 0),
    m_native(32768, 0),
    m_keys(HEIGHT)
{
    sx = WIDTH;
    sy = HEIGHT;
    for (LineKey &k : m_keys) k.valid = false;
}

emulator::Result AZBKDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;
    m_video = dynamic_cast<AZBKVga*>(im->dm->get_device_by_name(cd->get_parameter("video").value));
    if (m_video == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);
    m_video->attach_display(this);
    return emulator::Result::ok();
}

void AZBKDisplay::reset(bool cold)
{
    GenericDisplay::reset(cold);
    if (cold) std::fill(m_frame.begin(), m_frame.end(), 0);
    m_full_pass = true;
}

void AZBKDisplay::state_restored()
{
    GenericDisplay::state_restored();
    m_full_pass = true;
    for (LineKey &k : m_keys) k.valid = false;
}

// R5G5B5 в точку поверхности: пять разрядов расширяются до восьми повтором
// старших
void AZBKDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    std::vector<uint8_t> colors(32768 * 3);
    for (unsigned int i = 0; i < 32768; i++) {
        const unsigned int r = (i >> 10) & 31, g = (i >> 5) & 31, b = i & 31;
        colors[i * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
        colors[i * 3 + 1] = (uint8_t)((g << 3) | (g >> 2));
        colors[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
    }
    vr.FillRGB(reinterpret_cast<const uint8_t (*)[3]>(colors.data()), m_native.data(), 32768);
}

bool AZBKDisplay::native_frame_rate(uint64_t &num, uint64_t &den)
{
    return reduced_rate(65000000ull, AZBKVga::DOTS_PER_FRAME, num, den);
}

void AZBKDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

//-------------------------------------------------------------- строка ---

// Слово рулона, с которого начинается строка слоя
static unsigned int row_word(const AZBKLineState &s, unsigned int layer, unsigned int row)
{
    const unsigned int line_words = 32u << ((s.mode >> 3) & 3);
    unsigned int roll = (s.mode >> 12) & 15;
    if (roll > 11) roll = 11;
    const unsigned int roll_lines = AZBKVga::ROLL_WORDS[roll] / line_words;
    return s.pages[layer] * (AZBK::PAGE_BYTES / 2) + line_words * ((row + s.vscroll[layer]) % roll_lines);
}

void AZBKDisplay::make_key(unsigned int line, LineKey &key) const
{
    AZBKLineState s;
    m_video->line_state(s);
    memset(&key, 0, sizeof(key));
    key.params[0] = s.mode;
    for (unsigned int i = 0; i < 3; i++) {
        key.params[1 + i] = s.pages[i];
        key.params[4 + i] = s.vscroll[i];
        key.params[7 + i] = s.hscroll[i];
    }
    key.params[10] = s.legacy_pal;
    key.params[11] = s.quarter ? 1 : 0;
    key.params[12] = m_video->palette_version();
    const AZBK * board = m_video->board();
    const unsigned int layers = (s.mode & 4) ? 3 : 1;
    const unsigned int row = line / (((s.mode >> 9) & 3) + 1);
    for (unsigned int l = 0; l < layers; l++) {
        // Буфер строки - 256 слов, до двух страниц
        const unsigned int first = row_word(s, l, row) * 2 / AZBK::PAGE_BYTES;
        for (unsigned int p = first; p <= first + 1 && key.count < 6; p++) {
            key.pages[key.count] = p;
            key.gens[key.count] = board->page_generation(p);
            key.count++;
        }
    }
    key.valid = true;
}

static bool same_key(const AZBKDisplay * , const void * a, const void * b, size_t size)
{
    return memcmp(a, b, size) == 0;
}

void AZBKDisplay::render_line15(unsigned int line, uint16_t * out) const
{
    AZBKLineState s;
    m_video->line_state(s);
    const unsigned int yrep = ((s.mode >> 9) & 3) + 1;
    const unsigned int row = line / yrep;
    const uint16_t * pal = m_video->palette();

    // Бит 9 177664 = 0 у БК: видна только верхняя четверть экрана
    if (s.quarter && row >= 64) {
        for (unsigned int x = 0; x < WIDTH; x++) out[x] = 0;
        return;
    }

    const unsigned int bpp_code = s.mode & 3;
    const unsigned int bpp = 1u << bpp_code;
    const unsigned int per_word = 16 / bpp;
    const unsigned int mask = (1u << bpp) - 1;
    const unsigned int xrep = 1u << ((s.mode >> 6) & 3);
    const bool layered = (s.mode & 4) != 0;
    const unsigned int layers = layered ? 3 : 1;

    const AZBK * board = m_video->board();
    const uint8_t * mem = board->memory();
    const unsigned int size = board->memory_size();

    uint16_t buf[3][256];
    for (unsigned int l = 0; l < layers; l++) {
        const unsigned int a = row_word(s, l, row) * 2;
        for (unsigned int i = 0; i < 256; i++) {
            const unsigned int b = a + i * 2;
            buf[l][i] = (b + 1 < size) ? (uint16_t)(mem[b] | (mem[b + 1] << 8)) : 0;
        }
    }

    const unsigned int points = WIDTH / xrep;
    uint16_t * p = out;
    for (unsigned int sp = 0; sp < points; sp++) {
        const unsigned int k = sp / per_word;
        const unsigned int shift = (sp % per_word) * bpp;
        unsigned int cell;
        if (!layered) {
            const unsigned int v = (buf[0][(s.hscroll[0] + k) & 255] >> shift) & mask;
            switch (bpp_code) {
            case 0:  cell = 336 + v; break;
            case 1:  cell = 256 + 4 * s.legacy_pal + v; break;
            case 2:  cell = 320 + v; break;
            default: cell = v; break;
            }
        } else {
            unsigned int v[3];
            for (unsigned int l = 0; l < 3; l++) v[l] = (buf[l][(s.hscroll[l] + k) & 255] >> shift) & mask;
            if (bpp_code == 0) {
                // Плановый режим: три бита из трех слоев, верхний - старший;
                // верхняя половина 16-цветной палитры
                cell = 328 + ((v[0] << 2) | (v[1] << 1) | v[2]);
            } else {
                unsigned int l = 0;
                while (l < 2 && v[l] == 0) l++;
                switch (bpp_code) {
                case 1:  cell = 256 + 4 * l + v[l]; break;
                case 2:  cell = 16 * l + v[l]; break;
                default: cell = v[l]; break;
                }
            }
        }
        const uint16_t c = pal[cell < AZBKVga::PALETTE_SIZE ? cell : 0];
        for (unsigned int r = 0; r < xrep; r++) *p++ = c;
    }
}

void AZBKDisplay::put_line(unsigned int line)
{
    if (!has_valid_renderer() || render_pixels == nullptr) return;
    if ((int)(sx * 4) > line_bytes) return;
    uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + line * line_bytes);
    const uint16_t * f = &m_frame[line * WIDTH];
    for (unsigned int x = 0; x < WIDTH; x++) p[x] = m_native[f[x] & 077777];
}

void AZBKDisplay::draw_line(unsigned int line)
{
    if (m_video == nullptr || line >= HEIGHT) return;
    LineKey key;
    make_key(line, key);
    if (!m_full_pass && m_keys[line].valid && same_key(this, &key, &m_keys[line], sizeof(key))) return;
    m_keys[line] = key;
    render_line15(line, &m_frame[line * WIDTH]);
    m_lines_drawn++;
    compat_lock_guard guard(m_surface_mutex);
    put_line(line);
    screen_valid = true;
}

void AZBKDisplay::frame_done()
{
    m_full_pass = false;
    if (++m_since_full >= FULL_EVERY) {
        m_since_full = 0;
        m_full_pass = true;
    }
    screen_valid = true;
    was_updated = true;
    frame_complete();
}

// Поток отрисовки: поверхность заново из кадра, память AZ не нужна
void AZBKDisplay::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid)
        for (unsigned int line = 0; line < HEIGHT; line++) put_line(line);
    screen_valid = true;
    was_updated = true;
}

//----------------------------------------------------------------- поля ---

std::vector<DeviceFieldInfo> AZBKDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"crc",   "Hash of the whole picture (R5G5B5 of every dot)",       false});
    r.push_back({"rgb",   "Colours 0xRRGGBB of the dots from-to, numbered line * 1024 + x", true});
    r.push_back({"lines", "Lines drawn since power-on (unchanged ones are skipped)", false});
    return r;
}

bool AZBKDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "crc") {
        uint32_t h = 2166136261u;
        for (uint16_t v : m_frame) {
            h = (h ^ (v & 0xFF)) * 16777619u;
            h = (h ^ (v >> 8)) * 16777619u;
        }
        out.numeric = true;
        out.width = 32;
        out.values.push_back(h);
        return true;
    }
    if (field == "rgb") {
        out.numeric = true;
        out.width = 24;
        if (to < from) to = from;
        for (unsigned int i = from; i <= to && i < m_frame.size(); i++) {
            const unsigned int c = m_frame[i];
            const unsigned int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            out.values.push_back((((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2)));
        }
        return true;
    }
    if (field == "lines") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back((unsigned int)m_lines_drawn);
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

ComputerDevice * create_azbk_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AZBKDisplay(im, cd);
}
