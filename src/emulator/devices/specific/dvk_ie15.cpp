// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 15ИЭ-00-013 terminal (ДВК-1, ДВК-2): its logic block and 15ВВВ keyboard

#include <cstring>

#include "dvk_ie15.h"
#include "emulator/utils.h"

#define CALLBACK_RXD    1

// Развёртка в тактах МПУ
#define IE15_LINE_TICKS     200     // 1000 точек
#define IE15_HBLANK_TICKS   40      // 200 точек гашения в начале линии
#define IE15_TOTAL_LINES    (28 * 11)
#define IE15_FIRST_LINE     (2 * 11)

#define IE15_TRUE           0x80

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
    // РУС и ЛАТ в рабочем режиме (SA1 в 1-2) только шлют $0E и $0F, как
    // клавиши основного поля, с автоповтором; регистр и лампу ведёт прошивка
    // флагом 4. У Ириши те же клавиши переключают регистр сами, поэтому роли
    // из общей таблицы здесь снимаются
    for (size_t i = 0; i < m_key_roles.size(); )
        if (m_key_roles[i].second == KEY_ROLE_RUS_ON || m_key_roles[i].second == KEY_ROLE_RUS_OFF
            || m_key_roles[i].second == KEY_ROLE_RUS_TOGGLE)
            m_key_roles.erase(m_key_roles.begin() + i);
        else
            i++;
    return emulator::Result::ok();
}

// Переключатели и регистры - на терминале, не в ДВК: тёплый сброс машины их не
// трогает. Регистр РУС/ЛАТ не трогает и холодный: его ставит прошивка (флаг 4,
// IE15IO::reset()). При включении питания терминал на линии, в дуплексе и с
// нажатым РЕД (без него управляющие коды не исполняются, а пишутся на экран)
void IE15Keyboard::reset(bool cool)
{
    const bool rus = rus_mode;
    const bool lower = m_case_lower;
    MapKeyboard::reset(cool);
    set_rus(rus);
    {
        compat_lock_guard lock(m_turn);
        m_caps_id.clear();
    }
    if (!cool) {
        m_case_lower = lower;
    } else {
        for (size_t i = 0; i < sizeof(IE15_SWITCHES) / sizeof(IE15_SWITCHES[0]); i++)
            set_toggled(IE15_SWITCHES[i], false);
        set_toggled("key_lin", true);
        set_toggled("key_dup", true);
        set_toggled("key_red", true);
    }
}

void IE15Keyboard::send_key(unsigned int value, bool alt)
{
    MapKeyboard::send_key(value, alt);
    if (m_terminal != nullptr) m_terminal->key_press(m_token, (uint8_t)(value & 0x7F), m_dk);
}

// Линия ДК: блок курсора (столбцы Y22-Y24 матрицы) и цифровой блок (Y14-Y16),
// MAME ie15_kbd.cpp. По ней прошивка в режиме VT52 отдаёт стрелки как ESC
// A/B/C/D, 026/002/022 - как ESC P/Q/R, цифры блока - кодами дополнительной
// клавиатуры
static bool is_dk_id(const std::string &id)
{
    static const char * const DK[] = {"key_begin", "key_up", "key_left", "key_home", "key_right", "key_del",
                                      "key_down", "key_ins", "key_pgdn", "key_setup_next", "key_pgup"};
    if (id.compare(0, 8, "key_num_") == 0) return true;
    for (size_t i = 0; i < sizeof(DK) / sizeof(DK[0]); i++)
        if (id == DK[i]) return true;
    return false;
}

void IE15Keyboard::send_key_id(const std::string &id, bool press)
{
    compat_lock_guard lock(m_turn);
    send_id(id, press);
}

void IE15Keyboard::send_id(const std::string &id, bool press)
{
    m_dk = is_dk_id(id);
    m_token = "i" + id;
    MapKeyboard::send_key_id(id, press);
    if (!press && m_terminal != nullptr) m_terminal->key_release(m_token);
    m_token.clear();
    m_dk = false;
}

// Переключатели режимов на клавишах хоста F6-F10. Кода у них нет, поэтому и
// места в 15vvv.map (она переводит клавиши в коды); нажатие переключает,
// как щелчок по рисунку, отпускание ничего не делает
static const char * host_switch(unsigned int key)
{
    switch (key) {
        case EmuKey::F6:  return "key_sdv";
        case EmuKey::F7:  return "key_red";
        case EmuKey::F8:  return "key_lin";
        case EmuKey::F9:  return "key_dup";
        case EmuKey::F10: return "key_pch";
        default:          return nullptr;
    }
}

// Backspace хоста - ЗБ ($177): общая с Иришей раскладка даёт ему $08, а RT-11
// и мониторы ДВК стирают знак по DEL
void IE15Keyboard::key_down(unsigned int key)
{
    if (const char * sw = host_switch(key)) {
        set_toggled(sw, !toggled(sw));
        return;
    }
    compat_lock_guard lock(m_turn);
    if (key == EmuKey::Backspace) {
        send_id("key_bksp", true);
        note_id("key_bksp", true);
        return;
    }
    // РУС/ЛАТ хоста - одна клавиша на две: та из них, что сменит регистр,
    // каким его показывает прошивка; держится до отпускания
    if (key == code_ruslat) {
        if (!m_caps_id.empty()) return;
        m_caps_id = rus_mode ? "key_lat" : "key_rus";
        send_id(m_caps_id, true);
        note_id(m_caps_id, true);
        return;
    }
    m_dk = (key == EmuKey::Up || key == EmuKey::Down || key == EmuKey::Left
            || key == EmuKey::Right || key == EmuKey::Home);
    m_token = "h" + std::to_string(key);
    MapKeyboard::key_down(key);
    m_token.clear();
    m_dk = false;
}

void IE15Keyboard::key_up(unsigned int key)
{
    if (host_switch(key) != nullptr) return;
    compat_lock_guard lock(m_turn);
    if (key == EmuKey::Backspace) {
        send_id("key_bksp", false);
        note_id("key_bksp", false);
        return;
    }
    if (key == code_ruslat) {
        if (m_caps_id.empty()) return;
        send_id(m_caps_id, false);
        note_id(m_caps_id, false);
        m_caps_id.clear();
        return;
    }
    MapKeyboard::key_up(key);
    if (m_terminal != nullptr) m_terminal->key_release("h" + std::to_string(key));
}

// Порядок - как у IE15_SWITCHES, разряды SW_*
void IE15Keyboard::toggles_changed()
{
    unsigned v = 0;
    for (size_t i = 0; i < sizeof(IE15_SWITCHES) / sizeof(IE15_SWITCHES[0]); i++)
        if (toggled(IE15_SWITCHES[i])) v |= 1u << i;
    m_switches.store(v, std::memory_order_relaxed);
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

// В 4.4.0 РЕД был наоборот: выключенный значил нормальную работу. Снимок без
// red_on оттуда, его РЕД переворачивается
void IE15Keyboard::save_state(StateWriter &w)
{
    MapKeyboard::save_state(w);
    w.b("red_on", true);
}

emulator::Result IE15Keyboard::load_state(const StateReader &r)
{
    emulator::Result res = MapKeyboard::load_state(r);
    if (!res) return res;
    bool red_on = false;
    if (!r.b("red_on", red_on)) set_toggled("key_red", !toggled("key_red"));
    return emulator::Result::ok();
}

ComputerDevice * create_ie15_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IE15Keyboard(im, cd);
}

//--------------------------- Изображение -----------------------------------//

IE15Display::IE15Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericDisplay(im, cd)
{
    sx = WIDTH;
    sy = HEIGHT;
    for (unsigned i = 0; i < ROWS; i++) m_row_addr[i] = _FFFF;
}

emulator::Result IE15Display::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    m_font_rom = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("font").value, false));
    if (m_font_rom == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{IE15Display|" + std::string(QT_TRANSLATE_NOOP("IE15Display", "Character generator device is expected")) + "} " + name);
    m_font.assign(2048, 0);

    // Цвет люминофора, $RRGGBB
    const unsigned int color = read_confg_value(cd, "color", false, (unsigned int)0x33FF66);
    m_palette[1][0] = (color >> 16) & 0xFF;
    m_palette[1][1] = (color >> 8) & 0xFF;
    m_palette[1][2] = color & 0xFF;
    return emulator::Result::ok();
}

// Знакогенератор копируется при сбросе: ПЗУ, объявленное после изображения, к
// load_config своего образа ещё не прочло
void IE15Display::reset(bool cold)
{
    GenericDisplay::reset(cold);
    if (m_font_rom != nullptr)
        for (unsigned i = 0; i < m_font.size(); i++) m_font[i] = m_font_rom->get_direct(i) & 0xFF;
    if (cold) {
        for (unsigned i = 0; i < ROWS; i++) m_row_addr[i] = _FFFF;
        m_fl7 = false;
    }
}

void IE15Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(m_palette, m_colors, 2);
}

void IE15Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void IE15Display::draw_line(unsigned y, unsigned line, const uint16_t * cells, unsigned row_addr)
{
    if (line == 0 && y / CELL_H < ROWS) {
        m_row_addr[y / CELL_H] = row_addr;
        m_fl7 = (cells[0] & CELL_FL7) != 0;
    }

    compat_lock_guard guard(m_surface_mutex);
    if (!has_valid_renderer() || render_pixels == nullptr || m_vram == nullptr) return;
    if ((int)(WIDTH * 4) > line_bytes || y >= HEIGHT) return;

    // Луч нарисовал линию: render_all() кадр больше не стирает (иначе под
    // -j 8 снимок выходил чёрным)
    screen_valid = true;

    uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + y * line_bytes);
    const unsigned ra = line & 7;
    for (unsigned x = 0; x < 80; x++) {
        const uint16_t cell = cells[x];
        unsigned chr = cell & 0xFF;
        // Служебный знак - разряды 5 и 6 кода нули (ГС, D21): знакогенератор
        // получает ещё и разряд 6, то есть большую латинскую букву, и знак
        // мигает - или не виден вовсе
        const bool ctrl = (chr & 0x60) == 0;
        if (ctrl) chr |= 0x40;
        if (cell & CELL_FL7) chr = (chr & 0x7F) | ((chr & 0x20) << 2);
        const uint8_t gfx = (!(cell & CELL_SHOW) || (ctrl && !(cell & CELL_CTRL))) ? 0 : m_font[(chr << 3) | ra];
        for (int i = 7; i >= 1; i--) *p++ = m_colors[(gfx >> i) & 1];
        *p++ = m_colors[0];
        *p++ = m_colors[0];
        *p++ = m_colors[0];
    }
}

void IE15Display::end_frame()
{
    was_updated = true;
    frame_complete();
}

// Только поверхность, на которой луч ещё не рисовал
void IE15Display::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid && render_pixels != nullptr) {
        for (unsigned y = 0; y < HEIGHT; y++) {
            uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + y * line_bytes);
            for (unsigned x = 0; x < WIDTH; x++) *p++ = m_colors[0];
        }
        screen_valid = true;
        was_updated = true;
    }
}

std::vector<DeviceFieldInfo> IE15Display::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"text",   "Текст экрана, 24 строки, как в последнем кадре (кириллица - в UTF-8)", false});
    r.push_back({"status", "Служебная строка, пусто - погашена", false});
    return r;
}

bool IE15Display::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "text" || field == "status") {
        // Кириллица знакогенератора, КОИ-8: строчные с 300, заглавные с 340
        static const char * const CYR[64] = {
            "ю","а","б","ц","д","е","ф","г","х","и","й","к","л","м","н","о",
            "п","я","р","с","т","у","ж","в","ь","ы","з","ш","э","щ","ч","ъ",
            "Ю","А","Б","Ц","Д","Е","Ф","Г","Х","И","Й","К","Л","М","Н","О",
            "П","Я","Р","С","Т","У","Ж","В","Ь","Ы","З","Ш","Э","Щ","Ч","Ъ"};
        out.numeric = false;
        out.text.clear();
        const unsigned first = (field == "status") ? 0 : 1;
        const unsigned last = (field == "status") ? 1 : ROWS;
        for (unsigned r = first; r < last; r++) {
            std::string s;
            if (m_row_addr[r] != _FFFF && m_vram != nullptr)
                for (unsigned x = 0; x < 80; x++) {
                    uint8_t g = m_vram[(m_row_addr[r] + x) & 0xFFF];
                    if ((g & 0x60) == 0) { s += '.'; continue; }
                    if (m_fl7) g = (uint8_t)((g & 0x7F) | ((g & 0x20) << 2));
                    // Во втором наборе (SO) прошивка хранит коды с разрядом 7:
                    // 240-277 знакогенератора повторяют 040-077
                    if (g >= 0xC0) s += CYR[g - 0xC0];
                    else if ((g & 0x7F) >= 0x20 && (g & 0x7F) < 0x40) s += (char)(g & 0x7F);
                    else if (g >= 0x20 && g < 0x7F) s += (char)g;
                    else s += '.';
                }
            while (!s.empty() && s.back() == ' ') s.pop_back();
            out.text += (field == "status") ? s : "\n" + s;
        }
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

// Откуда показана каждая строка - для полей text и status до первого кадра
void IE15Display::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    w.array("row_addr", m_row_addr, ROWS);
    w.b("fl7", m_fl7);
}

emulator::Result IE15Display::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.array("row_addr", m_row_addr, ROWS);
    r.b("fl7", m_fl7);
    return emulator::Result::ok();
}

ComputerDevice * create_ie15_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IE15Display(im, cd);
}

//--------------------------- Блок логики -----------------------------------//

IE15IO::IE15IO(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_txd(this, im, 8, "txd", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
    , i_beep(this, im, 1, "beep", MODE_W)
{
    m_clocked = true;
    addresable_size = 030;
    can_read = true;
    can_write = true;
}

emulator::Result IE15IO::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    m_display = dynamic_cast<IE15Display*>(im->dm->get_device_by_name(cd->get_parameter("display").value, false));
    if (m_display == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{IE15IO|" + std::string(QT_TRANSLATE_NOOP("IE15IO", "Display of type ie15-display is expected")) + "} " + name);

    m_vram_dev = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("vram").value, false));
    if (m_vram_dev == nullptr || m_vram_dev->get_size() < 0x1000)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{IE15IO|" + std::string(QT_TRANSLATE_NOOP("IE15IO", "Video memory of 4 KB is expected")) + "} " + name);

    const std::string kbd = cd->get_parameter("keyboard", false).value;
    if (!kbd.empty()) {
        m_keyboard = dynamic_cast<IE15Keyboard*>(im->dm->get_device_by_name(kbd, false));
        if (m_keyboard == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{IE15IO|" + std::string(QT_TRANSLATE_NOOP("IE15IO", "Keyboard of type ie15-keyboard is expected")) + "} " + kbd);
        m_keyboard->set_terminal(this);
    }
    return emulator::Result::ok();
}

// Терминал - отдельный блок: тёплый сброс ЭВМ до него не доходит
void IE15IO::reset(bool cold)
{
    m_vram = m_vram_dev->get_buffer();
    m_display->set_vram(m_vram);
    if (!cold) return;

    m_ptr = 0;
    m_latch = false;
    m_video = m_cursor = m_short_beep = m_status_off = m_lat = m_fl7 = false;
    if (m_keyboard != nullptr) m_keyboard->follow_ruslat(m_lat);
    m_pos = m_line = 0;
    m_line_drawn = false;
    m_marker = false;
    m_line_on = false;
    m_draw_x = 0;
    m_row_start = _FFFF;
    memset(m_cells, 0, sizeof(m_cells));
    m_frame = 0;
    m_snow = 0;
    m_rx_char = m_tx_char = 0;
    m_rx_full = m_tx_busy = false;
    m_tx_left = 0;
    m_speed = 0;
    m_feed.clear();
    m_feed_left = 0;
    m_beep_left = m_beep_phase = 0;
    m_beep_level = false;
    i_beep.change(0);
    m_kb_code = 0;
    m_kb_dk = m_kb_ready = false;
    m_held.clear();
    m_keys_held.store(0, std::memory_order_relaxed);
    m_kb_on = false;
    m_kb_time = m_kb_next = m_scan_left = 0;
    compat_lock_guard lock(m_key_mutex);
    m_key_events.clear();
    m_events_waiting.store(0, std::memory_order_release);
}

// Время символа: 10 посылок; скорость 9600 >> код (делитель на ГС, ТО 6.1.3)
uint64_t IE15IO::char_ticks() const
{
    const uint64_t t = (uint64_t)m_system_clock * 10 / (9600u >> (m_speed & 7));
    return t ? t : 1;
}

void IE15IO::receive(uint8_t c)
{
    m_received++;
    if (m_rx_full) m_overruns++;
    m_rx_char = c;
    m_rx_full = true;
}

void IE15IO::key_press(const std::string &token, uint8_t c, bool dk)
{
    compat_lock_guard lock(m_key_mutex);
    KeyEvent e = {token, c, dk, true};
    m_key_events.push_back(e);
    if (token.empty()) {
        e.press = false;
        m_key_events.push_back(e);
    }
    m_events_waiting.store((unsigned)m_key_events.size(), std::memory_order_release);
}

// Отпускание клавиши без кода (Shift, переключатель) ничего не находит
void IE15IO::key_release(const std::string &token)
{
    compat_lock_guard lock(m_key_mutex);
    KeyEvent e = {token, 0, false, false};
    m_key_events.push_back(e);
    m_events_waiting.store((unsigned)m_key_events.size(), std::memory_order_release);
}

void IE15IO::key_events()
{
    std::deque<KeyEvent> ev;
    {
        compat_lock_guard lock(m_key_mutex);
        ev.swap(m_key_events);
        m_events_waiting.store(0, std::memory_order_release);
    }
    for (size_t n = 0; n < ev.size(); n++) {
        const KeyEvent &e = ev[n];
        size_t i = 0;
        while (i < m_held.size() && (m_held[i].released || m_held[i].token != e.token)) i++;
        if (!e.press) {
            if (i < m_held.size()) m_held[i].released = true;
        } else if (i < m_held.size() && !e.token.empty()) {
            // Та же клавиша ещё нажата: другой код - тот же контакт матрицы
            m_held[i].code = e.code;
            m_held[i].dk = e.dk;
        } else {
            HeldKey h = {e.token, e.code, e.dk, false, 0};
            m_held.push_back(h);
        }
    }
    m_keys_held.store((unsigned)m_held.size(), std::memory_order_relaxed);
}

// Счётчик клавиатуры остановился на первой нажатой: её код, флаг, автоповтор
// через секунду
void IE15IO::strobe_first()
{
    m_kb_code = m_held[0].code;
    m_kb_dk = m_held[0].dk;
    m_kb_ready = true;
    m_kb_on = true;
    m_kb_time = 0;
    m_kb_next = m_system_clock;
}

void IE15IO::keyboard(unsigned int counter)
{
    // 40 мс; деление - раз на частоту, а не на каждом такте с нажатой клавишей
    if (m_min_hold_clock != m_system_clock) {
        m_min_hold_clock = m_system_clock;
        m_min_hold = m_system_clock / 25;
    }
    const uint64_t min_hold = m_min_hold;
    m_scan_left = (m_scan_left > counter) ? m_scan_left - counter : 0;
    bool first_gone = false;
    size_t i = 0;
    while (i < m_held.size()) {
        m_held[i].held += counter;
        if (m_held[i].released && m_held[i].held >= min_hold) {
            if (i == 0 && m_kb_on) first_gone = true;
            m_held.erase(m_held.begin() + i);
            m_keys_held.store((unsigned)m_held.size(), std::memory_order_relaxed);
        } else i++;
    }
    // Клавиша отпущена: флаг снят, счётчик снова идёт по матрице (64 позиции
    // на 10 кГц) и следующую нажатую находит не сразу. Без этой паузы клавиша,
    // нажатая в тот же миг, когда отпущена прошлая, при выключенном автоповторе
    // пропадала бы: прошивка не успела бы увидеть флаг снятым
    if (first_gone) {
        m_kb_ready = false;
        m_kb_on = false;
        m_scan_left = (uint64_t)m_system_clock * 64 / 10000;
    }
    if (m_held.empty()) return;
    if (!m_kb_on) {
        if (m_scan_left == 0) strobe_first();
    } else if (!m_held[0].released) {
        m_kb_time += counter;
        if (m_kb_time >= m_kb_next) {
            m_kb_ready = true;
            m_kb_next += m_system_clock / 15;
        }
    }
}

void IE15IO::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_RXD) receive((uint8_t)(new_value & 0xFF));
}

// Линия растра m_line: кончилось её строчное гашение
void IE15IO::begin_line()
{
    const unsigned L = m_line;
    m_marker = (((L + 1) % IE15_TOTAL_LINES) % 11) > 7;
    if (L < IE15_FIRST_LINE || L >= IE15_FIRST_LINE + IE15Display::HEIGHT) return;
    m_line_on = true;
    m_draw_x = 0;
    m_row_start = _FFFF;
    m_cells_on = m_cells_off = false;
}

// Конец линии: луч прошёл её всю
void IE15IO::end_line()
{
    render_to(80);
    m_line_on = false;
    // Межстрочье тёмное и так, снег виден только на линиях знака
    if (m_cells_on && m_cells_off && (m_line - IE15_FIRST_LINE) % IE15Display::CELL_H < 8) m_snow++;
    const unsigned y = m_line - IE15_FIRST_LINE;
    m_display->draw_line(y, y % IE15Display::CELL_H, m_cells, m_row_start);
    if (y + 1 == IE15Display::HEIGHT) {
        m_frame++;
        m_display->end_frame();
    }
}

// Знакоместа до x_end - при нынешних флагах и адресе
void IE15IO::render_to(unsigned int x_end)
{
    if (m_draw_x >= x_end) return;
    const unsigned y = m_line - IE15_FIRST_LINE;
    const bool blink = (m_frame % 10) > 4;
    // Линии 8-10 - межстрочье: там виден только маркер (флаг 1), мигающий.
    // Служебная строка гаснет флагом 3
    uint16_t attr = 0;
    if (!(y < IE15Display::CELL_H && m_status_off) && (y % IE15Display::CELL_H < 8 || (m_cursor && !blink)))
        attr |= IE15Display::CELL_SHOW;
    // РЕД отжат - управляющие коды не исполнены, а записаны в ЗУ: они мигают.
    // При нажатом не видны, в том числе метки служебной строки (MAME
    // показывает их при включённом RED и погашенной служебной строке). Мигают
    // они в фазе с маркером: в ГС обоих гасит одна и та же фаза 5 Гц (yursav,
    // zx-pk.ru, тема 20744)
    if (m_keyboard != nullptr && !m_keyboard->switch_on(IE15Keyboard::SW_RED) && !blink)
        attr |= IE15Display::CELL_CTRL;
    if (m_fl7) attr |= IE15Display::CELL_FL7;
    for (; m_draw_x < x_end; m_draw_x++) {
        // Без показа счётчик стоит, знакоместо тёмное
        if (!m_video) {
            m_cells[m_draw_x] = 0;
            m_cells_off = true;
            continue;
        }
        m_cells_on = true;
        if (m_draw_x == 0 && (attr & IE15Display::CELL_SHOW)) m_row_start = m_ptr;
        m_cells[m_draw_x] = m_vram[m_ptr] | attr;
        m_ptr = (m_ptr + 1) & 0xFFF;
    }
}

// Луч - до нынешнего такта: знакоместо x берётся на такте 40 + 2x
void IE15IO::video_sync()
{
    if (!m_line_on || m_pos <= IE15_HBLANK_TICKS) return;
    const unsigned x = (m_pos - IE15_HBLANK_TICKS + 1) / 2;
    render_to(x < 80 ? x : 80);
}

void IE15IO::clock(unsigned int counter)
{
    if (m_events_waiting.load(std::memory_order_acquire) != 0) key_events();
    if (!m_held.empty() || m_scan_left != 0) keyboard(counter);

    if (m_beep_left != 0) {
        // 2400 Гц: полупериод - 1/4800 секунды
        m_beep_phase += (uint64_t)counter * 4800;
        while (m_beep_phase >= m_system_clock) {
            m_beep_phase -= m_system_clock;
            m_beep_level = !m_beep_level;
            i_beep.change(m_beep_level ? 1 : 0);
        }
        if (m_beep_left > counter) m_beep_left -= counter;
        else {
            m_beep_left = 0;
            m_beep_level = false;
            i_beep.change(0);
        }
    }

    if (m_tx_busy) {
        if (m_tx_left > counter) m_tx_left -= counter;
        else {
            m_tx_busy = false;
            m_tx_left = 0;
            m_sent++;
            i_txd.change(m_tx_char);
        }
    }

    if (!m_feed.empty()) {
        if (m_feed_left > counter) m_feed_left -= counter;
        else {
            receive(m_feed.front());
            m_feed.pop_front();
            m_feed_left = m_feed.empty() ? 0 : char_ticks();
        }
    }

    m_pos += counter;
    for (;;) {
        if (!m_line_drawn && m_pos >= IE15_HBLANK_TICKS) {
            m_line_drawn = true;
            begin_line();
        }
        if (m_pos < IE15_LINE_TICKS) break;
        if (m_line_on) end_line();
        m_pos -= IE15_LINE_TICKS;
        m_line_drawn = false;
        if (++m_line == IE15_TOTAL_LINES) m_line = 0;
    }
}

unsigned int IE15IO::read_flag(unsigned int n)
{
    switch (n) {
        case 0: return m_pos < IE15_HBLANK_TICKS;
        case 1: return m_marker;
        case 2: return m_line >= IE15_FIRST_LINE && m_line < IE15_FIRST_LINE + IE15Display::HEIGHT;
        // Вход флага 3 - выход ФЛ7: прошивка смотрит, стоит ли он, прежде чем
        // поставить или снять
        case 3: return m_fl7;
        case 4: return m_lat;
        default: return 0;
    }
}

void IE15IO::write_flag(unsigned int n, bool v)
{
    if (n == 0 || n == 1 || n == 3 || n == 7) video_sync();
    switch (n) {
        case 0: m_video = v; break;
        case 1: m_cursor = v; break;
        case 2: m_short_beep = v; break;
        case 3: m_status_off = v; break;
        case 4:
            m_lat = v;
            if (m_keyboard != nullptr) m_keyboard->follow_ruslat(v);
            break;
        case 7: m_fl7 = v; break;
        default: break;
    }
}

unsigned int IE15IO::get_value(unsigned int address)
{
    switch (address) {
        case 000: {
            video_sync();
            const uint8_t v = m_vram[m_ptr];
            m_ptr = (m_ptr + 1) & 0xFFF;
            m_latch = false;
            return v;
        }
        case 001: return m_rx_full ? 0 : IE15_TRUE;
        case 006: m_rx_full = false; return m_rx_char;
        case 010: return m_tx_busy ? 0 : IE15_TRUE;
        case 011: return m_kb_code;
        case 012: return get_direct(address);
        case 013: return m_kb_ready ? 0 : IE15_TRUE;
        case 014: case 015: case 016: case 017: return get_direct(address);
        default:
            if (address >= 020 && address < 030) return read_flag(address - 020);
            return _FFFF;
    }
}

// Без побочных действий: ЗУ и приёмник не сдвигаются
unsigned int IE15IO::get_direct(unsigned int address)
{
    const bool kbd = m_keyboard != nullptr;
    switch (address) {
        case 000: return m_vram != nullptr ? m_vram[m_ptr] : _FFFF;
        case 006: return m_rx_char;
        case 012: return (!kbd || m_keyboard->switch_on(IE15Keyboard::SW_RED)) ? IE15_TRUE : 0;
        case 014: return (kbd && m_keyboard->switch_on(IE15Keyboard::SW_SDV)) ? IE15_TRUE : 0;
        case 015: return m_kb_dk ? IE15_TRUE : 0;
        case 016: return (!kbd || m_keyboard->switch_on(IE15Keyboard::SW_DUP)) ? IE15_TRUE : 0;
        case 017: return (!kbd || m_keyboard->switch_on(IE15Keyboard::SW_LIN)) ? IE15_TRUE : 0;
        case 001: case 010: case 011: case 013: return get_value(address);
        default:
            if (address >= 020 && address < 030) return read_flag(address - 020);
            return _FFFF;
    }
}

void IE15IO::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    value &= 0xFF;
    if (address <= 005) video_sync();
    switch (address) {
        case 000:
            // Обращение к ЗУ - две команды; пишет вторая
            m_latch = !m_latch;
            if (!m_latch) {
                m_vram[m_ptr] = (uint8_t)value;
                m_ptr = (m_ptr + 1) & 0xFFF;
            }
            return;
        case 002: m_ptr = (m_ptr & 0x00F) | (value << 4); return;
        case 003: m_ptr = (m_ptr & 0xFF0) | (value >> 4); return;
        case 004: m_ptr = (m_ptr + 1) & 0xFFF; return;
        case 005: m_ptr = (m_ptr - 1) & 0xFFF; return;
        case 006:
            if (m_tx_busy) note_unsupported("transmit while busy");
            m_tx_char = (uint8_t)value;
            m_tx_busy = true;
            m_tx_left = char_ticks();
            return;
        case 010:
            m_bells++;
            m_beep_left = (uint64_t)m_system_clock * (m_short_beep ? 15 : 40) / 100;
            m_beep_phase = 0;
            return;
        case 014: m_speed = value & 7; return;
        case 015: m_kb_ready = false; return;
        default:
            if (address >= 020 && address < 030) write_flag(address - 020, (value & 1) != 0);
            return;
    }
}

std::vector<DeviceFieldInfo> IE15IO::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"received", "Байтов принято от ЭВМ",                       false});
    r.push_back({"sent",     "Байтов передано в ЭВМ",                       false});
    r.push_back({"overruns", "Принятых байтов, затёртых следующим",         false});
    r.push_back({"bells",    "Звонков",                                     false});
    r.push_back({"baud",     "Скорость линии (код в порту 14)",             false});
    r.push_back({"keys",     "Нажатых клавиш с кодом",                      false});
    r.push_back({"kb_ready", "Флаг готовности клавиатуры (порт 13)",        false});
    r.push_back({"ptr",      "Адрес ЗУ",                                    false});
    r.push_back({"flags",    "Флаги 0-7 (разряд n - флаг n; 5 и 6 - у МПУ)", false});
    r.push_back({"frame",    "Кадров с включения",                          false});
    r.push_back({"snow",     "Линий, где показ выключался или включался посреди", false});
    return r;
}

bool IE15IO::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    unsigned int v;
    if (field == "received") v = m_received;
    else if (field == "sent") v = m_sent;
    else if (field == "overruns") v = m_overruns;
    else if (field == "bells") v = m_bells;
    else if (field == "baud") v = 9600u >> m_speed;
    else if (field == "keys") v = m_keys_held.load(std::memory_order_relaxed);
    else if (field == "kb_ready") v = m_kb_ready ? 1 : 0;
    else if (field == "ptr") v = m_ptr;
    else if (field == "flags")
        v = (m_video ? 1 : 0) | (m_cursor ? 2 : 0) | (m_short_beep ? 4 : 0) | (m_status_off ? 8 : 0)
            | (m_lat ? 0x10 : 0) | (m_fl7 ? 0x80 : 0);
    else if (field == "frame") v = m_frame;
    else if (field == "snow") v = m_snow;
    else return AddressableDevice::get_field(field, from, to, out);
    out.numeric = true;
    out.values.push_back(v);
    return true;
}

void IE15IO::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("ptr", m_ptr);
    w.b("latch", m_latch);
    w.u("flags", (m_video ? 1 : 0) | (m_cursor ? 2 : 0) | (m_short_beep ? 4 : 0) | (m_status_off ? 8 : 0)
                 | (m_lat ? 0x10 : 0) | (m_fl7 ? 0x80 : 0), 8);
    w.n("pos", m_pos);
    w.n("line", m_line);
    w.b("line_drawn", m_line_drawn);
    w.b("marker", m_marker);
    w.b("line_on", m_line_on);
    w.n("draw_x", m_draw_x);
    w.u("row_start", m_row_start);
    w.array("cells", m_cells, 80);
    w.n("frame", m_frame);
    w.n("snow", m_snow);
    w.u("rx_char", m_rx_char, 8);
    w.b("rx_full", m_rx_full);
    w.u("tx_char", m_tx_char, 8);
    w.b("tx_busy", m_tx_busy);
    w.n64("tx_left", m_tx_left);
    w.n("speed", m_speed);
    std::vector<uint8_t> feed(m_feed.begin(), m_feed.end());
    w.n("feed_count", (uint32_t)feed.size());
    if (!feed.empty()) w.array("feed", feed.data(), feed.size());
    w.n64("feed_left", m_feed_left);
    w.n64("beep_left", m_beep_left);
    w.n64("beep_phase", m_beep_phase);
    w.b("beep_level", m_beep_level);
    w.u("kb_code", m_kb_code, 8);
    w.b("kb_dk", m_kb_dk);
    w.b("kb_ready", m_kb_ready);
    w.n("received", m_received);
    w.n("sent", m_sent);
    w.n("overruns", m_overruns);
    w.n("bells", m_bells);
}

emulator::Result IE15IO::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("ptr", m_ptr);
    m_ptr &= 0xFFF;
    r.b("latch", m_latch);
    uint32_t f = 0;
    r.u("flags", f);
    m_video = (f & 1) != 0;
    m_cursor = (f & 2) != 0;
    m_short_beep = (f & 4) != 0;
    m_status_off = (f & 8) != 0;
    m_lat = (f & 0x10) != 0;
    m_fl7 = (f & 0x80) != 0;
    r.u("pos", m_pos);
    r.u("line", m_line);
    if (m_pos >= IE15_LINE_TICKS) m_pos = 0;
    if (m_line >= IE15_TOTAL_LINES) m_line = 0;
    r.b("line_drawn", m_line_drawn);
    r.b("marker", m_marker);
    // Снимок без линии по знакоместам: эта линия пропадает
    m_line_on = false;
    r.b("line_on", m_line_on);
    r.u("draw_x", m_draw_x);
    if (m_draw_x > 80) m_draw_x = 80;
    r.u("row_start", m_row_start);
    r.array("cells", m_cells, 80);
    r.u("frame", m_frame);
    r.u("snow", m_snow);
    r.u("rx_char", m_rx_char);
    r.b("rx_full", m_rx_full);
    r.u("tx_char", m_tx_char);
    r.b("tx_busy", m_tx_busy);
    r.n64("tx_left", m_tx_left);
    r.u("speed", m_speed);
    m_speed &= 7;
    uint32_t n = 0;
    m_feed.clear();
    if (r.u("feed_count", n) && n > 0) {
        std::vector<uint8_t> feed(n);
        if (r.array("feed", feed.data(), n)) m_feed.assign(feed.begin(), feed.end());
    }
    r.n64("feed_left", m_feed_left);
    r.n64("beep_left", m_beep_left);
    r.n64("beep_phase", m_beep_phase);
    r.b("beep_level", m_beep_level);
    r.u("kb_code", m_kb_code);
    r.b("kb_dk", m_kb_dk);
    // Нажатые клавиши - ввод хоста, в снимок не входят: после загрузки ни одна
    // не нажата, и флаг готовности снят, как его сняло бы отпускание
    m_kb_ready = m_kb_on = false;
    m_scan_left = 0;
    m_held.clear();
    m_keys_held.store(0, std::memory_order_relaxed);
    r.u("received", m_received);
    r.u("sent", m_sent);
    r.u("overruns", m_overruns);
    r.u("bells", m_bells);
    return emulator::Result::ok();
}

std::vector<DeviceCommandInfo> IE15IO::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"receive", "\"text\"", "Байты как от ЭВМ, по одному за время символа (\\n, \\r, \\t, \\ooo)"});
    return r;
}

emulator::Result IE15IO::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "receive") {
        const std::vector<std::string> p = split_params(parameters);
        const std::string text = decode_send_text(p.empty() ? std::string() : p[0]);
        if (m_feed.empty() && !text.empty()) m_feed_left = char_ticks();
        for (size_t i = 0; i < text.size(); i++) m_feed.push_back((uint8_t)text[i]);
        return emulator::Result::ok();
    }
    return AddressableDevice::send_command(command, parameters);
}

ComputerDevice * create_ie15_io(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IE15IO(im, cd);
}
