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

//------------------------------ Writing -----------------------------------//
// The inverse of the parser, used by the GUI recorder to store a buffer as a
// script file. A command that went through parse_script_line() and then
// format_script_command() parses back to the same command.

// Arguments are kept the way they appear between the quotes of a script line,
// escapes included: the engine resolves them where it matters (KEY names,
// PRINT and TYPE text). A value built from scratch goes through
// escape_script_text() first, so that a quote or a backslash survives the
// round trip through a file.
std::string escape_script_text(const std::string &s);

// Quotes an argument when it cannot be written bare: an empty string, or one
// with anything outside [A-Za-z0-9_+.-]. The text itself is not changed.
std::string format_script_arg(const std::string &s);

// One line of script text, without a line terminator
std::string format_script_command(const ScriptCommand &c);

// Writes the commands as a UTF-8 text file, one per line, LF terminated
emulator::Result write_script_file(const std::string &file_name,
                                   const std::vector<ScriptCommand> &commands);

//----------------------------- Durations ----------------------------------//
// Emulated time a command takes on replay, ms. Only the commands with a
// predictable duration count: WAIT, KEY and TYPE. WAITFOR and SCREEN take
// whatever they take and are reported as 0.
unsigned int script_command_duration_ms(const ScriptCommand &c);

// Sum of the durations of the commands in [from, to)
uint64_t script_duration_ms(const std::vector<ScriptCommand> &commands, size_t from, size_t to);
