// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration extensions (.ext, .ext.zip), source

#include "config_ext.h"
#include "utils.h"

#include <cstdio>
#include <cstring>

#include "dsk_tools/dsk_tools.h"
#include "libs/dsk_tools/src/utils.h"
#include "libs/lodepng/lodepng.h"
#include "libs/zip_reader.h"

namespace {

bool ends_with_ci(const std::string &s, const std::string &suffix)
{
    if (s.size() < suffix.size()) return false;
    return str_tolower(s.substr(s.size() - suffix.size())) == suffix;
}

bool starts_with(const std::string &s, const std::string &prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string hex8(uint32_t v)
{
    char buf[9];
    snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

//A comment after a directive needs a space before it, so that a URL
//(http://...) stays whole
std::string strip_directive_comment(const std::string &s)
{
    for (size_t i = 1; i + 1 < s.size(); i++)
        if (s[i] == '/' && s[i + 1] == '/' && (s[i - 1] == ' ' || s[i - 1] == '\t'))
            return str_trim(s.substr(0, i));
    return s;
}

std::string unquote(const std::string &s)
{
    if (s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"') return s.substr(1, s.size() - 2);
    return s;
}

std::string quote_value(const std::string &v)
{
    if (v.find_first_of("=[]{}\r\n") != std::string::npos || v != str_trim(v))
        return "\"" + v + "\"";
    return v;
}

std::string parameter_text(const EmulatorConfigParameter &p, bool with_value)
{
    std::string s = p.name + p.left_range;
    if (!with_value) return s;
    s += " =";
    if (!p.value.empty()) s += " " + quote_value(p.value) + p.right_range;
    if (!p.right_extended.empty()) s += " {" + p.right_extended + "}";
    return s;
}

void make_dirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/' || path[i] == '\\')
        {
            const std::string dir = path.substr(0, i);
            //A drive letter is not a directory to create
            if (dir.size() == 2 && dir[1] == ':') continue;
            dsk_tools::utf8_mkdir(dir);
        }
}

bool write_file(const std::string &path, const uint8_t * data, size_t size)
{
    dsk_tools::UTF8_ofstream f(path, std::ios::binary);
    if (!f.good()) return false;
    if (size > 0) f.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    f.close();
    return true;
}

//The cache is shared by every running emulator: a parallel test run loads
//the same extension several times at once. A file that already holds these
//bytes is left alone, so only the first load writes and nobody truncates a
//file another process is reading. A copy a drive has written into differs
//and is put back
bool write_cached(const std::string &path, const uint8_t * data, size_t size)
{
    if (dsk_tools::file_exists(path) && dsk_tools::utf8_file_size(path) == static_cast<long long>(size))
    {
        const std::string old = dsk_tools::utf8_read_file(path);
        if (old.size() == size && (size == 0 || memcmp(old.data(), data, size) == 0)) return true;
    }
    return write_file(path, data, size);
}

emulator::Result load_error(const char * message, const std::string &detail)
{
    return emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{EmulatorConfig|" + std::string(message) + "} " + detail);
}

//The text of the extension itself, wherever it lies: an .ext is the file, an
//.ext.zip holds exactly one of them at the top of the archive. The bytes of
//the archive and the open reader come back with the text, so that the caller
//unpacks the rest without reading the file twice; for a plain .ext archive
//stays empty. ext_name only labels the messages of the parser
emulator::Result read_extension_text(const std::string &file, ZipReader &zip,
                                     std::string &archive, std::string &text,
                                     std::string &ext_name)
{
    archive.clear();
    text.clear();
    ext_name = file;

    if (!ends_with_ci(file, ".ext.zip"))
    {
        text = dsk_tools::utf8_read_file(file);
        if (text.empty())
            return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), file);
        return emulator::Result::ok();
    }

    archive = dsk_tools::utf8_read_file(file);
    if (archive.empty())
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), file);
    if (!zip.open(archive))
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.error());

    //The extension itself is the one .ext at the top of the archive
    int ext_index = -1;
    for (size_t i = 0; i < zip.entries().size(); i++)
    {
        const std::string &n = zip.entries()[i].name;
        if (n.find('/') == std::string::npos && ends_with_ci(n, ".ext"))
        {
            if (ext_index >= 0)
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive must hold exactly one .ext file at its top level"), file);
            ext_index = static_cast<int>(i);
        }
    }
    if (ext_index < 0)
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive must hold exactly one .ext file at its top level"), file);

    std::vector<uint8_t> bytes;
    if (!zip.read(static_cast<size_t>(ext_index), bytes))
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.error());
    text.assign(bytes.begin(), bytes.end());
    ext_name = file + "/" + zip.entries()[ext_index].name;
    return emulator::Result::ok();
}

} // namespace

//----------------------------------------------------------------------------

emulator::Result ConfigExtension::error_at(int line, const char * message, const std::string &detail) const
{
    std::string where = m_file_name + ":" + std::to_string(line);
    if (!detail.empty()) where += ": " + detail;
    return load_error(message, where);
}

emulator::Result ConfigExtension::parse(const std::string &text, const std::string &file_name)
{
    m_file_name = file_name;
    extends.clear();
    version.clear();
    is_protected = false;
    edits.clear();
    script.clear();
    script_line = 0;

    size_t p = 0;
    int line_no = 0;
    //A UTF-8 byte order mark, which Windows editors like to add
    if (starts_with(text, "\xEF\xBB\xBF")) p = 3;

    while (p < text.size())
    {
        size_t eol = text.find('\n', p);
        if (eol == std::string::npos) eol = text.size();
        const std::string t = str_trim(text.substr(p, eol - p));
        line_no++;
        p = eol < text.size() ? eol + 1 : text.size();

        if (t.empty() || starts_with(t, "//")) continue;

        if (t[0] == '@')
        {
            size_t sp = t.find_first_of(" \t");
            const std::string directive = t.substr(0, sp);
            const std::string arg = sp == std::string::npos ? "" : unquote(strip_directive_comment(str_trim(t.substr(sp))));
            if (directive == "@script")
            {
                //Everything below belongs to the script, comments included
                script = text.substr(p);
                script_line = static_cast<unsigned int>(line_no) + 1;
                break;
            }
            if (directive == "@protected")
            {
                //A bare @protected means 1
                is_protected = arg.empty() || (arg != "0");
                continue;
            }
            if (directive != "@extends" && directive != "@version")
                return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Unknown directive"), directive);
            if (arg.empty())
                return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Directive without a value"), directive);
            if (directive == "@extends")
            {
                if (!extends.empty())
                    return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Duplicate directive"), directive);
                extends = arg;
            } else {
                if (!version.empty())
                    return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Duplicate directive"), directive);
                version = arg;
            }
            continue;
        }

        ExtEdit e;
        e.line = line_no;
        e.op = t[0] == '-' ? ExtEdit::Remove : ExtEdit::Set;
        const size_t start = e.op == ExtEdit::Remove ? 1 : 0;
        const size_t colon = t.find(':');
        //A removal without a property removes the device itself
        if (e.op == ExtEdit::Remove && colon == std::string::npos)
        {
            e.op = ExtEdit::RemoveDevice;
            e.device = str_trim(strip_directive_comment(t.substr(1)));
            if (e.device.empty() || e.device.find_first_of(" \t=[]{}") != std::string::npos)
                return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), t);
            edits.push_back(e);
            continue;
        }
        if (colon == std::string::npos || colon <= start)
            return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), t);
        e.device = str_trim(t.substr(start, colon - start));
        if (e.device.empty())
            return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), t);

        std::string body = t.substr(colon + 1);
        //A {...} part may run over several lines: inline data does. A brace
        //in a comment does not count; the comment needs a space before it,
        //as base64 data may itself contain "//"
        size_t open = body.find('{');
        const size_t comment = body.find(" //");
        if (comment != std::string::npos && comment < open) open = std::string::npos;
        if (open != std::string::npos && body.find('}', open) == std::string::npos)
        {
            const size_t close = text.find('}', p);
            if (close == std::string::npos)
                return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Unterminated {"), e.device);
            size_t end = text.find('\n', close);
            if (end == std::string::npos) end = text.size();
            body += "\n" + text.substr(p, end - p);
            for (size_t i = p; i < end; i++) if (text[i] == '\n') line_no++;
            line_no++;
            p = end < text.size() ? end + 1 : text.size();
        }

        //The tokenizer loses the last character of a text that ends without a
        //line break, which a .cfg never does: it ends on "}"
        body += "\n";
        ConfigReader r(body);
        const std::string name = r.next();
        if (name.empty() || name.find_first_of("=[]{}") != std::string::npos)
            return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), t);
        std::string next;
        emulator::Result res = e.op == ExtEdit::Remove
            ? parse_parameter_key(r, name, e.param.left_range, next)
            : parse_parameter(r, name, e.param, next, e.device);
        if (!res) return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter"), t);
        e.param.name = name;
        //A removal may name the value too, to pick one of several lines with
        //the same key: -mapper:@memory[177714-177715] = ay
        if (e.op == ExtEdit::Remove && next == "=")
        {
            e.param.value = r.next();
            if (e.param.value.empty() || e.param.value.find_first_of("=[]{}") != std::string::npos)
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device parameter"), t);
            next = r.next();
        }
        if (!next.empty())
            return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Unexpected text after the property"), next);
        edits.push_back(e);
    }

    if (extends.empty())
        return error_at(1, QT_TRANSLATE_NOOP("EmulatorConfig", "The extension has no @extends"));
    if (version.empty())
        return error_at(1, QT_TRANSLATE_NOOP("EmulatorConfig", "The extension has no @version"));
    return emulator::Result::ok();
}

std::string ConfigExtension::serialize() const
{
    std::string s = "@extends " + extends + "\n@version " + version + "\n";
    if (is_protected) s += "@protected\n";
    for (size_t i = 0; i < edits.size(); i++)
    {
        const ExtEdit &e = edits[i];
        if (e.op == ExtEdit::RemoveDevice) { s += "-" + e.device + "\n"; continue; }
        const bool set = e.op == ExtEdit::Set;
        std::string text = parameter_text(e.param, set);
        if (!set && !e.param.value.empty()) text += " = " + quote_value(e.param.value);
        s += (set ? "" : "-") + e.device + ":" + text + "\n";
    }
    if (!script.empty())
    {
        s += "@script\n" + script;
        if (script[script.size() - 1] != '\n') s += "\n";
    }
    return s;
}

emulator::Result ConfigExtension::apply(EmulatorConfig &config, bool system_only) const
{
    for (size_t i = 0; i < edits.size(); i++)
    {
        const ExtEdit &e = edits[i];
        if (system_only && e.device != "system") continue;

        if (e.op == ExtEdit::RemoveDevice)
        {
            //What refers to the device - a mapper range, an interface link,
            //a mix - has to go with it, or loading the machine names it
            if (e.device == "system" || !config.remove_device(e.device))
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "No such device in the base configuration"), e.device);
            continue;
        }

        EmulatorConfigDevice * dev = config.get_device(e.device);
        if (dev == nullptr)
            return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "No such device in the base configuration"), e.device);

        const std::string key = e.param.key();
        if (e.op == ExtEdit::Remove)
        {
            const std::vector<size_t> found = dev->find_parameters(key, e.param.value.empty() ? nullptr : &e.param.value);
            if (found.empty())
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "No such property in the base configuration, it must be written exactly as there"),
                                e.device + ":" + key);
            if (found.size() > 1)
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The base configuration has several lines with this key, add the value of the one to remove"),
                                "-" + e.device + ":" + key + " = " + dev->parameters[found[0]].value);
            dev->parameters.erase(dev->parameters.begin() + static_cast<std::ptrdiff_t>(found[0]));
            continue;
        }

        if (e.param.is_list())
        {
            //A range of a mapper is a list entry: the same range to the same
            //device is replaced (its options change), anything else is added
            const std::vector<size_t> found = dev->find_parameters(key, &e.param.value);
            if (found.empty())
                dev->parameters.push_back(e.param);
            else
                dev->parameters[found[0]] = e.param;
            continue;
        }

        const std::vector<size_t> found = dev->find_parameters(key);
        if (found.size() > 1)
            return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The base configuration has several lines with this key, remove them first"),
                            e.device + ":" + key);
        if (found.empty())
        {
            //A property the base has under other modifiers (~data against
            //~data[0-7]) is not replaced silently: which of them was meant
            //is for the author to say
            const std::string bare = e.param.bare_name();
            for (size_t j = 0; j < dev->parameters.size(); j++)
                if (dev->parameters[j].bare_name() == bare)
                    return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The base configuration has this property with other modifiers, remove it first"),
                                    e.device + ":" + key + " / -" + e.device + ":" + dev->parameters[j].key());
            dev->parameters.push_back(e.param);
        }
        else
            dev->parameters[found[0]] = e.param;
    }

    EmulatorConfigDevice * system = config.get_device("system");
    if (system == nullptr)
        return error_at(1, QT_TRANSLATE_NOOP("EmulatorConfig", "No such device in the base configuration"), "system");
    EmulatorConfigParameter v;
    v.name = "version";
    v.value = version;
    system->set_parameter(v);

    //The extension may have changed the radix itself
    return config.apply_radix();
}

//----------------------------------------------------------------------------

bool is_extension_file(const std::string &path)
{
    return ends_with_ci(path, ".ext") || ends_with_ci(path, ".ext.zip");
}

bool is_machine_file(const std::string &path)
{
    return ends_with_ci(path, ".cfg") || is_extension_file(path);
}

std::string machine_file_stem(const std::string &path)
{
    if (ends_with_ci(path, ".ext.zip")) return path.substr(0, path.size() - 8);
    if (ends_with_ci(path, ".ext") || ends_with_ci(path, ".cfg")) return path.substr(0, path.size() - 4);
    return path;
}

emulator::Result machine_base_file(const std::string &file, const MachinePaths &paths, std::string &base)
{
    base.clear();
    if (!is_extension_file(file))
    {
        base = file;
        return emulator::Result::ok();
    }

    std::string text, archive, ext_name;
    ZipReader zip;
    emulator::Result res = read_extension_text(file, zip, archive, text, ext_name);
    if (!res) return res;

    ConfigExtension ext;
    res = ext.parse(text, ext_name);
    if (!res) return res;

    if (is_absolute_path(ext.extends))
        base = ext.extends;
    else
        base = paths.computers_path + ext.extends;
    if (!ends_with_ci(base, ".cfg"))
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "An extension can only be based on a .cfg file"), ext.extends);
    return emulator::Result::ok();
}

emulator::Result load_machine_description(const std::string &file, const MachinePaths &paths,
                                          EmulatorConfig &config, MachineSource &source,
                                          bool system_only)
{
    source = MachineSource();
    source.file = file;
    config.free_devices();

    emulator::Result res = emulator::Result::ok();

    if (!is_extension_file(file))
    {
        source.base_cfg = file;
        res = config.load_from_file(file, system_only);
    }
    else
    {
        source.is_extension = true;
        std::string text, archive, ext_name;
        ZipReader zip;
        res = read_extension_text(file, zip, archive, text, ext_name);
        if (!res) return res;

        if (archive.empty())
            source.ext_path = dsk_tools::get_file_path(file);
        else if (!system_only)
        {
            //Unpacked next to nothing else: the directory is named after
            //the archive and its contents, so a changed archive never
            //finds the files of an older one
            if (paths.cache_path.empty())
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "No cache directory to unpack into"), file);
            const uint32_t crc = lodepng_crc32(reinterpret_cast<const unsigned char *>(archive.data()), archive.size());
            const std::string dir = paths.cache_path + "ext/" + dsk_tools::get_filename(machine_file_stem(file)) + "-" + hex8(crc) + "/";
            for (size_t i = 0; i < zip.entries().size(); i++)
                if (!ZipReader::is_safe_name(zip.entries()[i].name))
                    return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.entries()[i].name);
            make_dirs(dir);
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i < zip.entries().size(); i++)
            {
                const ZipReader::Entry &en = zip.entries()[i];
                if (en.is_dir()) continue;
                if (!zip.read(i, bytes))
                    return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.error());
                const std::string out = dir + en.name;
                make_dirs(dsk_tools::get_file_path(out));
                if (!write_cached(out, bytes.data(), bytes.size()))
                    return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error writing file"), out);
            }
            source.ext_path = dir;
        }

        ConfigExtension ext;
        res = ext.parse(text, ext_name);
        if (!res) return res;

        if (is_absolute_path(ext.extends))
            source.base_cfg = ext.extends;
        else
            source.base_cfg = paths.computers_path + ext.extends;
        if (!ends_with_ci(source.base_cfg, ".cfg"))
            return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "An extension can only be based on a .cfg file"), ext.extends);

        res = config.load_from_file(source.base_cfg, system_only);
        if (!res) return res;
        res = ext.apply(config, system_only);
        if (!res) return res;
        source.script = ext.script;
        source.script_line = ext.script_line;
        source.is_protected = ext.is_protected;
    }

    if (!res || system_only) return res;
    return materialize_inline_data(config, paths.cache_path);
}

emulator::Result materialize_inline_data(EmulatorConfig &config, const std::string &cache_path)
{
    for (unsigned int d = 0; d < config.get_devices_count(); d++)
    {
        EmulatorConfigDevice * dev = config.get_device(d);
        for (size_t i = 0; i < dev->parameters.size(); i++)
        {
            EmulatorConfigParameter &p = dev->parameters[i];
            const std::string ext = str_trim(p.right_extended);
            if (str_tolower(ext.substr(0, 7)) != "base64:") continue;

            const std::string context = dev->name + ":" + p.key();
            if (cache_path.empty())
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "No cache directory for inline data"), context);

            //The decoder accepts line breaks but no other whitespace, and
            //inline data is usually indented
            std::string encoded;
            encoded.reserve(ext.size());
            for (size_t k = 7; k < ext.size(); k++)
            {
                const char c = ext[k];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') encoded += c;
            }
            std::vector<uint8_t> data;
            try {
                data = dsk_tools::base64_decode(encoded);
            } catch (std::exception &) {
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Invalid base64 data"), context);
            }

            std::string name = dsk_tools::get_filename(p.value);
            if (name.empty()) name = "data.bin";
            const uint32_t crc = lodepng_crc32(data.data(), data.size());
            const std::string dir = cache_path + "inline/";
            const std::string path = dir + hex8(crc) + "-" + name;
            //Checked on every load: a drive may have written into the copy
            //the last time, and the machine must start from the data in the
            //configuration
            make_dirs(dir);
            if (!write_cached(path, data.data(), data.size()))
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error writing file"), path);
            p.value = path;
            p.right_extended.clear();
        }
    }
    return emulator::Result::ok();
}
