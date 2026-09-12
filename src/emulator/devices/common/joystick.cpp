// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Digital joystick driven by host keys

#include "joystick.h"
#include "keyboard.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"

Joystick::Joystick(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
    , i_out(this, im, 16, "out", MODE_W)
    , m_state(0)
{
    device_class = "joystick";
}

emulator::Result Joystick::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    // The map lists "key: bits" pairs, one per contact, in the keyboard map format
    std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Joystick|" + std::string(QT_TRANSLATE_NOOP("Joystick", "Joystick map file is expected")) + "}");

    std::string content = dsk_tools::utf8_read_file(map_file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Joystick|" + std::string(QT_TRANSLATE_NOOP("Joystick", "Error reading map file")) + "} " + map_file);

    std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t li = 0; li < lines.size(); li++)
    {
        //The joystick map is a map file too, and CONFIG.md promises comments
        //for those: a "// arrows" line copied from a keyboard map must not be
        //what makes the machine refuse to start
        std::string line = str_trim(lines[li]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;
        std::vector<std::string> parts = split_string(line, ':', true);
        if (parts.size() != 2)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Joystick|" + std::string(QT_TRANSLATE_NOOP("Joystick", "Map file entry is incorrect")) + "} " + line);
        unsigned int key = translate_key_name(str_trim(parts[0]));
        if (key == _FFFF)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Joystick|" + std::string(QT_TRANSLATE_NOOP("Joystick", "Unknown key in the map file")) + "} " + line);
        Contact c;
        c.key = key;
        //A mistyped digit throws, and a config load catches nothing on the way
        //out: the emulator used to end with "terminate called", naming neither
        //the file nor the line
        try {
            c.bits = parse_numeric_value(str_trim(parts[1]));
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Joystick|" + std::string(QT_TRANSLATE_NOOP("Joystick", "Map file entry is incorrect")) + "} " + line);
        }
        m_contacts.push_back(c);
    }

    i_out.change(0);
    return emulator::Result::ok();
}

void Joystick::reset(MAYBE_UNUSED bool cold)
{
    m_held.clear();
    update();
}

void Joystick::key_event(unsigned int key, bool press)
{
    bool known = false;
    for (size_t i = 0; i < m_contacts.size(); i++)
        if (m_contacts[i].key == key) { known = true; break; }
    if (!known) return;

    for (size_t i = 0; i < m_held.size(); i++)
        if (m_held[i] == key) {
            if (!press) m_held.erase(m_held.begin() + i);
            update();
            return;
        }
    if (press) m_held.push_back(key);
    update();
}

// The output is the union of the contacts of all keys held down
void Joystick::update()
{
    unsigned int state = 0;
    for (size_t h = 0; h < m_held.size(); h++)
        for (size_t i = 0; i < m_contacts.size(); i++)
            if (m_contacts[i].key == m_held[h]) state |= m_contacts[i].bits;
    if (state != m_state) {
        m_state = state;
        i_out.change(state);
    }
}

std::vector<DeviceFieldInfo> Joystick::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"state", "Bits of the contacts currently closed", false});
    return r;
}

bool Joystick::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "state") { out.values.push_back(m_state); return true; }
    out.width = 0;
    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

ComputerDevice * create_joystick(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Joystick(im, cd);
}
