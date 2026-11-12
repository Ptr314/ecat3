// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Starting a program by the name of its platform, header
//
// A catalog of software hands an emulator a file and the name of the platform
// it is for: "БК0010", "БК0010 (11М)", "bk10". The table
// <platforms_path>platforms.cfg, written in the syntax of a configuration,
// turns the name into a launch script by the type of the file:
//
//     bk0010 : platform {
//         names = БК0010 | БК0010 (11М) | bk10
//         bin   = bk0010-bin.ecat
//     }
//
// A launch script is an ordinary script: its MACHINE line picks the machine,
// ${file} stands for the program. The script verb PLATFORM "name", "file" is
// replaced by it when a script is loaded, and the command line (--platform),
// the web page (?platform=) and MCP all come down to that verb.

#pragma once

#include <string>
#include <vector>

#include "emulator/result.h"
#include "emulator/script/script_types.h"

//The name of the table in the platforms directory
extern const char * const PLATFORMS_TABLE;

//A platform name the way the table compares it: trimmed, every run of spaces
//made one, ASCII and Cyrillic letters in lower case
std::string platform_name_key(const std::string &name);

//Which file of an archive is the program. A catalog names its archives
//"<id>-<name>.zip": the file whose name without its extension is <name> is
//taken, otherwise the only file of a type the platform runs. types are
//extensions with the dot, in lower case. -1, and problem set, when neither
//rule picks a file
int pick_archive_member(const std::string &archive, const std::vector<std::string> &names,
                        const std::vector<std::string> &types, std::string &problem);

struct PlatformLaunch {
    std::string script;     //The launch script
    std::string program;    //The program, taken out of its archive if it came in one
};

//Finds the launch script of the platform for this file. An archive is opened
//and the program unpacked into <cache_path>launch/
emulator::Result resolve_platform(const std::string &platforms_path, const std::string &cache_path,
                                  const std::string &platform, const std::string &file,
                                  PlatformLaunch &out);

//The commands a PLATFORM command stands for: the launch script with ${file}
//filled in. Its first command is MACHINE. base_path is where a relative file
//name is looked up first: the directory of the script that holds the command
emulator::Result expand_platform_command(const ScriptCommand &c, const std::string &base_path,
                                         const std::string &platforms_path, const std::string &cache_path,
                                         std::vector<ScriptCommand> &out);

//Replaces a PLATFORM command at the head of a script by its commands. A list
//that starts with anything else is left alone
emulator::Result expand_platform(std::vector<ScriptCommand> &commands, const std::string &base_path,
                                 const std::string &platforms_path, const std::string &cache_path);

//The text of a one-line script that launches a program: what a frontend loads
//for --platform or ?platform=. The file is best given as an absolute path
std::string platform_script_text(const std::string &platform, const std::string &file);

//The name that script goes by: <cache_path>launch/platform.ecat, so that a log
//of its errors lands in the cache and not among the machines. The directory
//is created, or the log could not be opened there
std::string platform_script_name(const std::string &cache_path);
