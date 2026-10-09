// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Model of the configuration extension editor, header
//
// What the editor of the machine chooser shows and writes: the parameters the
// devices of a machine let the user change (ComputerDevice::get_config_fields),
// their values in the base configuration and in the extension being edited,
// and the extension text built back from them. Qt-free: the window only
// displays the fields and hands the values back.

#pragma once

#include <string>
#include <vector>

#include "config_ext.h"
#include "core.h"

struct DeviceConfigField {
    std::string device;
    ConfigField field;

    //The line in the base configuration
    bool        base_present = false;
    std::string base_value;
    std::string base_extended;

    //The line in effect: the base with the extension applied. For a file
    //given inline, extended holds "base64: ..." and value the file name
    bool        present = false;
    std::string value;
    std::string extended;

    bool is_inline() const;
    bool changed() const;
};

//Constructs the devices of a parsed configuration without loading them and
//asks each for its fields. Base and current values are left empty
emulator::Result collect_config_fields(EmulatorConfig &config, std::vector<DeviceConfigField> &out);

class ExtEditModel
{
public:
    std::string file;               //The .ext being edited, empty until saved
    std::string base_cfg;           //Full path of the base
    std::string extends;            //The base as @extends writes it
    std::string base_version;       //system.version of the base
    std::string version;            //Of the extension
    ConfigExtension ext;            //Edits the editor does not know are kept as they are
    std::vector<DeviceConfigField> fields;
    size_t other_files = 0;         //Of an archive opened, beside its text

    //A .cfg starts a new extension of it; an .ext or .ext.zip is opened for
    //editing, or with derive starts a new extension standing on it, as a .cfg
    //does - the chooser's Copy, which takes the @script along. An unprotected
    //one that only sets fields is not stood on: the copy takes its base and
    //its edits, with no file name. An archive can be copied only so, since
    //@extends does not take one
    emulator::Result open(const std::string &path, const MachinePaths &paths, bool derive = false);

    //Every edit of ext is a field of the editor: nothing added, removed or wired
    bool only_field_edits() const;

    //Replaces the edits of the known fields by what the fields hold now and
    //returns the text of the extension
    std::string build();

    //Reads a file into a field as inline data
    static emulator::Result embed_file(DeviceConfigField &f, const std::string &path);
};

//"Name (n)" with the first n no machine in taken has. A "(k)" at the end of
//name is dropped first, so a copy of a copy is not "Name (1) (1)"
std::string unique_version(const std::string &name, const std::vector<std::string> &taken);

//"<dir><stem>-<n>.ext.zip" with the first n taken neither as .ext nor as
//.ext.zip: what the editor saves a new extension as
std::string unique_ext_file(const std::string &dir, const std::string &stem);
