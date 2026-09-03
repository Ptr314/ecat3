// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scripting engine parser, header

#pragma once

#include <string>
#include <vector>

#include "emulator/result.h"
#include "emulator/script/script_types.h"

// Parses a single line of a script.
//
// Empty lines and comments produce a command with verb SCRIPT_CMD_NONE and an
// Ok result. Available separately from the file parser so that a single command
// can be pushed into the engine by an external driver.
emulator::Result parse_script_line(const std::string &line, unsigned int line_no, ScriptCommand &out);

// Parses a whole script file. Lines that fail to parse are skipped and their
// messages are collected in errors, so a single bad line does not invalidate
// the rest of the script.
emulator::Result parse_script_file(const std::string &file_name,
                                   std::vector<ScriptCommand> &out,
                                   std::vector<std::string> &errors);

// Name of a command, for messages and documentation
std::string script_verb_name(unsigned int verb);
