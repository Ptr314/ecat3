// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Emulator config functions, source

#include "config.h"
#include "utils.h"

#include "dsk_tools/dsk_tools.h"

EmulatorConfigDevice::EmulatorConfigDevice(std::string name, std::string type):
    name(name),
    type(type)
{}

EmulatorConfigDevice::~EmulatorConfigDevice(){}

void EmulatorConfigDevice::add_parameter(std::string name, std::string left_range, std::string value, std::string right_range, std::string right_extended)
{
    parameters.push_back({name, left_range, value, right_range, right_extended});
}

EmulatorConfigParameter EmulatorConfigDevice::get_parameter(std::string name, bool required)
{
    for (size_t i = 0; i < parameters.size(); i++)
    {
        if (parameters[i].name == name) return parameters[i];
    }
    if (required)
        throw std::runtime_error(this->name + ":" + name);
    else
        return {"", "", "", "", ""};
}

EmulatorConfig::EmulatorConfig()
{}

EmulatorConfig::~EmulatorConfig()
{
    if (!devices.empty()) free_devices();
}

EmulatorConfig::EmulatorConfig(std::string file_name)
{
}

void EmulatorConfig::free_devices()
{
    devices.clear();  // Automatic cleanup via unique_ptr
}

std::string EmulatorConfig::read_next_entity(std::string *config, std::string stop)
{
    std::string s;
    const std::string parser_spaces = " \x09\x0D\x0A";
    const std::string parser_line = "\x0D\x0A";
    const std::string parser_border = "=[]{}";
    const std::string terminator = parser_border + stop;
    std::string stop_chars;
    while (!config->empty())
    {
        char c = (*config)[0];
        config->erase(0, 1);
        //Skipping spaces
        while (!config->empty() && parser_spaces.find(c) != std::string::npos)
        {
            c = (*config)[0];
            config->erase(0, 1);
        }
        if (!config->empty() || parser_spaces.find(c) == std::string::npos)
        {
            if (terminator.find(c) == std::string::npos)
            {
                if (c == '"')
                {
                    stop_chars = parser_line + "\"";
                    c = (*config)[0];
                    config->erase(0, 1);
                } else {
                    stop_chars = parser_border + parser_line + stop;
                }
                while (!config->empty() && stop_chars.find(c) == std::string::npos)
                {
                    s += c;
                    c = (*config)[0];
                    config->erase(0, 1);
                }
                if (!config->empty() && c != '"') {
                    config->insert(config->begin(), c);
                }
                s = str_trim(s);
                const auto pos = s.find("//");
                if (pos == 0) s = "";
                if (pos > 0 && pos != std::string::npos) s = str_trim(s.substr(0, pos));
                if (pos != std::string::npos && !config->empty()) {
                    // Skipping until end of line
                    char cc = (*config)[0];
                    while (!config->empty() && parser_line.find(cc) == std::string::npos) {
                        config->erase(0, 1);
                        cc = (*config)[0];
                    }
                }
                if (s.empty()) continue;
                return s;
            }
            return std::string(1, c);
        }
        return "";
    }
    return "";
}

std::string EmulatorConfig::read_extended_entity(std::string *config, std::string stop)
{
    std::string s;
    if (!config->empty())
    {
        char c = (*config)[0];
        config->erase(0, 1);
        while (!config->empty() && stop.find(c) == std::string::npos)
        {
            s += c;
            c = (*config)[0];
            config->erase(0, 1);
        }
    }
    return s;
}

EmulatorConfigDevice * EmulatorConfig::add_device(std::string device_name, std::string device_type)
{
    auto new_device = make_unique<EmulatorConfigDevice>(device_name, device_type);
    EmulatorConfigDevice* ptr = new_device.get();
    devices.push_back(std::move(new_device));
    return ptr;
}

std::string EmulatorConfigDevice::extended_parameter(unsigned int i, std::string expected_name)
{
    std::vector<std::string> list = split_string(parameters[i].right_extended, ',', true);
    for (size_t i = 0; i < list.size(); i++)
    {
        std::vector<std::string> parameter = split_string(list[i], '=', true);
        std::string name = str_tolower(str_trim(parameter[0]));
        std::string value = str_tolower(str_trim(parameter[1]));
        if (name == expected_name) return value;
    }
    return "";
}



emulator::Result EmulatorConfig::load_from_file(std::string file_name, bool system_only)
{
    std::string device_name;
    std::string device_type;

    if (!devices.empty()) free_devices();

    std::string config = dsk_tools::utf8_read_file(file_name);
    if (config.empty()) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file")) + "} " + file_name);
    }
    while(!config.empty())
    {
        device_name = read_next_entity(&config, ":");
        if (device_name.empty()) return emulator::Result::ok();
        if (device_name == "system")
        {
            device_type = "";
        } else {
            std::string s = read_next_entity(&config, ":");
            if (s.empty() || s != ":")
            {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no type found")) + "} " + device_name);
            }
            device_type = read_next_entity(&config);
            if (device_type.empty())
            {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no type found")) + "} " + device_name);
            }
        }
        EmulatorConfigDevice * new_device = add_device(device_name, device_type);

        std::string s = read_next_entity(&config);
        if (s.empty() || s != "{")
        {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no description found")) + "} " + device_name);
        }

        std::string param_name = read_next_entity(&config);
        if (param_name.empty())
        {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - incorrect parameters")) + "} " + device_name);
        }
        while (param_name != "}")
        {
            std::string new_param_name = param_name;
            s = read_next_entity(&config);
            if (s.empty() || (s != "[" && s != "="))
            {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - incorrect parameters")) + "} " + device_name);
            }

            //Do we have a range on the left?
            std::string range_left;
            if (s == "[")
            {
                range_left = "[";
                s = read_next_entity(&config);
                while (s != "=")
                {
                    range_left += s;
                    s = read_next_entity(&config);
                    if (s.empty())
                    {
                        return emulator::Result::error(emulator::ErrorCode::ConfigError,
                            "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter")) + "} " + device_name + ":" + param_name);
                    }
                }
            } else {
                range_left = "";
            }

            //Reading a right part
            s = read_next_entity(&config);
            if (s.empty())
            {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter")) + "} " + device_name + ":" + param_name);
            }
            std::string param_value;
            std::string range_right;
            if (s != "{")
            {
                param_value = s;
                s = read_next_entity(&config);
                if (s.empty())
                {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter")) + "} " + device_name + ":" + param_name);
                }
                if (s == "}")
                {
                    new_device->add_parameter(new_param_name, range_left, param_value, "", "");
                    param_name = s;
                    break;
                }
                range_right = "";
                if (s == "[")
                {
                    while(1)
                    {
                        range_right += s;
                        s = read_next_entity(&config);
                        if (s.empty())
                        {
                            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                                "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter")) + "} " + device_name + ":" + param_name);
                        }
                        if (s == "]") break;
                    }
                    range_right += s;
                    s = read_next_entity(&config);
                    if (s.empty())
                    {
                        return emulator::Result::error(emulator::ErrorCode::ConfigError,
                            "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter")) + "} " + device_name + ":" + param_name);
                    }
                }
            } else {
                param_value = "";
                range_right = "";
            }

            std::string extended_right;
            if (s == "{")
            {
                extended_right = read_extended_entity(&config, "}");
                param_name = read_next_entity(&config);
                if (param_name.empty())
                {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{EmulatorConfig|" + std::string(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - incorrect parameters")) + "} " + device_name);
                }
            } else {
                param_name = s;
            }
            new_device->add_parameter(new_param_name, range_left, param_value, range_right, extended_right);
        }

        if (system_only && device_name == "system") break;
    }
    return emulator::Result::ok();
}

EmulatorConfigDevice * EmulatorConfig::get_device(int i)
{
    return devices[i].get();
}

EmulatorConfigDevice * EmulatorConfig::get_device(const std::string& name)
{
    for (unsigned int i=0; i<devices.size(); i++)
    {
        if (devices[i]->name == name) return devices[i].get();
    }
    return nullptr;
}

