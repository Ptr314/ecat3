// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A printer on the lines of a parallel port

#include "printer.h"
#include "emulator/utils.h"

#define CALLBACK_STROBE 1

Printer::Printer(InterfaceManager *im, EmulatorConfigDevice *cd):
      PluggableDevice(im, cd)
    , i_data(this, im, 8, "data", MODE_R)
    , i_strobe(this, im, 1, "strobe", MODE_R, CALLBACK_STROBE)
    , i_busy(this, im, 1, "busy", MODE_W)
    , i_ready(this, im, 1, "ready", MODE_W)
    , i_online(this, im, 1, "online", MODE_W)
{
    m_clocked = true;   // clock() переопределён
    // Положение при пуске. Разъём, на котором стоит принтер, задаёт его сам
    // в своём load_config(), раньше или позже этого места
    try {
        m_plugged = read_confg_value(cd, "plugged", false, true);
    } catch (const std::exception &) {
        m_plugged = true;
    }
}

emulator::Result Printer::load_config(SystemData *sd)
{
    emulator::Result res = PluggableDevice::load_config(sd);
    if (!res) return res;

    // Время байта, мкс: столько принтер занят после строба
    const unsigned int char_us = read_confg_value(cd, "char_time", false, (unsigned int)1000);
    if (char_us == 0 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Printer|" + std::string(QT_TRANSLATE_NOOP("Printer", "Incorrect value of a parameter")) + "} char_time");
    m_char_ticks = (uint64_t)m_system_clock * char_us / 1000000;
    if (m_char_ticks == 0) m_char_ticks = 1;

    // Линии, которые никто ещё не вёл, хранят _FFFF: первый раз выходы
    // выставляются все, совпадает младший разряд или нет
    update_lines(true);
    return emulator::Result::ok();
}

void Printer::update_lines(bool all)
{
    const unsigned int busy = (m_plugged && m_busy) ? 1 : 0;
    const unsigned int ready = (m_plugged && !m_busy) ? 1 : 0;
    const unsigned int online = m_plugged ? 1 : 0;
    if (all || i_busy.value != busy) i_busy.change(busy);
    if (all || i_ready.value != ready) i_ready.change(ready);
    if (all || i_online.value != online) i_online.change(online);
}

void Printer::plug_changed()
{
    // Вынутый принтер бумагу не дожёвывает: вставленный снова готов сразу
    m_busy = false;
    m_left = 0;
    update_lines();
}

const char * Printer::plug_title() const
{
    return QT_TRANSLATE_NOOP("DeviceOptions", "Printer");
}

// Принтер - отдельный аппарат, сброс машины его не касается. Только при
// включении он не занят
void Printer::reset(bool cold)
{
    if (!cold) return;
    m_busy = false;
    m_left = 0;
    update_lines();
}

void Printer::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id != CALLBACK_STROBE) return;
    // Только подъём: 0 -> 1. Линия, которую ещё никто не вёл, хранит _FFFF,
    // и первое её значение фронтом не считается
    if ((old_value & 1) != 0 || (new_value & 1) == 0) return;
    if (!m_plugged || m_busy) return;

    put((uint8_t)(i_data.value & 0xFF));
    m_printed++;
    m_busy = true;
    m_left = (int64_t)m_char_ticks;
    update_lines();
}

void Printer::clock(unsigned int counter)
{
    if (!m_busy) return;
    m_left -= counter;
    if (m_left > 0) return;
    m_busy = false;
    m_left = 0;
    update_lines();
}

//--------------------------- Сценарии --------------------------------------//

std::vector<DeviceFieldInfo> Printer::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = PluggableDevice::get_device_fields();
    r.push_back({"printed",   "Сколько байтов принято",                                false});
    r.push_back({"busy",      "1, пока принтер занят принятым байтом",                 false});
    r.push_back({"ready",     "1, когда принтер подключён и готов принять байт",       false});
    r.push_back({"plugged",   "1, когда принтер подключён",                            false});
    add_output_fields(r);
    return r;
}

bool Printer::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (get_output_field(field, from, to, out)) return true;

    out.numeric = true;
    out.width = 16;
    if (field == "busy")      { out.values.push_back(m_busy? 1 : 0);                   return true; }
    if (field == "ready")     { out.values.push_back((m_plugged && !m_busy)? 1 : 0);   return true; }
    if (field == "plugged")   { out.values.push_back(m_plugged? 1 : 0);                return true; }
    out.width = 32;
    if (field == "printed")   { out.values.push_back(m_printed);                      return true; }

    out.numeric = false;
    out.width = 0;
    return PluggableDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> Printer::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = PluggableDevice::get_device_commands();
    r.push_back({"plug",      "[0|1]",    "Подключает (1) или отключает (0) принтер, без параметра - переключает"});
    return r;
}

emulator::Result Printer::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "plug") {
        std::vector<std::string> p = split_params(parameters);
        const std::string arg = p.empty()? std::string() : p[0];
        set_plugged(arg.empty()? !m_plugged : (parse_numeric_value(arg, 10) != 0));
        return emulator::Result::ok();
    }
    return PluggableDevice::send_command(command, parameters);
}

// Подключён ли принтер - положение разъёма или настройка, как у мыши и
// джойстика, а не состояние машины. Занятость и счётчик - состояние
void Printer::save_state(StateWriter &w)
{
    PluggableDevice::save_state(w);
    w.b("busy", m_busy);
    w.n64("left", (uint64_t)m_left);
    w.n("printed", m_printed);
}

emulator::Result Printer::load_state(const StateReader &r)
{
    emulator::Result res = PluggableDevice::load_state(r);
    if (!res) return res;
    r.b("busy", m_busy);
    uint64_t left = 0;
    if (r.n64("left", left)) m_left = (int64_t)left;
    r.u("printed", m_printed);
    return emulator::Result::ok();
}

ComputerDevice * create_printer(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Printer(im, cd);
}
