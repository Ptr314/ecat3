// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Service functions, source

#include <algorithm>
#include <chrono>
#include <ctime>
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

// The cast to unsigned char is the contract of ::tolower / ::toupper, and the
// strings these two get are not all ASCII: a key name or a modifier mistyped on
// a Russian layout arrives as a byte above $7F, which as a signed char is a
// negative index into the locale table - undefined, and an assert dialog in a
// debug MSVC runtime instead of the "entry is incorrect" the file deserves.
std::string str_tolower(const std::string &s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    return r;
}

std::string str_toupper(const std::string &s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c){ return static_cast<char>(::toupper(c)); });
    return r;
}

std::string hex_str(unsigned int value, int width)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*X", width, value);
    return std::string(buf);
}

// PDP-11 documentation and software are written in octal, so the machines
// built around it show register values that way
std::string oct_str(unsigned int value, int width)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*o", width, value);
    return std::string(buf);
}

std::string strip_message_context(const std::string &message)
{
    std::string result;
    size_t pos = 0;

    while (pos < message.length())
    {
        size_t open = message.find('{', pos);
        if (open == std::string::npos) {
            result += message.substr(pos);
            break;
        }

        size_t close = message.find('}', open);
        size_t sep = message.find('|', open);
        if (close == std::string::npos || sep == std::string::npos || sep > close) {
            //Not a marker, keep the text as it is
            result += message.substr(pos, open - pos + 1);
            pos = open + 1;
            continue;
        }

        result += message.substr(pos, open - pos);
        result += message.substr(sep + 1, close - sep - 1);
        pos = close + 1;
    }

    return result;
}

std::vector<std::string> split_params(const std::string &s)
{
    std::vector<std::string> result;
    std::string item;
    bool in_quotes = false;

    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if (c == '\\' && in_quotes && i + 1 < s.length() && s[i+1] == '"') {
            //An escaped quote does not close the string. Both characters are
            //kept, the consumer decides what to do with the escape.
            item += c;
            item += s[++i];
        } else if (c == '"') {
            in_quotes = !in_quotes;
            item += c;
        } else if (c == ',' && !in_quotes) {
            result.push_back(item);
            item.clear();
        } else
            item += c;
    }
    if (!item.empty() || !result.empty()) result.push_back(item);

    for (size_t i = 0; i < result.size(); i++)
    {
        std::string v = str_trim(result[i]);
        if (v.length() >= 2 && v[0] == '"' && v[v.length()-1] == '"')
            v = v.substr(1, v.length() - 2);
        result[i] = v;
    }
    return result;
}

std::string timestamp_string()
{
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", std::localtime(&t));
    return std::string(buf);
}

std::string format_number(unsigned int value, unsigned int base, unsigned int width_bits)
{
    if (width_bits != 16 && width_bits != 32) width_bits = 8;
    unsigned int mask = (width_bits >= 32)?_FFFF:((1u << width_bits) - 1);
    value &= mask;

    switch (base)
    {
        case 2: {
            std::string s(width_bits, '0');
            for (unsigned int i = 0; i < width_bits; i++)
                if ((value >> i) & 1) s[width_bits - 1 - i] = '1';
            return "#" + s;
        }
        case 8: {
            // 3 bits per digit, rounded up
            unsigned int digits = (width_bits + 2) / 3;
            std::string s(digits, '0');
            for (unsigned int i = 0; i < digits; i++)
                s[digits - 1 - i] = static_cast<char>('0' + ((value >> (i * 3)) & 7));
            return "&" + s;
        }
        case 10:
            return std::to_string(value);
        default:
            return "$" + hex_str(value, static_cast<int>(width_bits / 4));
    }
}

namespace {
    //One machine is loaded at a time, and its notation belongs to it, not to a
    //particular call site: a number without a prefix means the same thing
    //everywhere in its files
    unsigned int g_default_radix = 10;
}

void set_default_radix(unsigned int base)
{
    g_default_radix = (base == 2 || base == 8 || base == 10 || base == 16)?base:10;
}

unsigned int get_default_radix()
{
    return g_default_radix;
}

unsigned int parse_numeric_value(std::string str, unsigned int default_base)
{
    int base;
    int mult;

    if (str.empty())
        throw std::invalid_argument("Empty numeric value");

    std::string s = str;
    for (size_t i = 0; i < s.size(); i++)
        s[i] = toupper(s[i]);

    //The same prefixes format_number() writes, so a value printed by LOG can be
    //typed back in. '&' is octal, the notation every PDP-11 document uses; the
    //'#' of a PDP-11 listing means an immediate operand and is binary here.
    //'_' is decimal: in a file written in another radix it marks the values
    //that are counts and delays rather than numbers of the machine.
    char first = s[0];
    bool prefixed = true;
    if (first == '$') base = 16;
    else if (first == '&') base = 8;
    else if (first == '#') base = 2;
    else if (first == '_') base = 10;
    else {
        base = static_cast<int>((default_base != 0)?default_base:g_default_radix);
        prefixed = false;
    }

    if (prefixed) s.erase(0, 1);

    //A lone prefix ("$") leaves nothing to read
    if (s.empty()) throw std::invalid_argument("Invalid numeric value: " + str);

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
    //Bit numbers of a connection are decimal in any file: they are part of the
    //syntax of the connection, not a number of the machine
    if (!s.empty())
    {
        size_t p = s.find('-');
        if (p == std::string::npos)
        {
            *v1 = parse_numeric_value(s, 10);
            *v2 = *v1;
        } else {
            *v1 = parse_numeric_value(s.substr(0, p), 10);
            *v2 = parse_numeric_value(s.substr(p + 1), 10);
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

bool is_absolute_path(const std::string &path)
{
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    //Drive letter, e.g. C:\ or C:/
    if (path.length() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) return true;
    return false;
}

std::string resolve_output_path(SystemData * sd, const std::string &file_name)
{
    if (file_name.empty() || sd == nullptr) return file_name;
    if (is_absolute_path(file_name)) return file_name;
    if (sd->script_path.empty()) return file_name;
    return sd->script_path + file_name;
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

        // A script sets script_path while running, so files referenced by
        // COMMAND dev.load("...") are looked up next to the script first.
        if (!sd->script_path.empty())
        {
            file = sd->script_path + file_name;
            if (dsk_tools::file_exists(file)) return file;
        }

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
    }
    if (s == "1" || s == "true" || s == "y" || s == "yes") return true;
    if (s == "0" || s == "false" || s == "n" || s == "no") return false;
    throw std::runtime_error("Invalid boolean value");
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
