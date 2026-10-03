// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: КСМ (ДВК character display controller) video output

#include "dvk_ksm_display.h"
#include "emulator/utils.h"
#include "dvk_kgd.h"

#define KSM_COLUMNS     80
#define KSM_ROWS        25
#define KSM_CELL_W      10
#define KSM_CELL_H      11
#define KSM_RING        48          // строк в кольце видеопамяти
#define KSM_ROW_BYTES   128
#define KSM_TEXT        0x2030      // начало кольца относительно C000
#define KSM_STATUS      0x38B0      // служебная строка, F8B0
#define KSM_CURSOR      0x2000      // курсорная плоскость на 8 КБ ниже
#define KSM_KGD_TOP     5           // линия растра КГД, с которой идёт текст

KSMDisplay::KSMDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , i_line(this, im, 8, "line", MODE_R)
{
    m_clocked = true;   // clock() отсчитывает кадры
    sx = KSM_COLUMNS * KSM_CELL_W;
    sy = KSM_ROWS * KSM_CELL_H;
}

emulator::Result KSMDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    m_vram = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("vram").value, false));
    if (m_vram == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{KSMDisplay|" + std::string(QT_TRANSLATE_NOOP("KSMDisplay", "Video memory device is expected")) + "} " + name);

    m_font_rom = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("font").value, false));
    if (m_font_rom == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{KSMDisplay|" + std::string(QT_TRANSLATE_NOOP("KSMDisplay", "Character generator device is expected")) + "} " + name);
    m_font.assign(2048, 0);

    // Графический контроллер на том же мониторе
    const std::string kgd = cd->get_parameter("kgd", false).value;
    if (!kgd.empty()) {
        m_kgd = dynamic_cast<DVKKGD*>(im->dm->get_device_by_name(kgd, false));
        if (m_kgd == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{KSMDisplay|" + std::string(QT_TRANSLATE_NOOP("KSMDisplay", "Graphics controller device is expected")) + "} " + kgd);
        sy = KGD_HEIGHT;
        m_text_top = KSM_KGD_TOP;
    }

    // Цвет люминофора, $RRGGBB
    const unsigned int color = read_confg_value(cd, "color", false, (unsigned int)0x33FF66);
    m_palette[1][0] = (color >> 16) & 0xFF;
    m_palette[1][1] = (color >> 8) & 0xFF;
    m_palette[1][2] = color & 0xFF;

    m_frame_ticks = m_system_clock / 50;
    if (m_frame_ticks == 0) m_frame_ticks = 1;
    return emulator::Result::ok();
}

// Знакогенератор копируется: это ПЗУ, и отрисовке незачем ходить в чужое
// устройство из своего потока. При сбросе, а не в load_config: ПЗУ,
// объявленное после дисплея, к тому времени своего образа ещё не прочло
void KSMDisplay::reset(bool cold)
{
    GenericDisplay::reset(cold);
    if (m_font_rom != nullptr)
        for (unsigned i = 0; i < m_font.size(); i++) m_font[i] = m_font_rom->get_direct(i) & 0xFF;
}

void KSMDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(m_palette, m_colors, 2);
}

void KSMDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void KSMDisplay::clock(unsigned int counter)
{
    m_ticks += counter;
    if (m_ticks >= m_frame_ticks) {
        m_ticks -= m_frame_ticks;
        m_frame++;
        screen_valid = false;
    }
}

void KSMDisplay::render_row(unsigned row, unsigned offset, bool blink)
{
    const uint8_t * vram = m_vram->get_buffer();
    const unsigned size = (unsigned)m_vram->get_size();
    for (unsigned y = 0; y < KSM_CELL_H; y++) {
        uint8_t * base = static_cast<uint8_t*>(render_pixels) + (m_text_top + row * KSM_CELL_H + y) * line_bytes;
        uint32_t * p = reinterpret_cast<uint32_t*>(base);
        const unsigned src = (y > 7) ? offset - KSM_CURSOR : offset;
        const unsigned ra = y % 8;
        for (unsigned x = 0; x < KSM_COLUMNS; x++) {
            const unsigned a = src + x;
            const unsigned chr = (a < size) ? vram[a] : 0;
            uint8_t gfx = m_font[(chr << 3) | ra];
            if ((y > 7 && blink) || (chr < 0x20 && !blink)) gfx = 0;
            for (int i = 6; i >= 0; i--) *p++ = m_colors[(gfx >> i) & 1];
            *p++ = m_colors[0];
            *p++ = m_colors[0];
            *p++ = m_colors[0];
        }
    }
}

// Картинка КГД поверх текста: горящая точка КГД - две горящие точки растра
void KSMDisplay::render_graphics()
{
    for (unsigned y = 0; y < KGD_HEIGHT && y < sy; y++) {
        uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + y * line_bytes);
        for (unsigned x = 0; x < KGD_WIDTH; x++)
            if (m_kgd->pixel(x, y)) {
                p[x * 2] = m_colors[1];
                p[x * 2 + 1] = m_colors[1];
            }
    }
}

void KSMDisplay::render_all(MAYBE_UNUSED bool force_render)
{
    if (m_vram != nullptr && render_pixels != nullptr) {
        // Линии растра без текста - над ним и под ним, когда растр КГД выше
        for (unsigned y = 0; y < sy; y++) {
            if (y >= m_text_top && y < m_text_top + KSM_ROWS * KSM_CELL_H) continue;
            uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + y * line_bytes);
            for (unsigned x = 0; x < sx; x++) p[x] = m_colors[0];
        }
        const bool text = (m_kgd == nullptr) || !m_kgd->text_off();
        if (text) {
            const bool blink = (m_frame % 10) > 4;
            const unsigned line = i_line.value & 0xFF;
            render_row(0, KSM_STATUS, blink);
            for (unsigned r = 1; r < KSM_ROWS; r++)
                render_row(r, KSM_TEXT + (((line + r - 1) % KSM_RING) * KSM_ROW_BYTES), blink);
        } else {
            for (unsigned y = m_text_top; y < m_text_top + KSM_ROWS * KSM_CELL_H; y++) {
                uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + y * line_bytes);
                for (unsigned x = 0; x < sx; x++) p[x] = m_colors[0];
            }
        }
        if (m_kgd != nullptr && m_kgd->graphics_on()) render_graphics();
    }
    screen_valid = true;
    was_updated = true;
}

std::vector<DeviceFieldInfo> KSMDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"line",  "Первая строка кольца видеопамяти на экране (порт A ВВ55)", false});
    r.push_back({"frame", "Кадров с пуска",                                            false});
    r.push_back({"text",  "Текст экрана, 25 строк (служебная первой)",                 false});
    return r;
}

bool KSMDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "line" || field == "frame") {
        out.numeric = true;
        out.values.push_back(field == "line" ? (i_line.value & 0xFF) : m_frame);
        return true;
    }
    // Экран текстом - то, что сценарий сравнивает вместо снимка. Коды ниже
    // 040 и выше 0177 показываются точкой: кириллица КОИ-7 - те же коды, что
    // латиница, её от латиницы здесь не отличить
    if (field == "text") {
        out.numeric = false;
        out.text.clear();
        const uint8_t * vram = m_vram->get_buffer();
        const unsigned size = (unsigned)m_vram->get_size();
        const unsigned line = i_line.value & 0xFF;
        for (unsigned r = 0; r < KSM_ROWS; r++) {
            const unsigned offset = (r == 0) ? KSM_STATUS : KSM_TEXT + (((line + r - 1) % KSM_RING) * KSM_ROW_BYTES);
            std::string s;
            for (unsigned x = 0; x < KSM_COLUMNS; x++) {
                const unsigned a = offset + x;
                const unsigned c = (a < size) ? vram[a] & 0x7F : 0;
                s += (c >= 0x20 && c < 0x7F) ? (char)c : '.';
            }
            while (!s.empty() && s.back() == ' ') s.pop_back();
            out.text += "\n" + s;
        }
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

void KSMDisplay::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    w.n("ticks", m_ticks);
    w.n("frame", m_frame);
}

emulator::Result KSMDisplay::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.u("ticks", m_ticks);
    r.u("frame", m_frame);
    return emulator::Result::ok();
}

ComputerDevice * create_ksm_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new KSMDisplay(im, cd);
}
