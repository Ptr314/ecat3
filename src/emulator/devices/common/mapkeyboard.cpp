// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port-based keyboard device

#include "emulator/utils.h"
#include "mapkeyboard.h"
#include "dsk_tools/dsk_tools.h"

MapKeyboard::MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , shift_pressed(false)
    , ctrl_pressed(false)
    , code_ruslat(0)
    , ruslat_bit(1)
    , i_ruslat(this, im, 1, "ruslat", MODE_W)
    , i_ready(this, im, 1, "ready", MODE_W)
    , i_pressed(this, im, 1, "pressed", MODE_W)
{
    m_rus_switches[0] = 0;
    m_rus_switches[1] = 0;
}

emulator::Result MapKeyboard::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Keyboard map file is expected")) + "}");
    else {
        std::string content = dsk_tools::utf8_read_file(map_file);
        if (content.empty()) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Error reading map file")) + "} " + map_file);
        }

        std::vector<std::string> lines = split_string(content, '\n', true);
        for (size_t li = 0; li < lines.size(); li++)
        {
            std::string line = str_trim(lines[li]);
            if (!line.empty()) {
                std::vector<std::string> parts = split_string(line, ':', true);
                if (parts.size() != 2) {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Map file entry is incorrect")) + "} " + line);
                }
                std::vector<std::string> left_parts = split_string(parts[0], '/', true);
                if (left_parts.size() < 1 || left_parts.size() > 2) {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Map file entry is incorrect")) + "} " + line);
                }

                std::string key = str_trim(left_parts[0]);
                std::string modificators = (left_parts.size() > 1) ? str_trim(left_parts[1]) : "";
                std::string value = str_trim(parts[1]);
                key_map.push_back({
                    translate_key(key),
                    parse_numeric_value(value),
                    (modificators.find('S') != std::string::npos),
                    (modificators.find('C') != std::string::npos),
                    (modificators.find('R') != std::string::npos)
                });
            }
        }
    }
    port_value = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("port-value").value));

    std::string s = cd->get_parameter("port-ruslat", false).value;
    if (!s.empty()) {
        port_ruslat = dynamic_cast<Port*>(im->dm->get_device_by_name(s));
    } else
        port_ruslat = nullptr;

    std::string rl = cd->get_parameter("ruslat", false).value;
    if (!rl.empty()) {
        code_ruslat = translate_key(rl);
    }

    try {
        rus_value = read_confg_value(cd, "rus-on", false, (unsigned int)1);
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "rus-on should be 0 or 1")) + "}");
    }

    try {
        ruslat_bit = read_confg_value(cd, "rus-bit", false, (unsigned int)0);
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "rus-bit should be a number")) + "}");
    }

    const std::string mode_str = str_tolower(cd->get_parameter("rusmode", false).value);
    if (mode_str.empty() || mode_str == "pin") {
        m_use_pin = true;
        m_use_codes = false;
    } else if (mode_str == "both"){
        m_use_pin = true;
        m_use_codes = true;
    } else if (mode_str == "code"){
        m_use_pin = false;
        m_use_codes = true;
    } else
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Incorrect keyboard rusmode")) + "} " + mode_str);

    std::string rs = cd->get_parameter("rus_switches", false).value;
    if (!rs.empty()) {
        std::vector<std::string> rs_parts = split_string(rs, '/', true);
        if (rs_parts.size() == 2) {
            m_rus_switches[0] = parse_numeric_value(str_trim(rs_parts[0]));
            m_rus_switches[1] = parse_numeric_value(str_trim(rs_parts[1]));
            m_has_rus_switches = true;
        } else {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "rus_switches should have two values separated by '/'")) + "}");
        }
    }

    i_ready.change(1);

    return emulator::Result::ok();
}

void MapKeyboard::set_rus(bool new_rus)
{
    Keyboard::set_rus(new_rus);

    unsigned int ruslat_state = new_rus?rus_value:(rus_value ^ 1);
    if (port_ruslat != nullptr) {
        unsigned int port_value = (port_ruslat->get_value(0) & ~(1 << ruslat_bit)) | (ruslat_state  << ruslat_bit);
        port_ruslat->set_value_word(port_value, port_value); // Alow using both port & port-address
    }
    i_ruslat.change(ruslat_state);
}

void MapKeyboard::send_key(unsigned int value)
{
    port_value->set_value_word(value, value); // To use both port & port-address
    i_ready.change(0);
    i_ready.change(1);
}

// Some machines have a line telling whether any key is held at the moment,
// separate from the code of the last key pressed. The БК firmware uses it for
// the auto repeat, so a key that is never seen as held is dropped again right
// after it has been read.
void MapKeyboard::update_pressed()
{
    i_pressed.change(keys_held.empty()? 0 : 1);
}

void MapKeyboard::key_down(unsigned int key)
{
    bool known = false;
    for (size_t i = 0; i < keys_held.size(); i++)
        if (keys_held[i] == key) { known = true; break; }
    if (!known) {
        keys_held.push_back(key);
        update_pressed();
    }

    if (key == EmuKey::Control)
        ctrl_pressed = true;
    else if (key == EmuKey::Shift)
        shift_pressed = true;
    else if (key == code_ruslat) {
        set_rus(!rus_mode);
        if (m_use_codes && m_has_rus_switches) {
            send_key(m_rus_switches[rus_mode?0:1]);
        }
    }else {
        bool found_with_rus = false;
        bool found_no_rus = false;
        unsigned key_index = 0;
        if (m_use_codes)
            for (size_t i=0; i<key_map.size(); i++)
                if (       key_map[i].key_code == key
                        && key_map[i].ctrl     == ctrl_pressed
                        && key_map[i].shift    == shift_pressed
                        && (key_map[i].rus == rus_mode || !m_use_codes)
                    )
                {
                    key_index = i;
                    found_with_rus = true;
                    break;
                }
        if (!found_with_rus) {
            for (size_t i=0; i<key_map.size(); i++)
                if (       key_map[i].key_code == key
                        && key_map[i].ctrl     == ctrl_pressed
                        && key_map[i].shift    == shift_pressed
                    )
                {
                    key_index = i;
                    found_no_rus = true;
                    break;
                }
        }
        if (found_with_rus || found_no_rus) send_key(key_map[key_index].value);
    }
}

// A symbol of the upper register has a map entry with Shift and none without
// it, so pressing the key alone gives nothing. Letters have both entries and
// are typed as they are.
bool MapKeyboard::needs_shift(unsigned int key)
{
    bool plain = false, shifted = false;
    for (size_t i = 0; i < key_map.size(); i++)
        if (key_map[i].key_code == key && !key_map[i].ctrl) {
            if (key_map[i].shift) shifted = true; else plain = true;
        }
    return shifted && !plain;
}

void MapKeyboard::key_up(unsigned int key)
{
    for (size_t i = 0; i < keys_held.size(); i++)
        if (keys_held[i] == key) {
            keys_held.erase(keys_held.begin() + i);
            update_pressed();
            break;
        }

    if (key == EmuKey::Control)
        ctrl_pressed = false;
    else if (key == EmuKey::Shift)
        shift_pressed = false;
}

void MapKeyboard::reset(bool cool)
{
    Keyboard::reset(cool);

    keys_held.clear();
    update_pressed();

    if (code_ruslat != 0)
        if (code_ruslat == EmuKey::CapsLock)
            set_rus(checkCapsLock());
        else
            set_rus(false);
    else
        set_rus(false);
}

std::vector<DeviceFieldInfo> MapKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = Keyboard::get_device_fields();
    r.push_back({"shift", "1 while Shift is held",                     false});
    r.push_back({"ctrl",  "1 while Ctrl is held",                      false});
    r.push_back({"held",  "Codes of the keys currently down",          false});
    r.push_back({"count", "How many keys are currently down",          false});
    return r;
}

bool MapKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "shift" || field == "ctrl")
    {
        out.numeric = true;
        out.values.push_back(((field == "shift") ? shift_pressed : ctrl_pressed) ? 1 : 0);
        return true;
    }

    //A key that a script pressed and never released is invisible otherwise,
    //and it changes everything typed afterwards
    if (field == "held")
    {
        out.numeric = true;
        //The codes match Qt::Key, so the modifiers live above bit 16 and a
        //narrower width would quietly cut them off
        out.width = 32;
        for (size_t i = 0; i < keys_held.size(); i++) out.values.push_back(keys_held[i]);
        if (keys_held.empty()) out.values.push_back(0);
        return true;
    }

    if (field == "count")
    {
        out.numeric = true;
        out.values.push_back(static_cast<unsigned int>(keys_held.size()));
        return true;
    }

    return Keyboard::get_field(field, from, to, out);
}

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new MapKeyboard(im, cd);
}
