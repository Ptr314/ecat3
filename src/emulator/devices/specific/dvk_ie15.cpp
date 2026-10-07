// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 15ИЭ-00-013 terminal (ДВК-1, ДВК-2) and its 15ВВВ keyboard

#include <cstring>

#include "dvk_ie15.h"
#include "emulator/utils.h"

#define IE15_CELL_W     10
#define IE15_CELL_H     11
#define IE15_SPACE      0x20

#define CALLBACK_RXD    1

//--------------------------- Клавиатура 15ВВВ ------------------------------//

static const char * const IE15_SWITCHES[] = {"key_dup", "key_lin", "key_red", "key_pch", "key_sdv"};

IE15Keyboard::IE15Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    MapKeyboard(im, cd)
{}

emulator::Result IE15Keyboard::load_config(SystemData *sd)
{
    emulator::Result res = MapKeyboard::load_config(sd);
    if (!res) return res;
    // Переключатели в таблице клавиш не описаны: она общая с Иришей, где
    // они никуда не подключены
    for (size_t i = 0; i < sizeof(IE15_SWITCHES) / sizeof(IE15_SWITCHES[0]); i++)
        register_key_id(IE15_SWITCHES[i], KEY_ROLE_TOGGLE);
    return emulator::Result::ok();
}

// Переключатели - на терминале, не в ДВК: сброс машины их не трогает, при
// включении питания терминал на линии и в дуплексе
void IE15Keyboard::reset(bool cool)
{
    MapKeyboard::reset(cool);
    if (cool) {
        for (size_t i = 0; i < sizeof(IE15_SWITCHES) / sizeof(IE15_SWITCHES[0]); i++)
            set_toggled(IE15_SWITCHES[i], false);
        set_toggled("key_lin", true);
        set_toggled("key_dup", true);
    }
}

void IE15Keyboard::send_key(unsigned int value, bool alt)
{
    MapKeyboard::send_key(value, alt);
    if (m_terminal != nullptr) m_terminal->key_code((uint8_t)(value & 0x7F), m_dk);
}

static bool is_cursor_id(const std::string &id)
{
    return id == "key_up" || id == "key_down" || id == "key_left" || id == "key_right" || id == "key_home";
}

void IE15Keyboard::send_key_id(const std::string &id, bool press)
{
    m_dk = is_cursor_id(id);
    MapKeyboard::send_key_id(id, press);
    m_dk = false;
}

// Backspace хоста - ЗБ ($177): общая с Иришей раскладка даёт ему $08, а RT-11
// и мониторы ДВК стирают знак по DEL
void IE15Keyboard::key_down(unsigned int key)
{
    if (key == EmuKey::Backspace) {
        send_key_id("key_bksp", true);
        note_id("key_bksp", true);
        return;
    }
    m_dk = (key == EmuKey::Up || key == EmuKey::Down || key == EmuKey::Left
            || key == EmuKey::Right || key == EmuKey::Home);
    MapKeyboard::key_down(key);
    m_dk = false;
}

void IE15Keyboard::key_up(unsigned int key)
{
    if (key == EmuKey::Backspace) {
        send_key_id("key_bksp", false);
        note_id("key_bksp", false);
        return;
    }
    MapKeyboard::key_up(key);
}

std::vector<Keyboard::Indicator> IE15Keyboard::indicators() const
{
    std::vector<Indicator> r = MapKeyboard::indicators();
    std::string lower;
    for (size_t i = 0; i < m_key_roles.size(); i++)
        if (m_key_roles[i].second == KEY_ROLE_CASE_LOWER) lower = m_key_roles[i].first;
    Indicator nr = {"led_nr", m_case_lower, lower};
    Indicator prd = {"led_prd", true, ""};
    r.push_back(nr);
    r.push_back(prd);
    for (size_t i = 0; i < sizeof(IE15_SWITCHES) / sizeof(IE15_SWITCHES[0]); i++) {
        const std::string key = IE15_SWITCHES[i];
        Indicator s = {"led_" + key.substr(4), toggled(key), key};
        r.push_back(s);
    }
    return r;
}

ComputerDevice * create_ie15_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IE15Keyboard(im, cd);
}

//--------------------------- Терминал 15ИЭ ---------------------------------//

IE15Terminal::IE15Terminal(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , i_txd(this, im, 8, "txd", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
{
    m_clocked = true;   // передача в ЭВМ и мигание курсора
    sx = COLUMNS * IE15_CELL_W;
    sy = ROWS * IE15_CELL_H;
    memset(m_screen, IE15_SPACE, sizeof(m_screen));
}

emulator::Result IE15Terminal::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    m_font_rom = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("font").value, false));
    if (m_font_rom == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{IE15Terminal|" + std::string(QT_TRANSLATE_NOOP("IE15Terminal", "Character generator device is expected")) + "} " + name);
    m_font.assign(2048, 0);

    const std::string kbd = cd->get_parameter("keyboard", false).value;
    if (!kbd.empty()) {
        m_keyboard = dynamic_cast<IE15Keyboard*>(im->dm->get_device_by_name(kbd, false));
        if (m_keyboard == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{IE15Terminal|" + std::string(QT_TRANSLATE_NOOP("IE15Terminal", "Keyboard of type ie15-keyboard is expected")) + "} " + kbd);
        m_keyboard->set_terminal(this);
    }

    // Цвет люминофора, $RRGGBB
    const unsigned int color = read_confg_value(cd, "color", false, (unsigned int)0x33FF66);
    m_palette[1][0] = (color >> 16) & 0xFF;
    m_palette[1][1] = (color >> 8) & 0xFF;
    m_palette[1][2] = color & 0xFF;

    m_baud = read_confg_value(cd, "baud", false, (unsigned int)9600);
    if (m_baud == 0) m_baud = 9600;
    // 10 посылок на символ (8N1)
    m_char_ticks = (uint64_t)m_system_clock * 10 / m_baud;
    if (m_char_ticks == 0) m_char_ticks = 1;
    m_frame_ticks = m_system_clock / 50;
    if (m_frame_ticks == 0) m_frame_ticks = 1;
    return emulator::Result::ok();
}

// Знакогенератор копируется при сбросе: ПЗУ, объявленное после терминала, к
// load_config своего образа ещё не прочло
void IE15Terminal::reset(bool cold)
{
    GenericDisplay::reset(cold);
    if (m_font_rom != nullptr)
        for (unsigned i = 0; i < m_font.size(); i++) m_font[i] = m_font_rom->get_direct(i) & 0xFF;
    // Терминал - отдельный блок: сброс ДВК его экран не трогает
    if (cold) {
        memset(m_screen, IE15_SPACE, sizeof(m_screen));
        m_row = m_col = 0;
        m_shift_out = false;
        m_esc = 0;
        m_tx_queue.clear();
        m_tx_left = 0;
        compat_lock_guard lock(m_key_mutex);
        m_key_queue.clear();
    }
}

void IE15Terminal::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(m_palette, m_colors, 2);
}

void IE15Terminal::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void IE15Terminal::clock(unsigned int counter)
{
    for (;;) {
        unsigned int c;
        {
            compat_lock_guard lock(m_key_mutex);
            if (m_key_queue.empty()) break;
            c = m_key_queue.front();
            m_key_queue.pop_front();
        }
        keyboard_byte((uint8_t)(c & 0xFF), (c & 0x100) != 0);
    }

    m_ticks += counter;
    if (m_ticks >= m_frame_ticks) {
        m_ticks -= m_frame_ticks;
        m_frame++;
        screen_valid = false;
    }

    if (m_tx_left > 0) {
        if (m_tx_left > counter) { m_tx_left -= counter; return; }
        m_tx_left = 0;
        const uint8_t b = m_tx_queue.front();
        m_tx_queue.pop_front();
        m_sent++;
        i_txd.change(b);
    }
    if (m_tx_left == 0 && !m_tx_queue.empty()) m_tx_left = m_char_ticks;
}

void IE15Terminal::transmit(uint8_t c)
{
    m_tx_queue.push_back(c);
    if (m_tx_left == 0) m_tx_left = m_char_ticks;
}

void IE15Terminal::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_RXD) {
        m_received++;
        // «Местный» терминал принятое не показывает
        if (m_keyboard == nullptr || m_keyboard->online()) process((uint8_t)(new_value & 0x7F));
    }
}

void IE15Terminal::key_code(uint8_t c, bool dk)
{
    compat_lock_guard lock(m_key_mutex);
    m_key_queue.push_back(c | (dk ? 0x100u : 0u));
}

void IE15Terminal::keyboard_byte(uint8_t c, bool dk)
{
    // Стрелки и «домой» блока курсора - ESC-последовательности VT52
    std::string out;
    switch (dk ? c : 0) {
        case 0x1C: out = "\x1B" "A"; break;
        case 0x1D: out = "\x1B" "B"; break;
        case 0x19: out = "\x1B" "C"; break;
        case 0x1A: out = "\x1B" "D"; break;
        case 0x08: out = "\x1B" "H"; break;
        default:   out = std::string(1, (char)c); break;
    }
    const bool online = (m_keyboard == nullptr) || m_keyboard->online();
    const bool echo = !online || (m_keyboard != nullptr && !m_keyboard->duplex());
    for (size_t i = 0; i < out.size(); i++) {
        if (online) transmit((uint8_t)out[i]);
        if (echo) process((uint8_t)out[i]);
    }
}

void IE15Terminal::scroll()
{
    memmove(m_screen[0], m_screen[1], (ROWS - 1) * COLUMNS);
    memset(m_screen[ROWS - 1], IE15_SPACE, COLUMNS);
}

void IE15Terminal::clear_screen(unsigned from_row, unsigned from_col)
{
    memset(&m_screen[from_row][from_col], IE15_SPACE, COLUMNS - from_col);
    for (unsigned r = from_row + 1; r < ROWS; r++) memset(m_screen[r], IE15_SPACE, COLUMNS);
}

// Знак КОИ-7: во втором наборе (SO) коды 100-177 - кириллица, в
// знакогенераторе она на 200 выше (КОИ-8)
void IE15Terminal::put_char(uint8_t c)
{
    uint8_t g = c;
    if (m_shift_out && c >= 0x40) g = c | 0x80;
    m_screen[m_row][m_col] = g;
    if (m_col < COLUMNS - 1) m_col++;
}

void IE15Terminal::process(uint8_t c)
{
    if (m_esc == 2) { m_esc_row = (c >= 0x20) ? c - 0x20 : 0; m_esc = 3; return; }
    if (m_esc == 3) {
        const unsigned col = (c >= 0x20) ? c - 0x20 : 0;
        if (m_esc_row < ROWS) m_row = m_esc_row;
        m_col = (col < COLUMNS) ? col : COLUMNS - 1;
        m_esc = 0;
        return;
    }
    if (m_esc == 1) {
        m_esc = 0;
        switch (c) {
            case 'A': if (m_row > 0) m_row--; break;
            case 'B': if (m_row < ROWS - 1) m_row++; break;
            case 'C': if (m_col < COLUMNS - 1) m_col++; break;
            case 'D': if (m_col > 0) m_col--; break;
            case 'H': m_row = m_col = 0; break;
            case 'J': clear_screen(m_row, m_col); break;
            case 'K': memset(&m_screen[m_row][m_col], IE15_SPACE, COLUMNS - m_col); break;
            case 'Y': m_esc = 2; break;
            case 'Z': transmit(0x1B); transmit('/'); transmit('K'); break;
            default: break;     // = > F G и прочее
        }
        return;
    }

    switch (c) {
        case 0x07: m_bells++; break;
        case 0x08: if (m_col > 0) m_col--; break;
        case 0x09: m_col = (m_col | 7) + 1; if (m_col > COLUMNS - 1) m_col = COLUMNS - 1; break;
        case 0x0A: if (m_row < ROWS - 1) m_row++; else scroll(); break;
        case 0x0D: m_col = 0; break;
        case 0x0E: m_shift_out = true; break;
        case 0x0F: m_shift_out = false; break;
        case 0x1B: m_esc = 1; break;
        default:
            if (c >= 0x20 && c < 0x7F) put_char(c);
            break;
    }
}

void IE15Terminal::render_all(MAYBE_UNUSED bool force_render)
{
    if (render_pixels != nullptr && !m_font.empty()) {
        // Курсор - мигающая черта под знаком, 2,5 раза в секунду
        const bool cursor_on = (m_frame % 20) < 10;
        for (unsigned r = 0; r < ROWS; r++)
            for (unsigned y = 0; y < IE15_CELL_H; y++) {
                uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + (r * IE15_CELL_H + y) * line_bytes);
                for (unsigned x = 0; x < COLUMNS; x++) {
                    uint8_t gfx = (y < 8) ? m_font[(m_screen[r][x] << 3) | y] : 0;
                    if (y == 9 && cursor_on && r == m_row && x == m_col) gfx = 0xFE;
                    for (int i = 7; i >= 1; i--) *p++ = m_colors[(gfx >> i) & 1];
                    *p++ = m_colors[0];
                    *p++ = m_colors[0];
                    *p++ = m_colors[0];
                }
            }
    }
    screen_valid = true;
    was_updated = true;
}

std::vector<DeviceFieldInfo> IE15Terminal::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"text",     "Текст экрана, 24 строки (кириллица - в UTF-8)", false});
    r.push_back({"cursor",   "Строка и столбец курсора",                      false});
    r.push_back({"received", "Байтов принято от ЭВМ",                         false});
    r.push_back({"sent",     "Байтов передано в ЭВМ",                         false});
    r.push_back({"bells",    "Звонков (код 07)",                              false});
    return r;
}

bool IE15Terminal::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "cursor") {
        out.numeric = true;
        out.values.push_back(m_row);
        out.values.push_back(m_col);
        return true;
    }
    if (field == "received" || field == "sent" || field == "bells") {
        out.numeric = true;
        out.values.push_back(field == "received" ? m_received : field == "sent" ? m_sent : m_bells);
        return true;
    }
    if (field == "text") {
        // Кириллица знакогенератора, КОИ-8: строчные с 300, заглавные с 340
        static const char * const CYR[64] = {
            "ю","а","б","ц","д","е","ф","г","х","и","й","к","л","м","н","о",
            "п","я","р","с","т","у","ж","в","ь","ы","з","ш","э","щ","ч","ъ",
            "Ю","А","Б","Ц","Д","Е","Ф","Г","Х","И","Й","К","Л","М","Н","О",
            "П","Я","Р","С","Т","У","Ж","В","Ь","Ы","З","Ш","Э","Щ","Ч","Ъ"};
        out.numeric = false;
        out.text.clear();
        for (unsigned r = 0; r < ROWS; r++) {
            std::string s;
            for (unsigned x = 0; x < COLUMNS; x++) {
                const uint8_t g = m_screen[r][x];
                if (g >= 0xC0) s += CYR[g - 0xC0];
                else if (g >= 0x20 && g < 0x7F) s += (char)g;
                else s += '.';
            }
            while (!s.empty() && s.back() == ' ') s.pop_back();
            out.text += "\n" + s;
        }
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

void IE15Terminal::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    w.array("screen", &m_screen[0][0], ROWS * COLUMNS, COLUMNS);
    w.n("row", m_row);
    w.n("col", m_col);
    w.b("shift_out", m_shift_out);
    w.n("esc", m_esc);
    w.n("esc_row", m_esc_row);
    w.n64("tx_left", m_tx_left);
    std::vector<uint8_t> q(m_tx_queue.begin(), m_tx_queue.end());
    w.n("tx_count", (uint32_t)q.size());
    if (!q.empty()) w.array("tx_queue", q.data(), q.size());
    w.n("ticks", m_ticks);
    w.n("frame", m_frame);
}

emulator::Result IE15Terminal::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.array("screen", &m_screen[0][0], ROWS * COLUMNS);
    r.u("row", m_row);
    r.u("col", m_col);
    r.b("shift_out", m_shift_out);
    r.u("esc", m_esc);
    r.u("esc_row", m_esc_row);
    r.n64("tx_left", m_tx_left);
    uint32_t n = 0;
    m_tx_queue.clear();
    if (r.u("tx_count", n) && n > 0) {
        std::vector<uint8_t> q(n);
        if (r.array("tx_queue", q.data(), n)) m_tx_queue.assign(q.begin(), q.end());
    }
    if (m_row >= ROWS) m_row = ROWS - 1;
    if (m_col >= COLUMNS) m_col = COLUMNS - 1;
    r.u("ticks", m_ticks);
    r.u("frame", m_frame);
    return emulator::Result::ok();
}

std::vector<DeviceCommandInfo> IE15Terminal::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = GenericDisplay::get_device_commands();
    r.push_back({"receive", "\"text\"", "Байты как от ЭВМ, мимо линии (\\n, \\r, \\t, \\ooo)"});
    return r;
}

emulator::Result IE15Terminal::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "receive") {
        const std::vector<std::string> p = split_params(parameters);
        const std::string text = decode_send_text(p.empty() ? std::string() : p[0]);
        for (size_t i = 0; i < text.size(); i++) process((uint8_t)(text[i] & 0x7F));
        return emulator::Result::ok();
    }
    return GenericDisplay::send_command(command, parameters);
}

ComputerDevice * create_ie15_terminal(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IE15Terminal(im, cd);
}
