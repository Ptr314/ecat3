// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Saved machine state (.ecats), reading and writing the text

#include "state.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "base64.h"
#include "core.h"
#include "utils.h"

#include "dsk_tools/dsk_tools.h"
//base64_decode() лежит во внутреннем заголовке dsk_tools, и звать его надо по
//полному пути: у MSVC "utils.h" из dsk_tools.h попадает в emulator/utils.h,
//потому что он ищет кавычечный include и по цепочке включающих файлов
#include "libs/dsk_tools/src/utils.h"

namespace {

emulator::Result state_error(const std::string &file, unsigned int line, const std::string &text)
{
    const std::string where = file + (line ? (" line " + std::to_string(line)) : std::string());
    return emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{MachineState|Saved state} " + where + ": " + text);
}

//A section starts on a line whose whole text is the directive. A .cfg body
//never has one: every line of it belongs to a device block, and a mapper's
//@memory line carries a range and a value
bool section_marker(const std::string &line, const char * name, std::string &argument)
{
    const std::string t = str_trim(line);
    const size_t n = strlen(name);
    if (t.compare(0, n, name) != 0) return false;
    if (t.size() == n) { argument.clear(); return true; }
    if (t[n] != ' ' && t[n] != '\t') return false;
    argument = str_trim(t.substr(n));
    return true;
}

std::string hex_digits(uint32_t value, unsigned int digits)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*X", static_cast<int>(digits), value);
    return buf;
}

} // namespace

//----------------------------- The file itself -----------------------------//

emulator::Result MachineStateFile::parse(const std::string &text, const std::string &file_name)
{
    enum { Header, Config, State } where = Header;
    unsigned int line_no = 0;
    size_t pos = 0;

    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string raw = text.substr(pos, end - pos);
        pos = end + 1;
        line_no++;

        std::string argument;
        if (section_marker(raw, "@config", argument))
        {
            //"@config" alone opens the section; a name after it would mean a
            //separate file, which a self contained state does not have
            if (!argument.empty())
                return state_error(file_name, line_no, "@config takes no argument: the configuration is written here");
            if (where != Header) return state_error(file_name, line_no, "@config comes before @state, once");
            where = Config;
            continue;
        }
        if (section_marker(raw, "@state", argument))
        {
            if (where == State) return state_error(file_name, line_no, "@state appears twice");
            state_version = ECATS_STATE_VERSION;
            if (!argument.empty())
            {
                try { state_version = parse_numeric_value(argument, 10); }
                catch (std::exception &) { return state_error(file_name, line_no, "@state: not a version number: " + argument); }
            }
            if (state_version > ECATS_STATE_VERSION)
                return state_error(file_name, line_no,
                    "written by a newer eCat3 (state version " + argument + ")");
            where = State;
            continue;
        }

        if (where == Config) { config_text += raw; config_text += "\n"; continue; }
        if (where == State)  { state_text  += raw; state_text  += "\n"; continue; }

        //Header: directives and comments
        const std::string t = str_trim(raw);
        if (t.empty() || t.compare(0, 2, "//") == 0) continue;
        if (t[0] != '@')
            return state_error(file_name, line_no, "expected a @directive before @config: " + t);

        const size_t sp = t.find_first_of(" \t");
        const std::string name = t.substr(0, sp);
        const std::string arg = sp == std::string::npos ? std::string() : str_trim(t.substr(sp));

        if (name == "@ecats")
        {
            try { container_version = parse_numeric_value(arg, 10); }
            catch (std::exception &) { return state_error(file_name, line_no, "@ecats: not a version number: " + arg); }
            if (container_version > ECATS_CONTAINER_VERSION)
                return state_error(file_name, line_no, "written by a newer eCat3 (container version " + arg + ")");
        }
        else if (name == "@version") version = arg;
        else if (name == "@machine") machine = arg;
        else if (name == "@type")    system_type = arg;
        else if (name == "@name")    system_name = arg;
        else if (name == "@saved")   saved = arg;
        else if (name == "@script")
            //A state that runs a script when it is opened is a different
            //thing, and an unpleasant surprise in a file fetched from a link
            return state_error(file_name, line_no, "a saved state cannot carry a script");
        else
            return state_error(file_name, line_no, "unknown directive: " + name);
    }

    if (container_version == 0) return state_error(file_name, 0, "no @ecats version line");
    if (where == Header) return state_error(file_name, 0, "no @config section");
    if (where != State) return state_error(file_name, 0, "no @state section");
    return emulator::Result::ok();
}

std::string MachineStateFile::header() const
{
    std::string s = "// eCat3 machine state. Everything the machine needs is in this file.\n"
                    "// Numbers carry their base: $ hex, & octal, # binary, _ decimal.\n"
                    "// A missing key keeps what a cold start gives; an unknown one is ignored.\n";
    s += "@ecats " + std::to_string(ECATS_CONTAINER_VERSION) + "\n";
    if (!version.empty())     s += "@version " + version + "\n";
    if (!machine.empty())     s += "@machine " + machine + "\n";
    if (!system_type.empty()) s += "@type " + system_type + "\n";
    if (!system_name.empty()) s += "@name " + system_name + "\n";
    if (!saved.empty())       s += "@saved " + saved + "\n";
    return s;
}

//---------------------------------- Writer ---------------------------------//

StateWriter::StateWriter(unsigned int base, StateBlobSink * blobs)
    : m_base(base), m_blobs(blobs)
{}

void StateWriter::begin_device(const std::string &name, const std::string &type)
{
    m_device = name + (type.empty() ? std::string() : (" : " + type));
    m_prefix.clear();
    m_stack.clear();
    m_device_empty = true;
}

void StateWriter::end_device()
{
    //A device that had nothing to say is left out entirely: the file stays
    //about what actually changed
    if (!m_device_empty) m_text += "}\n\n";
    m_device.clear();
}

void StateWriter::line(const std::string &text)
{
    if (m_device_empty && !m_device.empty())
    {
        m_text += m_device + " {\n";
        m_device_empty = false;
    }
    m_text += "\t" + text + "\n";
}

std::string StateWriter::num(uint32_t value, unsigned int bits, unsigned int base) const
{
    const unsigned int b = base ? base : m_base;
    //format_number() writes a decimal number bare, because in a configuration
    //that is the machine's own notation. Here every number says what base it
    //is in, so that the file reads the same whatever radix the machine uses
    if (b == 10) return "_" + format_number(value, 10, bits);
    return format_number(value, b, bits);
}

void StateWriter::u(const char * key, uint32_t value, unsigned int bits, unsigned int base)
{
    line(m_prefix + key + " = " + num(value, bits, base));
}

void StateWriter::n(const char * key, uint32_t value)
{
    line(m_prefix + key + " = _" + std::to_string(value));
}

void StateWriter::n64(const char * key, uint64_t value)
{
    line(m_prefix + key + " = _" + std::to_string(value));
}

void StateWriter::b(const char * key, bool value)
{
    line(m_prefix + key + " = " + (value ? "1" : "0"));
}

void StateWriter::s(const char * key, const std::string &value)
{
    line(m_prefix + key + " = " + config_quote_value(value));
}

void StateWriter::u_at(const char * key, unsigned int index, uint32_t value, unsigned int bits)
{
    line(m_prefix + key + "[" + std::to_string(index) + "] = " + num(value, bits, 0));
}

void StateWriter::n_at(const char * key, unsigned int index, uint32_t value)
{
    line(m_prefix + key + "[" + std::to_string(index) + "] = _" + std::to_string(value));
}

//One slice per line, each labelled with the range it covers, so that a line
//can be edited, moved or deleted on its own
void StateWriter::array(const char * key, const uint8_t * data, size_t count, unsigned int per_line)
{
    for (size_t i = 0; i < count; i += per_line)
    {
        const size_t last = (i + per_line < count) ? (i + per_line - 1) : (count - 1);
        std::string s = m_prefix + key + "[" + std::to_string(i) + "-" + std::to_string(last) + "] =";
        for (size_t k = i; k <= last; k++) s += (k == i ? " " : ", ") + num(data[k], 8, 0);
        line(s);
    }
}

void StateWriter::array(const char * key, const uint16_t * data, size_t count, unsigned int per_line)
{
    for (size_t i = 0; i < count; i += per_line)
    {
        const size_t last = (i + per_line < count) ? (i + per_line - 1) : (count - 1);
        std::string s = m_prefix + key + "[" + std::to_string(i) + "-" + std::to_string(last) + "] =";
        for (size_t k = i; k <= last; k++) s += (k == i ? " " : ", ") + num(data[k], 16, 0);
        line(s);
    }
}

void StateWriter::array(const char * key, const uint32_t * data, size_t count, unsigned int per_line)
{
    for (size_t i = 0; i < count; i += per_line)
    {
        const size_t last = (i + per_line < count) ? (i + per_line - 1) : (count - 1);
        std::string s = m_prefix + key + "[" + std::to_string(i) + "-" + std::to_string(last) + "] =";
        for (size_t k = i; k <= last; k++) s += (k == i ? " " : ", ") + num(data[k], 32, 0);
        line(s);
    }
}

void StateWriter::array(const char * key, const bool * data, size_t count, unsigned int per_line)
{
    for (size_t i = 0; i < count; i += per_line)
    {
        const size_t last = (i + per_line < count) ? (i + per_line - 1) : (count - 1);
        std::string s = m_prefix + key + "[" + std::to_string(i) + "-" + std::to_string(last) + "] = ";
        for (size_t k = i; k <= last; k++) s += data[k] ? "1" : "0";
        line(s);
    }
}

void StateWriter::hex(const char * key, const uint8_t * data, size_t size, unsigned int bits)
{
    if (size == 0) return;
    line(m_prefix + key + " = {hex:");
    m_text += encode_hex_dump(data, size, bits, "\t\t");
    m_text += "\t}\n";
}

void StateWriter::blob(const char * key, const std::string &suggested, const uint8_t * data, size_t size)
{
    if (m_blobs != nullptr)
    {
        const std::string name = m_blobs->put(suggested, data, size);
        line(m_prefix + key + " = " + config_quote_value(name));
        return;
    }
    //No archive to put it in: inline, the way a configuration embeds a file.
    //Unreadable, but a single text file cannot do better
    const std::vector<unsigned char> v(data, data + size);
    line(m_prefix + key + " = " + config_quote_value(suggested) + " {base64:");
    const std::string encoded = base64_encode(v);
    for (size_t i = 0; i < encoded.size(); i += 76)
        m_text += "\t\t" + encoded.substr(i, 76) + "\n";
    m_text += "\t}\n";
}

namespace {

const char * mode_name(unsigned int mode)
{
    switch (mode)
    {
        case MODE_OFF: return "off";
        case MODE_R:   return "r";
        case MODE_W:   return "w";
        default:       return "rw";
    }
}

//The inverse of StateWriter::line_value()
bool line_value(const std::string &s, unsigned int &out)
{
    const std::string t = str_trim(s);
    if (t == "off") { out = _FFFF; return true; }
    try { out = parse_numeric_value(t); }
    catch (std::exception &) { return false; }
    return true;
}

bool mode_value(const std::string &s, unsigned int &out)
{
    if (s == "off") { out = MODE_OFF; return true; }
    if (s == "r")   { out = MODE_R;   return true; }
    if (s == "w")   { out = MODE_W;   return true; }
    if (s == "rw")  { out = MODE_RW;  return true; }
    return false;
}

} // namespace

std::string StateWriter::line_value(unsigned int value, unsigned int mask) const
{
    //_FFFF on a line is not a number, it is "nobody is driving this". Written
    //as a value it would either be an unreadable row of ones or, masked down
    //to the width of the line, a different value altogether
    if (value == _FFFF) return "off";

    //The width of the line - widened when the value does not fit it, which
    //happens on a line whose size was narrowed by the range it was connected
    //through while its value still carries what was put there before
    unsigned int bits = (mask > 0xFFFF) ? 32 : ((mask > 0xFF) ? 16 : 8);
    while (bits < 32 && (value >> bits) != 0) bits *= 2;
    return num(value, bits, 0);
}

void StateWriter::iface(const Interface &i)
{
    unsigned int value, old_value, edge_value, mode;
    i.snapshot(value, old_value, edge_value, mode);

    std::string extra;
    if (old_value != value)  extra += std::string(extra.empty() ? "" : ", ") + "old = " + line_value(old_value, i.mask);
    if (edge_value != value) extra += std::string(extra.empty() ? "" : ", ") + "edge = " + line_value(edge_value, i.mask);
    //Only a mode the device changed after load_config() is worth writing, and
    //we cannot tell here - so it always goes out, it is three characters
    extra += std::string(extra.empty() ? "" : ", ") + "mode = " + mode_name(mode);

    line(m_prefix + "~" + i.name + " = " + line_value(value, i.mask) + " {" + extra + "}");
}

bool StateReader::iface(const std::string &name, unsigned int &value, unsigned int &old_value,
                        unsigned int &edge_value, unsigned int &mode) const
{
    const std::string full = m_prefix + "~" + name;
    const EmulatorConfigParameter * p = find(full);
    if (p == nullptr) return false;

    if (!line_value(p->value, value)) { set_error(full, "not a line value: " + p->value); return false; }
    //Anything the block does not mention follows the value, which is what it
    //means for a line that has been quiet
    old_value = value;
    edge_value = value;

    const std::vector<std::string> items = split_params(p->right_extended);
    for (size_t i = 0; i < items.size(); i++)
    {
        const size_t eq = items[i].find('=');
        if (eq == std::string::npos) continue;
        const std::string key = str_trim(items[i].substr(0, eq));
        const std::string val = str_trim(items[i].substr(eq + 1));
        if (key == "old")       { if (!line_value(val, old_value))  { set_error(full, "not a line value: " + val); return false; } }
        else if (key == "edge") { if (!line_value(val, edge_value)) { set_error(full, "not a line value: " + val); return false; } }
        else if (key == "mode" && !mode_value(val, mode))
            { set_error(full, "unknown mode: " + val); return false; }
    }
    return true;
}

void StateWriter::push(const char * name)
{
    m_stack.push_back(m_prefix);
    m_prefix += std::string(name) + ".";
}

void StateWriter::pop()
{
    if (m_stack.empty()) return;
    m_prefix = m_stack.back();
    m_stack.pop_back();
}

//---------------------------------- Reader ---------------------------------//

StateReader::StateReader(EmulatorConfigDevice * device, SystemData * sd,
                         std::vector<char> * used, std::string * error)
    : m_dev(device), m_sd(sd), m_used(used), m_error(error)
{}

const std::string & StateReader::device_name() const
{
    static const std::string none;
    return m_dev ? m_dev->name : none;
}

const std::string & StateReader::error() const
{
    static const std::string none;
    return m_error ? *m_error : none;
}

void StateReader::set_error(const std::string &key, const std::string &text) const
{
    if (m_error == nullptr) return;
    if (!m_error->empty()) return;
    *m_error = device_name() + "." + key + ": " + text;
}

const EmulatorConfigParameter * StateReader::find(const std::string &key) const
{
    if (m_dev == nullptr) return nullptr;
    for (size_t i = 0; i < m_dev->parameters.size(); i++)
        if (m_dev->parameters[i].key() == key)
        {
            if (m_used && i < m_used->size()) (*m_used)[i] = 1;
            return &m_dev->parameters[i];
        }
    return nullptr;
}

bool StateReader::value_of(const char * key, uint32_t &out) const
{
    const std::string full = m_prefix + key;
    const EmulatorConfigParameter * p = find(full);
    if (p == nullptr) return false;
    try { out = parse_numeric_value(p->value); }
    catch (std::exception &e) { set_error(full, e.what()); return false; }
    return true;
}

bool StateReader::u(const char * key, uint32_t &out) const
{
    return value_of(key, out);
}

bool StateReader::u(const char * key, uint16_t &out) const
{
    uint32_t v = 0;
    if (!value_of(key, v)) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

bool StateReader::u(const char * key, uint8_t &out) const
{
    uint32_t v = 0;
    if (!value_of(key, v)) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

bool StateReader::u(const char * key, int &out) const
{
    uint32_t v = 0;
    if (!value_of(key, v)) return false;
    out = static_cast<int>(v);
    return true;
}

bool StateReader::n64(const char * key, uint64_t &out) const
{
    const std::string full = m_prefix + key;
    const EmulatorConfigParameter * p = find(full);
    if (p == nullptr) return false;
    //parse_numeric_value() is 32 bit, and a cycle counter is not. The prefix
    //is still honoured, because a hand written value may carry one
    std::string s = str_trim(p->value);
    unsigned int base = 10;
    if (!s.empty() && (s[0] == '$' || s[0] == '&' || s[0] == '#' || s[0] == '_'))
    {
        base = (s[0] == '$') ? 16 : (s[0] == '&') ? 8 : (s[0] == '#') ? 2 : 10;
        s = s.substr(1);
    }
    else base = get_default_radix();
    if (s.empty()) { set_error(full, "empty value"); return false; }
    uint64_t v = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
        unsigned int d;
        const char c = s[i];
        if (c >= '0' && c <= '9') d = static_cast<unsigned int>(c - '0');
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned int>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned int>(c - 'a' + 10);
        else { set_error(full, "not a number: " + p->value); return false; }
        if (d >= base) { set_error(full, "not a number: " + p->value); return false; }
        v = v * base + d;
    }
    out = v;
    return true;
}

bool StateReader::b(const char * key, bool &out) const
{
    uint32_t v = 0;
    if (!value_of(key, v)) return false;
    out = (v != 0);
    return true;
}

bool StateReader::s(const char * key, std::string &out) const
{
    const EmulatorConfigParameter * p = find(m_prefix + key);
    if (p == nullptr) return false;
    out = p->value;
    return true;
}

bool StateReader::u_at(const char * key, unsigned int index, uint32_t &out) const
{
    const std::string full = m_prefix + key + "[" + std::to_string(index) + "]";
    const EmulatorConfigParameter * p = find(full);
    if (p == nullptr) return false;
    try { out = parse_numeric_value(p->value); }
    catch (std::exception &e) { set_error(full, e.what()); return false; }
    return true;
}

template<typename T>
bool StateReader::read_array(const char * key, T * data, size_t count) const
{
    if (m_dev == nullptr) return false;
    const std::string name = m_prefix + key;
    bool found = false;

    for (size_t i = 0; i < m_dev->parameters.size(); i++)
    {
        const EmulatorConfigParameter &p = m_dev->parameters[i];
        if (p.name != name || p.left_range.empty()) continue;
        if (p.left_range.size() < 3 || p.left_range[0] != '['
            || p.left_range[p.left_range.size() - 1] != ']') continue;

        unsigned int from = 0, to = 0;
        try { convert_range(p.left_range.substr(1, p.left_range.size() - 2), &from, &to); }
        catch (std::exception &e) { set_error(name, e.what()); return found; }

        if (m_used && i < m_used->size()) (*m_used)[i] = 1;
        found = true;

        //A bit array is written as a run of 0 and 1 without separators: one
        //line of 64 flags is a line, not a page
        const std::string trimmed = str_trim(p.value);
        const bool packed = trimmed.find(',') == std::string::npos && trimmed.size() > 1
                            && trimmed.find_first_not_of("01") == std::string::npos;
        std::vector<std::string> items;
        if (packed)
            for (size_t k = 0; k < trimmed.size(); k++) items.push_back(std::string(1, trimmed[k]));
        else
            items = split_params(p.value);

        for (size_t k = 0; k < items.size(); k++)
        {
            const size_t index = from + k;
            if (index > to || index >= count) break;
            try { data[index] = static_cast<T>(parse_numeric_value(items[k])); }
            catch (std::exception &e) { set_error(name, e.what()); return found; }
        }
    }
    return found;
}

bool StateReader::array(const char * key, uint8_t * data, size_t count) const
{
    return read_array<uint8_t>(key, data, count);
}

bool StateReader::array(const char * key, uint16_t * data, size_t count) const
{
    return read_array<uint16_t>(key, data, count);
}

bool StateReader::array(const char * key, uint32_t * data, size_t count) const
{
    return read_array<uint32_t>(key, data, count);
}

bool StateReader::array(const char * key, bool * data, size_t count) const
{
    return read_array<bool>(key, data, count);
}

bool StateReader::hex(const char * key, uint8_t * data, size_t size, unsigned int bits) const
{
    const std::string full = m_prefix + key;
    const EmulatorConfigParameter * p = find(full);
    if (p == nullptr) return false;
    std::string body = str_trim(p->right_extended);
    if (body.compare(0, 4, "hex:") != 0) { set_error(full, "not a hex dump"); return false; }
    body = body.substr(4);

    std::string error;
    if (!decode_hex_dump(body, data, size, bits, error)) { set_error(full, error); return false; }
    return true;
}

bool StateReader::blob(const char * key, std::vector<uint8_t> &out) const
{
    const std::string full = m_prefix + key;
    const EmulatorConfigParameter * p = find(full);
    if (p == nullptr) return false;

    const std::string extended = str_trim(p->right_extended);
    if (extended.compare(0, 7, "base64:") == 0)
    {
        std::string encoded;
        for (size_t i = 7; i < extended.size(); i++)
        {
            const char c = extended[i];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') encoded += c;
        }
        try { out = dsk_tools::base64_decode(encoded); }
        catch (std::exception &) { set_error(full, "invalid base64 data"); return false; }
        return true;
    }

    //An ordinary member of the archive, found next to the state
    const std::string path = find_file_location(m_sd, p->value);
    if (path.empty()) { set_error(full, "file not found: " + p->value); return false; }
    const std::string data = dsk_tools::utf8_read_file(path);
    out.assign(data.begin(), data.end());
    return true;
}

bool StateReader::blob_into(const char * key, uint8_t * data, size_t size) const
{
    std::vector<uint8_t> v;
    if (!blob(key, v)) return false;
    if (v.size() != size)
    {
        set_error(m_prefix + key, "expected " + std::to_string(size) + " bytes, got "
                  + std::to_string(v.size()));
        return false;
    }
    memcpy(data, v.data(), size);
    return true;
}

StateReader StateReader::sub(const char * name) const
{
    StateReader r(m_dev, m_sd, m_used);
    r.m_prefix = m_prefix + name + ".";
    r.m_error = m_error;
    return r;
}

//---------------------------------- Hex dump -------------------------------//

std::string encode_hex_dump(const uint8_t * data, size_t size, unsigned int bits,
                            const std::string &indent)
{
    const unsigned int unit = (bits == 16) ? 2 : 1;      //Bytes per value
    const unsigned int digits = unit * 2;
    const size_t per_line = 16;                          //Bytes, either way

    //Wide enough for the last address, and never narrower than four digits
    unsigned int addr_digits = 4;
    while ((size >> (addr_digits * 4)) != 0) addr_digits++;

    std::string out;
    bool folding = false;
    std::string previous;

    for (size_t a = 0; a < size; a += per_line)
    {
        const size_t n = (a + per_line <= size) ? per_line : (size - a);
        std::string values;
        for (size_t k = 0; k < n; k += unit)
        {
            uint32_t v = data[a + k];
            if (unit == 2) v |= static_cast<uint32_t>(k + 1 < n ? data[a + k + 1] : 0) << 8;
            values += (k ? " " : "") + hex_digits(v, digits);
        }

        //A line identical to the one before it becomes a single '*', however
        //many of them follow: an untouched RAM is three lines
        if (n == per_line && values == previous)
        {
            if (!folding) { out += indent + "*\n"; folding = true; }
            continue;
        }
        folding = false;
        previous = (n == per_line) ? values : std::string();
        out += indent + hex_digits(static_cast<uint32_t>(a), addr_digits) + ": " + values + "\n";
    }
    return out;
}

bool decode_hex_dump(const std::string &text, uint8_t * data, size_t size, unsigned int bits,
                     std::string &error)
{
    const unsigned int unit = (bits == 16) ? 2 : 1;
    const size_t per_line = 16;

    std::vector<uint8_t> pattern;       //The line a '*' repeats
    size_t repeat_from = 0;             //Where the repetition starts
    bool repeating = false;

    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = str_trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) { if (end == text.size()) break; else continue; }

        if (line == "*")
        {
            if (pattern.empty()) { error = "a '*' before any line to repeat"; return false; }
            repeating = true;
            continue;
        }

        const size_t colon = line.find(':');
        if (colon == std::string::npos) { error = "line without an address: " + line; return false; }

        size_t address = 0;
        try { address = parse_numeric_value("$" + str_trim(line.substr(0, colon))); }
        catch (std::exception &) { error = "bad address: " + line; return false; }

        //Everything between the repeated line and this address is that line
        if (repeating)
        {
            for (size_t a = repeat_from; a < address && a < size; a += pattern.size())
                for (size_t k = 0; k < pattern.size() && a + k < size && a + k < address; k++)
                    data[a + k] = pattern[k];
            repeating = false;
        }

        std::vector<std::string> items = split_string(str_trim(line.substr(colon + 1)), ' ', true);
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < items.size(); i++)
        {
            uint32_t v = 0;
            try { v = parse_numeric_value("$" + items[i]); }
            catch (std::exception &) { error = "bad value '" + items[i] + "' at " + line.substr(0, colon); return false; }
            bytes.push_back(static_cast<uint8_t>(v & 0xFF));
            if (unit == 2) bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }
        for (size_t k = 0; k < bytes.size() && address + k < size; k++) data[address + k] = bytes[k];

        if (bytes.size() == per_line) pattern = bytes; else pattern.clear();
        repeat_from = address + bytes.size();
    }

    //A '*' as the last line fills everything that is left
    if (repeating && !pattern.empty())
        for (size_t a = repeat_from; a < size; a += pattern.size())
            for (size_t k = 0; k < pattern.size() && a + k < size; k++)
                data[a + k] = pattern[k];

    return true;
}
