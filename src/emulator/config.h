// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Emulator config functions, header

#pragma once

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "emulator/result.h"

struct EmulatorConfigParameter {
    std::string name;
    std::string left_range;
    std::string value;
    std::string right_range;
    std::string right_extended;

    //What identifies a line within its device: the name as written, prefix
    //included, plus the range on the left. A mapper has hundreds of @memory
    //lines and an interface may be wired in parts (~data[0-3], ~data[4-7]),
    //so the name alone is not enough
    std::string key() const { return name + left_range; }
    //The name without the ~ / @ prefix, to catch the same property written
    //with other modifiers
    std::string bare_name() const;
    //@memory, @port and @page are lists of ranges rather than properties
    bool is_list() const { return !name.empty() && name[0] == '@'; }
};

class EmulatorConfigDevice
{
public:
    EmulatorConfigDevice(std::string name, std::string type);
    ~EmulatorConfigDevice();

    std::string name;
    std::string type;
    std::vector<EmulatorConfigParameter> parameters;

    //Base of the numbers this configuration writes without a prefix, from
    //"radix" in the system section
    unsigned int radix = 10;

    void add_parameter(std::string name, std::string left_range, std::string value, std::string right_range, std::string right_extended);
    std::string extended_parameter(unsigned int i, std::string expected_name);
    EmulatorConfigParameter get_parameter(std::string name, bool required = true);

    //Indexes of the lines with this key(). A key is not always unique: a
    //mapper may send one range to several devices, a port may take one bit
    //field from two sources. A non-null value narrows the search to the lines
    //with that value
    std::vector<size_t> find_parameters(const std::string &key, const std::string *value = nullptr) const;
    //Replaces the first line with the same key() in place, or appends it
    void set_parameter(const EmulatorConfigParameter &p);
};

//A cursor over the text of a configuration. Every file the emulator reads a
//machine from - .cfg and .ext alike - goes through it, so the two formats
//cannot drift apart in what a value may look like
class ConfigReader
{
public:
    explicit ConfigReader(const std::string &text, size_t pos = 0): text(text), pos(pos) {}
    //The text is referenced, not copied: a temporary would dangle
    explicit ConfigReader(std::string &&, size_t = 0) = delete;

    //The next token: one of =[]{} or a stop character by itself, a quoted
    //string, or a run of text up to one of those or the end of the line.
    //Comments (//) are skipped. Empty at the end of the text
    std::string next(const std::string &stop = "");
    //Raw text up to the first stop character, which is consumed. Used for
    //the {...} part of a parameter
    std::string raw(const std::string &stop);

    bool at_end() const { return pos >= text.size(); }
    size_t position() const { return pos; }
    //1-based number of the line the cursor is on
    int line() const;

private:
    const std::string &text;
    size_t pos;

    char get() { return pos < text.size() ? text[pos++] : '\0'; }
};

//Reads the key of a parameter whose name has already been read: an optional
//[range] after it. Returns in `next` the token that follows - "=" in a valid
//parameter line
emulator::Result parse_parameter_key(ConfigReader &r, const std::string &name, std::string &left_range, std::string &next);

//Reads the rest of a parameter whose name has already been read:
//[range] = value [range] {extended}. Returns in `next` the token that follows
//it, which is empty at the end of the text. `context` names the parameter in
//error messages
emulator::Result parse_parameter(ConfigReader &r, const std::string &name, EmulatorConfigParameter &p,
                                 std::string &next, const std::string &context);

class EmulatorConfig
{
public:
    EmulatorConfig();
    ~EmulatorConfig();

    emulator::Result load_from_file(const std::string &file_name, bool system_only = false);
    emulator::Result load_from_text(const std::string &text, bool system_only = false);
    void free_devices();

    EmulatorConfigDevice * get_device(int i);
    EmulatorConfigDevice * get_device(const std::string& name);
    unsigned int get_devices_count() const { return devices.size(); }
    //False if there is no such device
    bool remove_device(const std::string& name);

    //Reads system.radix into every device. Called after loading, and again
    //after an extension has changed the system section
    emulator::Result apply_radix();

private:
    std::vector<std::unique_ptr<EmulatorConfigDevice>> devices;

    EmulatorConfigDevice *add_device(std::string device_name, std::string device_type);
};

//----------------------- Writing a configuration back ----------------------//
//The inverse of the parser above. A saved state carries the configuration it
//was taken from, already resolved, so that it depends on no other file

//Quotes a value the tokenizer would otherwise break apart. A value cannot
//contain a quote or a comment marker: next() ends a string at the first " and
//cuts everything from //, so such a value cannot come out of the parser either
std::string config_quote_value(const std::string &v);
//One parameter line, without the indent and the line break. With
//with_value false only the key is written, which is what a removal needs
std::string config_parameter_text(const EmulatorConfigParameter &p, bool with_value = true);
//The whole configuration in .cfg syntax. Devices and parameters keep the order
//they were read in - a mapper sends one range to several devices and the order
//decides which of them answers, so this is not a detail. Values are written
//back as the strings they were parsed from, so numbers keep their radix and
//their prefixes. Comments of the original are not kept
std::string serialize_config(EmulatorConfig &config);
