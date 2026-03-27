// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Service functions, source

#include <algorithm>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

#include "utils.h"
#include "dsk_tools/dsk_tools.h"

std::vector<std::string> split_string(const std::string &s, char delimiter, bool skip_empty)
{
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delimiter))
    {
        if (!skip_empty || !token.empty())
            result.push_back(token);
    }
    return result;
}

std::string str_trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string str_tolower(const std::string &s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string hex_str(unsigned int value, int width)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*X", width, value);
    return std::string(buf);
}

unsigned int parse_numeric_value(std::string str)
{
    int base;
    int mult;

    if (str.empty())
        throw std::invalid_argument("Empty numeric value");

    std::string s = str;
    for (size_t i = 0; i < s.size(); i++)
        s[i] = toupper(s[i]);

    char first = s[0];
    if (first == '$') base = 16;
    else if (first == '#') base = 2;
    else base = 10;

    if (base != 10) s.erase(0, 1);

    if (s[s.length() - 1] == 'K') {
        mult = 1024;
        s.erase(s.length() - 1, 1);
    } else {
        mult = 1;
    }

    char *end;
    long value = strtol(s.c_str(), &end, base);

    if (*end != '\0') throw std::invalid_argument("Invalid numeric value: " + str);

    return value * mult;
}

unsigned int create_mask(unsigned int size, unsigned int shift)
{
    return ~(_FFFF << size) << shift;
    // (4, 4):
    //1                    FFFF
    //2                            FFF0
    //3    000F
    //4                                     00F0
}

void convert_range(const std::string &s, unsigned int * v1, unsigned int * v2)
{
    if (!s.empty())
    {
        size_t p = s.find('-');
        if (p == std::string::npos)
        {
            *v1 = parse_numeric_value(s);
            *v2 = *v1;
        } else {
            *v1 = parse_numeric_value(s.substr(0, p));
            *v2 = parse_numeric_value(s.substr(p + 1));
        }
    } else
        throw std::runtime_error("Empty range value");
}

unsigned int CalcBits(unsigned int V, unsigned int MaxBits)
{
    unsigned int result = 0;
    for (unsigned int i=0; i < MaxBits-1; i++)
        result += (V >> i) & 1;
    return result;
}

std::string find_file_location(SystemData * sd, const std::string &file_name)
{
    if (!file_name.empty())
    {
        const std::string &system_path = sd->system_path;
        const std::string &software_path = sd->software_path;
        const std::string &data_path = sd->data_path;
        std::string dir = dsk_tools::parent_dir_name(system_path);
        std::string file;

        file = system_path + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = system_path + "files/" + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = software_path + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = software_path + dir + "/" + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = data_path + file_name;
        if (dsk_tools::file_exists(file)) return file;
    }
    return "";
}

unsigned int read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, unsigned int def)
{
    std::string s = cd->get_parameter(name, required).value;
    if (s.empty()) {
        return def;
    } else {
        return parse_numeric_value(s);
    }
}

std::string read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, const std::string &def)
{
    std::string s = cd->get_parameter(name, required).value;
    if (s.empty()) {
        return def;
    } else {
        return str_tolower(s);
    }
}

bool read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, bool def)
{
    std::string s = str_tolower(cd->get_parameter(name, required).value);
    if (s.empty()) {
        return def;
    } else {
        if (s == "1" || s == "true" || s == "y" || s == "yes") return true;
        if (s == "0" || s == "false" || s == "n" || s == "no") return false;
        throw std::runtime_error("Invalid boolean value");
    }
}

bool checkCapsLock()
{
#ifdef _WIN32
    return GetKeyState(VK_CAPITAL) == 1;
#else
    return false;
#endif
}

int getRandomNumber(int min, int max) {
    static std::random_device rd;  // Источник случайности
    static std::mt19937 gen(rd()); // Генератор
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}
