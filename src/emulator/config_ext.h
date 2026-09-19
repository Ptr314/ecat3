// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration extensions (.ext, .ext.zip), header
//
// An extension is a variant of a machine written as edits to its base .cfg:
//
//   @extends bk/BK-0010-01.cfg
//   @version БК-0010-01 с игрой
//   fdd0:image = game.img
//   -mapper:@memory[100000-100005]
//   @script
//   WAIT _1000
//
// The base is parsed as usual, the edits are applied to the parsed
// EmulatorConfig, and the machine is built from the result. See CONFIG.md.

#pragma once

#include <string>
#include <vector>

#include "config.h"

struct ExtEdit {
    enum Op { Set, Remove };
    Op                      op = Set;
    std::string             device;
    //For Set the whole line; for Remove only name and left_range are filled
    EmulatorConfigParameter param;
    int                     line = 0;
};

class ConfigExtension
{
public:
    std::string             extends;
    std::string             version;
    std::vector<ExtEdit>    edits;
    std::string             script;         //Text after @script, empty if none
    unsigned int            script_line = 0;    //Line of the file the script starts on

    //file_name only labels the error messages
    emulator::Result parse(const std::string &text, const std::string &file_name);
    //The text parse() reads back into the same extension. For the future
    //configuration editor, which saves what the user changed as an .ext
    std::string serialize() const;
    //Applies the edits to a parsed base. With system_only, as the machine
    //chooser loads only the system section, edits of other devices are skipped
    emulator::Result apply(EmulatorConfig &config, bool system_only = false) const;

private:
    std::string m_file_name;
    emulator::Result error_at(int line, const char * message, const std::string &detail = "") const;
};

//Where a machine was loaded from
struct MachineSource {
    std::string file;           //What was asked for: .cfg, .ext or .ext.zip
    std::string base_cfg;       //The .cfg the machine is built on
    //Directory of the extension (or of the unpacked archive), searched for
    //files before the base configuration's own. Empty for a plain .cfg
    std::string ext_path;
    std::string script;         //Script of the extension, runs after loading
    unsigned int script_line = 1;   //Line of the extension the script starts on
    bool        is_extension = false;
};

//Directories the loader needs
struct MachinePaths {
    std::string computers_path; //A relative @extends is resolved against it
    //Where inline {base64: ...} data and unpacked archives are written. With
    //no cache, a configuration that needs one fails to load
    std::string cache_path;
};

bool is_extension_file(const std::string &path);        //.ext or .ext.zip
bool is_machine_file(const std::string &path);          //.cfg, .ext or .ext.zip
//The name without .cfg / .ext / .ext.zip, for companion files (.md) and keys
std::string machine_file_stem(const std::string &path);

//The one way a machine description is read, whatever the file: a .cfg as is,
//an extension on top of its base. With system_only only the system section
//comes back (the machine chooser), and nothing is written to the cache
emulator::Result load_machine_description(const std::string &file, const MachinePaths &paths,
                                          EmulatorConfig &config, MachineSource &source,
                                          bool system_only = false);

//Replaces every parameter written as `name {base64: ...}` by a file in the
//cache holding the decoded data, so that device loaders only ever see paths.
//The name is kept at the end of the file name: loaders pick the format by
//extension
emulator::Result materialize_inline_data(EmulatorConfig &config, const std::string &cache_path);
