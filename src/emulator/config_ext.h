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
// EmulatorConfig, and the machine is built from the result. See docs/CONFIG.md.

#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "state.h"

struct ExtEdit {
    //RemoveDevice takes the whole device out: "-hdd", no property. AddDevice
    //is a device block written as in a .cfg: "fdc : bk-fdc { ... }"
    enum Op { Set, Remove, RemoveDevice, AddDevice };
    Op                      op = Set;
    std::string             device;
    //For Set the whole line; for Remove only name and left_range are filled,
    //for RemoveDevice and AddDevice nothing
    EmulatorConfigParameter param;
    //AddDevice: the type and the parameters of the new device
    std::string             type;
    std::vector<EmulatorConfigParameter> params;
    //From an @before line above the edit: where an AddDevice or a Set of a
    //mapper range goes instead of the end. before_device alone is a device
    //("@before mapper"); with before_key it is a line of that device
    //("@before mapper:@memory[100000-137777]", before_value picking one of
    //several lines with that key)
    std::string             before_device;
    std::string             before_key;
    std::string             before_value;
    int                     line = 0;
};

class ConfigExtension
{
public:
    std::string             extends;
    std::string             version;
    //@protected: the editor of the machine chooser does not change or delete
    //this file. A copy of it is the user's own and is not protected
    bool                    is_protected = false;
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
    emulator::Result attach_before(ExtEdit &e, const ExtEdit &pending, bool &has_before, int &placed) const;
};

//Where a machine was loaded from
struct MachineSource {
    std::string file;           //What was asked for: .cfg, .ext, .ext.zip or .ecats[.zip]
    //The .cfg the machine is built on. A saved state carries its own
    //configuration and is therefore its own base
    std::string base_cfg;
    //Directory of the extension (or of the unpacked archive), searched for
    //files before the base configuration's own. Empty for a plain .cfg
    std::string ext_path;
    std::string script;         //Script of the extension, runs after loading
    unsigned int script_line = 1;   //Line of the extension the script starts on
    bool        is_extension = false;
    bool        is_protected = false;    //@protected of the extension
    //Body of the @state section of a .ecats, applied by Emulator::run() once
    //the devices exist and have been reset. Empty for every other file
    bool        is_state = false;
    std::string state;
    unsigned int state_version = 0;
    std::string version;        //@version of a state, its name for the user
};

//Directories the loader needs
struct MachinePaths {
    std::string computers_path; //A relative @extends is resolved against it
    //Where inline {base64: ...} data and unpacked archives are written. With
    //no cache, a configuration that needs one fails to load
    std::string cache_path;
};

bool is_extension_file(const std::string &path);        //.ext or .ext.zip
bool is_state_file(const std::string &path);            //.ecats or .ecats.zip
bool is_machine_file(const std::string &path);          //any of the above, plus .cfg
//The name without its extension, for companion files (.md) and for ini keys
std::string machine_file_stem(const std::string &path);

//The text of an extension, an .ext or the one .ext inside an .ext.zip.
//ext_name labels the parser's messages; other_files, if asked, counts the
//files an archive holds beside the text (0 for a plain .ext)
emulator::Result read_extension_file(const std::string &file, std::string &text, std::string &ext_name,
                                     size_t * other_files = nullptr);
//Writes it back the same way. An existing archive keeps the files beside the
//text and the text's own name; a new one holds <stem>.ext alone
emulator::Result write_extension_file(const std::string &file, const std::string &text);

//The file a machine file is built on, without loading anything: a .cfg is its
//own base, an extension names one in @extends - a .cfg or another extension.
//The web frontend asks before it loads an extension downloaded from a link,
//because the base and its files arrive in a bundle of their own that has to
//be unpacked first
emulator::Result machine_base_file(const std::string &file, const MachinePaths &paths,
                                   std::string &base);

//The configuration an extension is built on, without the extension itself: its
//.cfg, or the chain of extensions down to one, applied. base_file is what its
//@extends resolves to. Nothing is unpacked and no inline data is written: the
//editor of the machine chooser compares against it
emulator::Result load_extension_base(const ConfigExtension &ext, const MachinePaths &paths,
                                     EmulatorConfig &config, std::string &base_file);

//The one way a machine description is read, whatever the file: a .cfg as is,
//an extension on top of its base (which may be an extension in turn, down to a
//.cfg). A .cfg that is not there is taken as an .ext of the same name, if that
//is. With system_only only the system section comes back (the machine
//chooser), and nothing is written to the cache
emulator::Result load_machine_description(const std::string &file, const MachinePaths &paths,
                                          EmulatorConfig &config, MachineSource &source,
                                          bool system_only = false);

//Replaces every parameter written as `name {base64: ...}` by a file in the
//cache holding the decoded data, so that device loaders only ever see paths.
//The name is kept at the end of the file name: loaders pick the format by
//extension
emulator::Result materialize_inline_data(EmulatorConfig &config, const std::string &cache_path);
