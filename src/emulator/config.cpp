// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Emulator config functions, source

#include "config.h"
#include "utils.h"

#include "dsk_tools/dsk_tools.h"

static emulator::Result config_error(const char * message, const std::string &detail)
{
    return emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{EmulatorConfig|" + std::string(message) + "} " + detail);
}

std::string EmulatorConfigParameter::bare_name() const
{
    size_t i = 0;
    while (i < name.size() && (name[i] == '~' || name[i] == '@')) i++;
    return name.substr(i);
}

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

std::vector<size_t> EmulatorConfigDevice::find_parameters(const std::string &key, const std::string *value) const
{
    std::vector<size_t> r;
    for (size_t i = 0; i < parameters.size(); i++)
        if (parameters[i].key() == key && (value == nullptr || parameters[i].value == *value))
            r.push_back(i);
    return r;
}

void EmulatorConfigDevice::set_parameter(const EmulatorConfigParameter &p)
{
    const std::vector<size_t> found = find_parameters(p.key());
    if (!found.empty())
        parameters[found[0]] = p;
    else
        parameters.push_back(p);
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

//----------------------------------------------------------------------------

int ConfigReader::line() const
{
    int n = 1;
    const size_t end = pos < text.size() ? pos : text.size();
    for (size_t i = 0; i < end; i++)
        if (text[i] == '\n') n++;
    return n;
}

std::string ConfigReader::next(const std::string &stop)
{
    std::string s;
    const std::string parser_spaces = " \x09\x0D\x0A";
    const std::string parser_line = "\x0D\x0A";
    const std::string parser_border = "=[]{}";
    const std::string terminator = parser_border + stop;
    std::string stop_chars;
    while (!at_end())
    {
        char c = get();
        //Skipping spaces
        while (!at_end() && parser_spaces.find(c) != std::string::npos)
            c = get();
        if (!at_end() || parser_spaces.find(c) == std::string::npos)
        {
            if (terminator.find(c) == std::string::npos)
            {
                if (c == '"')
                {
                    stop_chars = parser_line + "\"";
                    c = get();
                } else {
                    stop_chars = parser_border + parser_line + stop;
                }
                while (!at_end() && stop_chars.find(c) == std::string::npos)
                {
                    s += c;
                    c = get();
                }
                //The character that ended the token belongs to the next one,
                //except the closing quote
                if (!at_end() && c != '"') pos--;
                s = str_trim(s);
                const auto cpos = s.find("//");
                if (cpos == 0) s = "";
                if (cpos > 0 && cpos != std::string::npos) s = str_trim(s.substr(0, cpos));
                if (cpos != std::string::npos) {
                    // Skipping until end of line
                    while (!at_end() && parser_line.find(text[pos]) == std::string::npos) pos++;
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

std::string ConfigReader::raw(const std::string &stop)
{
    std::string s;
    if (!at_end())
    {
        char c = get();
        while (!at_end() && stop.find(c) == std::string::npos)
        {
            s += c;
            c = get();
        }
    }
    return s;
}

//----------------------------------------------------------------------------

emulator::Result parse_parameter_key(ConfigReader &r, const std::string &name, std::string &left_range, std::string &next)
{
    left_range.clear();
    next = r.next();
    if (next != "[") return emulator::Result::ok();

    //The range is kept as written, tokens glued together without spaces:
    //[0-15], or [#011:#011][$8000-$BFFF] for a mapper line
    left_range = "[";
    next = r.next();
    while (next != "=" && !next.empty())
    {
        left_range += next;
        next = r.next();
    }
    return emulator::Result::ok();
}

emulator::Result parse_parameter(ConfigReader &r, const std::string &name, EmulatorConfigParameter &p,
                                 std::string &next, const std::string &context)
{
    p = EmulatorConfigParameter();
    p.name = name;

    std::string s;
    emulator::Result res = parse_parameter_key(r, name, p.left_range, s);
    if (!res) return res;
    if (s != "=")
        return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter"), context + ":" + name);

    //Reading a right part
    s = r.next();
    if (s.empty())
        return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter"), context + ":" + name);

    if (s != "{")
    {
        p.value = s;
        s = r.next();
        if (s == "[")
        {
            while(1)
            {
                p.right_range += s;
                s = r.next();
                if (s.empty())
                    return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter"), context + ":" + name);
                if (s == "]") break;
            }
            p.right_range += s;
            s = r.next();
        }
    }

    if (s == "{")
    {
        p.right_extended = r.raw("}");
        s = r.next();
    }
    next = s;
    return emulator::Result::ok();
}

//----------------------------------------------------------------------------

EmulatorConfig::EmulatorConfig()
{}

EmulatorConfig::~EmulatorConfig()
{
    if (!devices.empty()) free_devices();
}

void EmulatorConfig::free_devices()
{
    devices.clear();  // Automatic cleanup via unique_ptr
}

EmulatorConfigDevice * EmulatorConfig::add_device(std::string device_name, std::string device_type)
{
    auto new_device = make_unique<EmulatorConfigDevice>(device_name, device_type);
    EmulatorConfigDevice* ptr = new_device.get();
    devices.push_back(std::move(new_device));
    return ptr;
}

emulator::Result EmulatorConfig::load_from_file(const std::string &file_name, bool system_only)
{
    if (!devices.empty()) free_devices();

    std::string config = dsk_tools::utf8_read_file(file_name);
    if (config.empty())
        return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), file_name);
    return load_from_text(config, system_only);
}

emulator::Result EmulatorConfig::load_from_text(const std::string &text, bool system_only)
{
    if (!devices.empty()) free_devices();

    ConfigReader r(text);
    while(!r.at_end())
    {
        std::string device_name = r.next(":");
        std::string device_type;
        //The usual way out: the file has ended on whitespace
        if (device_name.empty()) return apply_radix();
        if (device_name != "system")
        {
            std::string s = r.next(":");
            if (s != ":")
                return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no type found"), device_name);
            device_type = r.next();
            if (device_type.empty())
                return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no type found"), device_name);
        }
        EmulatorConfigDevice * new_device = add_device(device_name, device_type);

        if (r.next() != "{")
            return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no description found"), device_name);

        std::string param_name = r.next();
        while (param_name != "}")
        {
            if (param_name.empty() || param_name == "=" || param_name == "[" || param_name == "{")
                return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - incorrect parameters"), device_name);

            EmulatorConfigParameter p;
            std::string next;
            emulator::Result res = parse_parameter(r, param_name, p, next, device_name);
            if (!res) return res;
            new_device->parameters.push_back(p);
            param_name = next;
        }

        if (system_only && device_name == "system") break;
    }
    return apply_radix();
}

//The radix belongs to the whole file, so every device carries it: a device
//parses its own parameters, and some of them are read before load_config()
emulator::Result EmulatorConfig::apply_radix()
{
    EmulatorConfigDevice * system = get_device("system");
    if (system == nullptr) return emulator::Result::ok();

    const std::string s = system->get_parameter("radix", false).value;
    if (s.empty()) return emulator::Result::ok();

    unsigned int radix = 10;
    try {
        //Decimal by definition: the base cannot be written in the base it sets
        radix = parse_numeric_value(s, 10);
    } catch (std::exception &) {
        radix = 0;
    }
    if (radix != 2 && radix != 8 && radix != 10 && radix != 16)
        return config_error(QT_TRANSLATE_NOOP("EmulatorConfig", "radix must be 2, 8, 10 or 16"), s);

    for (size_t i = 0; i < devices.size(); i++) devices[i]->radix = radix;
    return emulator::Result::ok();
}

EmulatorConfigDevice * EmulatorConfig::get_device(int i)
{
    return devices[i].get();
}

bool EmulatorConfig::remove_device(const std::string& name)
{
    for (size_t i = 0; i < devices.size(); i++)
        if (devices[i]->name == name) {
            devices.erase(devices.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    return false;
}

EmulatorConfigDevice * EmulatorConfig::get_device(const std::string& name)
{
    for (unsigned int i=0; i<devices.size(); i++)
    {
        if (devices[i]->name == name) return devices[i].get();
    }
    return nullptr;
}

//----------------------------------------------------------------------------

std::string config_quote_value(const std::string &v)
{
    if (v.find_first_of("=[]{}\r\n") != std::string::npos || v != str_trim(v))
        return "\"" + v + "\"";
    return v;
}

std::string config_parameter_text(const EmulatorConfigParameter &p, bool with_value)
{
    std::string s = p.name + p.left_range;
    if (!with_value) return s;
    s += " =";
    if (!p.value.empty()) s += " " + config_quote_value(p.value) + p.right_range;
    if (!p.right_extended.empty()) s += " {" + p.right_extended + "}";
    return s;
}

std::string serialize_config(EmulatorConfig &config)
{
    std::string s;
    for (unsigned int i = 0; i < config.get_devices_count(); i++)
    {
        EmulatorConfigDevice * d = config.get_device(static_cast<int>(i));
        //Only the system section has no type, and load_from_text() knows it
        //by name - writing "system : " back would not parse
        s += d->type.empty() ? d->name : (d->name + " : " + d->type);
        s += " {\n";
        for (size_t j = 0; j < d->parameters.size(); j++)
            s += "\t" + config_parameter_text(d->parameters[j]) + "\n";
        s += "}\n\n";
    }
    return s;
}
