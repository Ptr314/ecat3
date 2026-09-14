// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A socket that one of several input devices is plugged into

#include "connector.h"
#include "emulator/utils.h"

#define CONNECTOR_OPTION_DEVICE 0

//----------------------- PluggableDevice -----------------------------------//

PluggableDevice::PluggableDevice(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
    , m_plugged(true)
{
}

void PluggableDevice::set_plugged(bool on)
{
    if (on == m_plugged) return;
    m_plugged = on;
    plug_changed();
}

//----------------------- Connector -----------------------------------------//

Connector::Connector(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
    , m_selected(0)
{
    device_class = "connector";
}

emulator::Result Connector::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    std::vector<std::string> names = split_string(cd->get_parameter("devices", false).value, '|', true);
    if (names.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Connector|" + std::string(QT_TRANSLATE_NOOP("Connector", "A list of devices is expected for")) + "} " + name);

    for (size_t i = 0; i < names.size(); i++) {
        const std::string n = str_trim(names[i]);
        PluggableDevice * d = dynamic_cast<PluggableDevice*>(im->dm->get_device_by_name(n, false));
        if (d == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Connector|" + std::string(QT_TRANSLATE_NOOP("Connector", "Not a device that can be plugged in")) + "} " + n);
        m_devices.push_back(d);
    }

    // What is plugged in until the user picks something else
    const std::string def = str_trim(cd->get_parameter("default", false).value);
    m_selected = 0;
    if (!def.empty() && str_tolower(def) != "none") {
        for (size_t i = 0; i < m_devices.size(); i++)
            if (m_devices[i]->name == def) m_selected = static_cast<unsigned int>(i + 1);
        if (m_selected == 0)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Connector|" + std::string(QT_TRANSLATE_NOOP("Connector", "The default device is not in the list")) + "} " + def);
    }

    apply();
    return emulator::Result::ok();
}

// The device being unplugged goes first: it stops driving the shared lines
// before the new one puts its own state on them
void Connector::apply()
{
    for (size_t i = 0; i < m_devices.size(); i++)
        if (i + 1 != m_selected) m_devices[i]->set_plugged(false);
    if (m_selected > 0) m_devices[m_selected - 1]->set_plugged(true);
}

DeviceOptions Connector::get_device_options()
{
    DeviceOption opt;
    opt.id = CONNECTOR_OPTION_DEVICE;
    opt.type = DEVICE_OPTION_DROPDOWN;
    opt.title = QT_TRANSLATE_NOOP("DeviceOptions", "Connected device");
    opt.icon = "input_devices_settings.png";
    opt.values.push_back({0, QT_TRANSLATE_NOOP("DeviceOptions", "Nothing connected")});
    for (size_t i = 0; i < m_devices.size(); i++)
        opt.values.push_back({static_cast<unsigned>(i + 1), m_devices[i]->plug_title()});
    return {opt};
}

void Connector::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id != CONNECTOR_OPTION_DEVICE) return;
    if (value_id > m_devices.size()) return;
    m_selected = value_id;
    apply();
}

std::vector<DeviceFieldInfo> Connector::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"device", "Name of the plugged device, none if the socket is empty", false});
    return r;
}

bool Connector::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "device") {
        out.text = (m_selected > 0)?m_devices[m_selected - 1]->name:std::string("none");
        return true;
    }
    return ComputerDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> Connector::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"plug", "device|none", "Plugs a device from the list in, unplugging the one there"});
    return r;
}

emulator::Result Connector::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "plug") {
        std::vector<std::string> p = split_params(parameters);
        const std::string n = p.empty()?std::string():str_trim(p[0]);
        if (n.empty() || str_tolower(n) == "none") {
            set_device_option(CONNECTOR_OPTION_DEVICE, 0);
            return emulator::Result::ok();
        }
        for (size_t i = 0; i < m_devices.size(); i++)
            if (m_devices[i]->name == n) {
                set_device_option(CONNECTOR_OPTION_DEVICE, static_cast<unsigned>(i + 1));
                return emulator::Result::ok();
            }
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "{Connector|" + std::string(QT_TRANSLATE_NOOP("Connector", "The device is not in the list")) + "} " + n);
    }
    return ComputerDevice::send_command(command, parameters);
}

ComputerDevice * create_connector(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Connector(im, cd);
}
