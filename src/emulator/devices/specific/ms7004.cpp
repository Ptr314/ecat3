// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: МС7004 keyboard (a DEC LK201 workalike) on a serial line

#include "ms7004.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

#define CODE_SHIFT      0xAE        // ВР (левый и правый)
#define CODE_CTRL       0xAF        // СУ
#define CODE_UP         0xB3        // отпущена ВР или СУ
#define CODE_REPEAT     0xB4        // автоповтор: терминал повторяет последний символ
#define CODE_RETURN     0xBD        // ВК: без автоповтора
#define CODE_INHIBITED  0xB7        // ответ на $89
#define CODE_TEST       0xBA        // ответ на $D3

#define QUEUE_LIMIT     32
#define MAX_HELD        3           // столько обычных клавиш прошивка помнит

#define CMD_POWER_UP    0xFD        // перезапуск: ответ - последовательность включения
#define CMD_REQUEST_ID  0xAB        // запрос номера клавиатуры
#define CMD_INHIBIT     0x89        // запретить выдачу кодов
#define CMD_RESUME      0x8B        // разрешить выдачу кодов
#define CMD_TEST        0xD3
#define CMD_REPEAT_OFF  0xE1
#define CMD_REPEAT_OFF2 0xD9
#define CMD_REPEAT_ON   0xE3
#define KEYBOARD_ID     0x01        // номер прошивки в ответах

#define CALLBACK_RXD    1

// Проходов матрицы до первого автоповтора (байт 27H, 01AC); счётчик, дошедший
// до нуля, на следующем проходе заворачивается - ещё 255 проходов
#define REPEAT_PASSES   11
#define WRAP_PASSES     255

// Повторяется ли клавиша (029B): не ВК, не верхний ряд, не $80-$8F
static bool repeatable(unsigned int code)
{
    return code != CODE_RETURN && (code & 0x80) != 0 && (code & 0x70) != 0;
}

MS7004::MS7004(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , i_data(this, im, 8, "data", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
{
    m_clocked = true;   // clock() отдаёт коды из очереди и ведёт автоповтор
}

emulator::Result MS7004::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    m_interval = read_confg_value(cd, "interval", false, (unsigned int)3000);
    // Времена автоповтора - в миллисекундах; в конфигурации ДВК числа
    // восьмеричные, так что пишутся с «_»
    const uint64_t clock = (m_system_clock != 0) ? m_system_clock : 1000000;
    const unsigned int delay_ms = read_confg_value(cd, "repeat_delay", false, (unsigned int)360);
    const unsigned int period_ms = read_confg_value(cd, "repeat_period", false, (unsigned int)28);
    m_repeat_delay = (unsigned int)(clock * delay_ms / 1000);
    m_repeat_period = (unsigned int)(clock * period_ms / 1000);
    if (m_repeat_period == 0) m_repeat_period = 1;

    const std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Keyboard map file is expected")) + "} " + name);

    const std::string content = dsk_tools::utf8_read_file(map_file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Error reading map file")) + "} " + map_file);

    m_keys.clear();
    const std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string line = str_trim(lines[i]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;

        // Двоеточие ищется с конца имени: клавиша хоста сама бывает «:»
        const size_t colon = line.find(':', 1);
        if (colon == std::string::npos)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Map file entry is incorrect")) + "} " + line);

        const std::string key_name = str_trim(line.substr(0, colon));
        std::string value = str_trim(line.substr(colon + 1));

        ShiftMode shift = SHIFT_ANY;
        const size_t space = value.find_first_of(" \t");
        if (space != std::string::npos) {
            const std::string mark = str_trim(value.substr(space));
            value = str_trim(value.substr(0, space));
            if      (mark == "+shift") shift = SHIFT_ON;
            else if (mark == "-shift") shift = SHIFT_OFF;
            else
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Map file entry is incorrect")) + "} " + line);
        }

        const unsigned int host = translate_key(key_name);
        if (host == _FFFF)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Unknown key in the map file")) + "} " + line);

        unsigned int code;
        try {
            code = parse_numeric_value(value);
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Invalid value in the map file")) + "} " + line);
        }

        KeyEntry e;
        e.host = host;
        e.code = code & 0xFF;
        e.shift = shift;
        m_keys.push_back(e);
    }

    return emulator::Result::ok();
}

void MS7004::reset(const bool cold)
{
    Keyboard::reset(cold);
    compat_lock_guard lock(m_queue_mutex);
    m_queue.clear();
    m_queued = false;
    m_shift_host = m_ctrl_host = m_shift_machine = m_ctrl_machine = false;
    m_forced = 0;
    m_wait = 0;
    m_held.clear();
    m_repeat_armed = false;
    m_repeating = false;
    m_repeat_left = 0;
    m_repeat_on = true;
    m_inhibit = false;
    m_second = false;
}

const MS7004::KeyEntry * MS7004::entry_of(unsigned int host) const
{
    for (size_t i = 0; i < m_keys.size(); i++)
        if (m_keys[i].host == host) return &m_keys[i];
    return nullptr;
}

void MS7004::enqueue_locked(unsigned int code)
{
    if (m_queue.size() >= QUEUE_LIMIT) {
        m_dropped++;
        return;
    }
    m_queue.push_back(code);
    m_queued = true;
}

// Код терминалу; после $89 прошивка его не выдаёт (006F), он пропадает
void MS7004::send_locked(unsigned int code)
{
    if (!m_inhibit) enqueue_locked(code);
}

// Автоповтор переходит к последней из нажатых клавиш. Счётчик у прошивки
// один: новая клавиша заводит его заново, а после отпускания последней он
// идёт дальше для предыдущей - с того места, где был, или, если повтор уже
// шёл и счётчик стоит на нуле, ещё 255 проходов
void MS7004::arm_repeat_locked()
{
    if (m_held.empty() || !repeatable(m_held.back().second)) {
        m_repeat_armed = false;
        m_repeating = false;
        return;
    }
    if (m_repeating) {
        m_repeat_left = m_repeat_delay / REPEAT_PASSES * WRAP_PASSES;
        m_repeating = false;
    }
    m_repeat_armed = true;
}

// ВР в том положении, в каком его должен видеть терминал. Отпускание - $B3,
// и у КСМ оно снимает сначала СУ: если ВР ставили за хост (пометка знака), а
// СУ держат, СУ возвращается ещё одним $B3 (снимает ВР) и своим кодом
void MS7004::set_shift(bool down, bool forced)
{
    if (m_shift_machine == down) return;
    m_shift_machine = down;
    if (down) {
        send_locked(CODE_SHIFT);
        return;
    }
    send_locked(CODE_UP);
    if (forced && m_ctrl_machine) {
        send_locked(CODE_UP);
        send_locked(CODE_CTRL);
    }
}

void MS7004::press(const KeyEntry &e, bool down)
{
    compat_lock_guard lock(m_queue_mutex);

    if (e.code == CODE_SHIFT) {
        m_shift_host = down;
        if (m_forced == 0) set_shift(down, false);
        return;
    }
    if (e.code == CODE_CTRL) {
        m_ctrl_host = down;
        if (m_ctrl_machine == down) return;
        m_ctrl_machine = down;
        send_locked(down ? CODE_CTRL : CODE_UP);
        return;
    }

    size_t held = 0;
    while (held < m_held.size() && m_held[held].first != e.host) held++;
    if (down) {
        // Уже нажата (KEYDOWN дважды) или три клавиши уже держат - прошивка
        // её не замечает
        if (held < m_held.size() || m_held.size() >= MAX_HELD) return;
        if (e.shift != SHIFT_ANY) {
            set_shift(e.shift == SHIFT_ON, true);
            m_forced++;
        }
        send_locked(e.code);
        m_held.push_back(std::make_pair(e.host, e.code));
        m_repeat_left = m_repeat_delay;
        m_repeating = false;
        arm_repeat_locked();
    } else {
        if (held >= m_held.size()) return;
        const bool last = (held + 1 == m_held.size());
        m_held.erase(m_held.begin() + held);
        if (last) arm_repeat_locked();
        if (e.shift != SHIFT_ANY && m_forced > 0) {
            m_forced--;
            if (m_forced == 0) set_shift(m_shift_host, true);
        }
    }
}

void MS7004::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    // Линия хранит последний байт до следующего; _FFFF - её никто не ведёт
    if (callback_id == CALLBACK_RXD && new_value != _FFFF) command(new_value & 0xFF);
}

// Команда терминала (поток эмуляции: байт приходит от ИРПС), разбор - 0400
void MS7004::command(unsigned int code)
{
    compat_lock_guard lock(m_queue_mutex);
    m_commands++;
    // Второй байт команд индикаторов, звука и щелчка ($11, $13, $1B, $23)
    // разбирается как параметр, а не как $89/$8B (0465)
    const bool second = m_second;
    m_second = (code == 0x11 || code == 0x13 || code == 0x1B || code == 0x23);
    if (second && (code == CMD_INHIBIT || code == CMD_RESUME)) return;

    switch (code) {
    case CMD_POWER_UP:
        // Ответ уходит ещё под запретом выдачи, потом запрет снимается и
        // автоповтор включается (02FC). Нажатое прошивка помнит
        send_locked(KEYBOARD_ID);
        send_locked(0);
        send_locked(0);
        send_locked(0);
        m_inhibit = false;
        m_repeat_on = true;
        break;
    case CMD_REQUEST_ID:
        send_locked(KEYBOARD_ID);
        send_locked(0);
        break;
    case CMD_INHIBIT:
        send_locked(CODE_INHIBITED);
        m_inhibit = true;
        break;
    case CMD_RESUME:
        m_inhibit = false;
        break;
    case CMD_TEST:
        send_locked(CODE_TEST);
        break;
    case CMD_REPEAT_OFF:
    case CMD_REPEAT_OFF2:
        m_repeat_on = false;
        break;
    case CMD_REPEAT_ON:
        m_repeat_on = true;
        break;
    default:
        break;
    }
}

void MS7004::key_down(unsigned int key)
{
    const KeyEntry * e = entry_of(key);
    if (e != nullptr) press(*e, true);
}

void MS7004::key_up(unsigned int key)
{
    const KeyEntry * e = entry_of(key);
    if (e != nullptr) press(*e, false);
}

// Поток эмуляции: автоповтор и код на линию не чаще, чем раз в символ
void MS7004::clock(unsigned int counter)
{
    if (m_repeat_armed) {
        compat_lock_guard lock(m_queue_mutex);
        if (m_repeat_armed) {
            if (m_repeat_left > counter)
                m_repeat_left -= counter;
            else if (m_repeat_on) {
                send_locked(CODE_REPEAT);
                m_repeats++;
                m_repeating = true;
                m_repeat_left = m_repeat_period;
            } else {
                // Выключенный автоповтор: счётчик заворачивается (02A9)
                m_repeat_left = m_repeat_delay / REPEAT_PASSES * WRAP_PASSES;
            }
        }
    }

    if (m_wait > counter) {
        m_wait -= counter;
        return;
    }
    m_wait = 0;
    if (!m_queued) return;

    unsigned int code;
    {
        compat_lock_guard lock(m_queue_mutex);
        if (m_queue.empty()) {
            m_queued = false;
            return;
        }
        code = m_queue.front();
        m_queue.pop_front();
        m_queued = !m_queue.empty();
    }
    m_last = code;
    m_codes++;
    i_data.change(code);
    m_wait = m_interval;
}

std::vector<DeviceFieldInfo> MS7004::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = Keyboard::get_device_fields();
    r.push_back({"codes",   "Сколько кодов отдано терминалу",            false});
    r.push_back({"last",    "Последний отданный код LK201",               false});
    r.push_back({"queued",  "Сколько кодов ждут своей очереди на линии",  false});
    r.push_back({"dropped", "Сколько кодов не влезло в очередь",          false});
    r.push_back({"commands", "Сколько команд прислал терминал",            false});
    r.push_back({"repeats", "Сколько кодов автоповтора ($B4) отдано",     false});
    r.push_back({"repeat",  "Автоповтор включён (команды $E1/$D9/$E3)",   false});
    r.push_back({"inhibit", "Выдача кодов запрещена командой $89",        false});
    return r;
}

bool MS7004::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "codes")   { out.values.push_back(m_codes);   return true; }
    if (field == "last")    { out.values.push_back(m_last);    return true; }
    if (field == "dropped") { out.values.push_back(m_dropped); return true; }
    if (field == "commands") { out.values.push_back(m_commands); return true; }
    if (field == "repeats") { out.values.push_back(m_repeats); return true; }
    if (field == "repeat")  { out.values.push_back(m_repeat_on ? 1 : 0); return true; }
    if (field == "inhibit") { out.values.push_back(m_inhibit ? 1 : 0); return true; }
    if (field == "queued") {
        compat_lock_guard lock(m_queue_mutex);
        out.values.push_back((unsigned int)m_queue.size());
        return true;
    }
    out.numeric = false;
    out.width = 0;
    return Keyboard::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> MS7004::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = Keyboard::get_device_commands();
    r.push_back({"code", "n[,n...]", "Отдаёт терминалу коды LK201 как есть"});
    return r;
}

emulator::Result MS7004::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "code") {
        const std::vector<std::string> p = split_params(parameters);
        compat_lock_guard lock(m_queue_mutex);
        for (size_t i = 0; i < p.size(); i++) {
            try {
                enqueue_locked(parse_numeric_value(p[i]) & 0xFF);
            } catch (const std::exception &) {
                return emulator::Result::error(emulator::ErrorCode::BadParameters,
                    "{MS7004|" + std::string(QT_TRANSLATE_NOOP("MS7004", "Invalid key code")) + "} " + p[i]);
            }
        }
        return emulator::Result::ok();
    }
    return Keyboard::send_command(command, parameters);
}

void MS7004::save_state(StateWriter &w)
{
    Keyboard::save_state(w);
    w.b("shift_machine", m_shift_machine);
    w.b("ctrl_machine", m_ctrl_machine);
    w.b("repeat_on", m_repeat_on);
    w.b("inhibit", m_inhibit);
    w.u("wait", m_wait);
}

emulator::Result MS7004::load_state(const StateReader &r)
{
    emulator::Result res = Keyboard::load_state(r);
    if (!res) return res;
    compat_lock_guard lock(m_queue_mutex);
    r.b("shift_machine", m_shift_machine);
    r.b("ctrl_machine", m_ctrl_machine);
    r.b("repeat_on", m_repeat_on);
    r.b("inhibit", m_inhibit);
    r.u("wait", m_wait);
    m_queue.clear();
    m_queued = false;
    m_shift_host = m_ctrl_host = false;
    m_forced = 0;
    m_held.clear();
    m_repeat_armed = false;
    m_repeating = false;
    m_second = false;
    return emulator::Result::ok();
}

ComputerDevice * create_ms7004(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new MS7004(im, cd);
}
