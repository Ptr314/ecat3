// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: МС7004 keyboard (a DEC LK201 workalike) on a serial line

#include "ms7004.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

#define CODE_SHIFT      0xAE        // ВР
#define CODE_CTRL       0xAF        // СУ
#define CODE_ALL_UPS    0xB3        // все клавиши «вниз/вверх» отпущены

#define QUEUE_LIMIT     32

#define CMD_POWER_UP    0xFD        // перезапуск: ответ - последовательность включения
#define CMD_REQUEST_ID  0xAB        // запрос номера клавиатуры
#define KEYBOARD_ID     0x01        // номер прошивки в ответах

#define CALLBACK_RXD    1

MS7004::MS7004(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , i_data(this, im, 8, "data", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
{
    m_clocked = true;   // clock() отдаёт коды из очереди
}

emulator::Result MS7004::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    m_interval = read_confg_value(cd, "interval", false, (unsigned int)3000);

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
    m_shift_host = m_ctrl_host = m_shift_machine = false;
    m_forced = 0;
    m_wait = 0;
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

// Одна из клавиш «вниз/вверх» (ВР, СУ) отпущена, их состояние уже обновлено.
// Прошивка КСМ понимает код ВР и код СУ только как «нажата», а «все
// отпущены» снимает у неё сначала СУ и лишь при снятом СУ - ВР (0587). Если
// другая клавиша ещё держится, двумя «все отпущены» снимаются обе, и её код
// уходит снова. Иначе ВР, отпущенный под СУ (переключение раскладки Windows
// по Ctrl+Shift), оставался у терминала нажатым, и буквы шли строчными
void MS7004::modifier_released()
{
    if (m_ctrl_host || m_shift_machine) {
        enqueue_locked(CODE_ALL_UPS);
        enqueue_locked(CODE_ALL_UPS);
        if (m_ctrl_host) enqueue_locked(CODE_CTRL);
        if (m_shift_machine) enqueue_locked(CODE_SHIFT);
    } else
        enqueue_locked(CODE_ALL_UPS);
}

// ВР в том положении, в каком его должен видеть терминал
void MS7004::set_shift(bool down)
{
    if (m_shift_machine == down) return;
    m_shift_machine = down;
    if (down)
        enqueue_locked(CODE_SHIFT);
    else
        modifier_released();
}

void MS7004::press(const KeyEntry &e, bool down)
{
    compat_lock_guard lock(m_queue_mutex);

    if (e.code == CODE_SHIFT) {
        m_shift_host = down;
        if (m_forced == 0) set_shift(down);
        return;
    }
    if (e.code == CODE_CTRL) {
        if (m_ctrl_host == down) return;
        m_ctrl_host = down;
        if (down)
            enqueue_locked(CODE_CTRL);
        else
            modifier_released();
        return;
    }

    if (down) {
        if (e.shift != SHIFT_ANY) {
            set_shift(e.shift == SHIFT_ON);
            m_forced++;
        }
        enqueue_locked(e.code);
    } else {
        if (e.shift != SHIFT_ANY && m_forced > 0) {
            m_forced--;
            if (m_forced == 0) set_shift(m_shift_host);
        }
    }
}

void MS7004::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    // Линия хранит последний байт до следующего; _FFFF - её никто не ведёт
    if (callback_id == CALLBACK_RXD && new_value != _FFFF) command(new_value & 0xFF);
}

// Команда терминала (поток эмуляции: байт приходит от ИРПС)
void MS7004::command(unsigned int code)
{
    compat_lock_guard lock(m_queue_mutex);
    if (code == CMD_POWER_UP) {
        // Клавиатура начинает с чистого листа: нажатое терминалу не видно
        m_queue.clear();
        m_shift_machine = false;
        m_forced = 0;
        enqueue_locked(KEYBOARD_ID);
        enqueue_locked(0);
        enqueue_locked(0);
        enqueue_locked(0);
    } else if (code == CMD_REQUEST_ID) {
        enqueue_locked(KEYBOARD_ID);
        enqueue_locked(0);
    }
    m_commands++;
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

// Поток эмуляции: код на линию не чаще, чем раз в символ
void MS7004::clock(unsigned int counter)
{
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
    w.b("ctrl_machine", m_ctrl_host);
    w.u("wait", m_wait);
}

emulator::Result MS7004::load_state(const StateReader &r)
{
    emulator::Result res = Keyboard::load_state(r);
    if (!res) return res;
    compat_lock_guard lock(m_queue_mutex);
    r.b("shift_machine", m_shift_machine);
    r.b("ctrl_machine", m_ctrl_host);
    r.u("wait", m_wait);
    m_queue.clear();
    m_queued = false;
    m_shift_host = false;
    m_forced = 0;
    return emulator::Result::ok();
}

ComputerDevice * create_ms7004(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new MS7004(im, cd);
}
