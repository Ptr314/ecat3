// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Service functions, header

#pragma once

#include <string>
#include <vector>

#include "emulator/config.h"
#include "emulator/core.h"

#define _FFFF (unsigned int)(-1)

unsigned int parse_numeric_value(std::string str);

unsigned int create_mask(unsigned int size, unsigned int shift);

void convert_range(const std::string &s, unsigned int * v1, unsigned int * v2);

unsigned int CalcBits(unsigned int V, unsigned int MaxBits = 32);

std::string find_file_location(SystemData * sd, const std::string &file_name);

// Where a file written by a script command (tape.save, fdd.save, ram.save)
// goes. A relative name lands next to the script, the way a screenshot does;
// with no script running the name is left alone and resolves against the
// working directory, as it did before.
std::string resolve_output_path(SystemData * sd, const std::string &file_name);

// True for "/x", "\\x", "C:/x" and "C:\x". Used to decide whether a name given
// on the command line or in a script has to be resolved against a base path.
bool is_absolute_path(const std::string &path);

unsigned int read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, unsigned int def);
std::string read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, const std::string &def);
bool read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, bool def);

bool checkCapsLock();

int getRandomNumber(int min, int max);

std::vector<std::string> split_string(const std::string &s, char delimiter, bool skip_empty = false);
std::string str_trim(const std::string &s);
std::string str_tolower(const std::string &s);
std::string str_toupper(const std::string &s);
std::string hex_str(unsigned int value, int width);
std::string oct_str(unsigned int value, int width);

// Current local time as YYYY-MM-DD-HH-MM-SS, used to build log file names
std::string timestamp_string();

// Removes the {Context|Text} translation markers used in Result::message and
// leaves the plain text. The GUI translates them instead, see
// translateResultMessage() in dialogs/genericdbgwnd.h.
std::string strip_message_context(const std::string &message);

// Splits a comma separated parameter list, ignoring commas inside double quotes.
// Every item is trimmed and surrounding quotes are stripped.
// Used both by the script parser and by device send_command() implementations.
std::vector<std::string> split_params(const std::string &s);

// Formats a value according to the current LOGDEFS settings:
// base 16 -> $XX, base 2 -> #0101, base 8 -> &777, base 10 -> plain decimal.
// width_bits is 8 or 16 and defines zero padding for bases 2, 8 and 16.
std::string format_number(unsigned int value, unsigned int base, unsigned int width_bits);

// std::make_unique copy for C++11 and Mingw 4.9.2 compatibility
#if __cplusplus >= 201402L || defined(_MSC_VER)
using std::make_unique;
#else
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif
