// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration extensions (.ext, .ext.zip), source

#include "config_ext.h"
#include "cache.h"
#include "utils.h"

#include <cstdio>
#include <cstring>

#include "dsk_tools/core.h"
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

//"name : type {" - a colon with a lone word after it and the brace on the same
//line. A device:property line always has "=" before any brace
bool is_device_block_start(const std::string &s)
{
    const size_t colon = s.find(':');
    if (colon == std::string::npos) return false;
    const std::string name = str_trim(s.substr(0, colon));
    if (name.empty() || name.find_first_of(" \t=[]{}") != std::string::npos) return false;
    const std::string rest = s.substr(colon + 1);
    const size_t brace = rest.find('{');
    if (brace == std::string::npos) return false;
    const std::string type = str_trim(rest.substr(0, brace));
    return !type.empty() && type.find_first_of(" \t=[]{}:\"") == std::string::npos;
}

std::string unquote(const std::string &s)
{
    if (s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"') return s.substr(1, s.size() - 2);
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

//What one archive may unpack into, and how many files it may hold. A browser
//unpacks into the memory of its tab, hence less there. Far above anything a
//machine carries: the largest real member is a hard disk image of tens of MB
#ifdef WASM_BUILD
const uint64_t ARCHIVE_UNPACKED_LIMIT = 256ULL << 20;
#else
const uint64_t ARCHIVE_UNPACKED_LIMIT = 1ULL << 30;
#endif
const size_t ARCHIVE_ENTRIES_LIMIT = 4096;

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
//suffix is what the one text member of the archive is called: ".ext" for a
//configuration extension, ".ecats" for a saved state
emulator::Result read_extension_text(const std::string &file, ZipReader &zip,
                                     std::string &archive, std::string &text,
                                     std::string &ext_name, const std::string &suffix = ".ext")
{
    archive.clear();
    text.clear();
    ext_name = file;

    if (!ends_with_ci(file, suffix + ".zip"))
    {
        text = dsk_tools::utf8_read_file(file);
        if (text.empty())
            return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), file);
        return emulator::Result::ok();
    }

    //The archive is read whole, so its own size is bounded first. A missing
    //file has size -1, which the cast would turn into "too large"
    const auto archive_size = dsk_tools::utf8_file_size(file);
    if (archive_size < 0)
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), file);
    if (static_cast<uint64_t>(archive_size) > ARCHIVE_UNPACKED_LIMIT)
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive is too large"), file);
    archive = dsk_tools::utf8_read_file(file);
    if (archive.empty())
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), file);
    if (!zip.open(archive))
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.error());

    //Each entry is bounded by ZipReader, but not their sum: a few megabytes of
    //compressed zeros unpack into gigabytes - on the disk, or in the memory of
    //a browser tab that followed a link. The sizes in the directory are what
    //unpacking will write: ZipReader::read() refuses an entry whose data comes
    //out of any other size. So the sum is checked before a byte is written
    if (zip.entries().size() > ARCHIVE_ENTRIES_LIMIT)
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive holds too many files"),
                          file + " (" + std::to_string(zip.entries().size()) + ")");
    uint64_t unpacked = 0;
    for (size_t i = 0; i < zip.entries().size(); i++) unpacked += zip.entries()[i].size;
    if (unpacked > ARCHIVE_UNPACKED_LIMIT)
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive is too large"),
                          file + " (" + std::to_string(unpacked >> 20) + " MB)");

    //The text itself is the one member with that suffix at the top of the
    //archive; everything else beside it is a file the machine loads
    int ext_index = -1;
    for (size_t i = 0; i < zip.entries().size(); i++)
    {
        const std::string &n = zip.entries()[i].name;
        if (n.find('/') == std::string::npos && ends_with_ci(n, suffix))
        {
            if (ext_index >= 0)
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive must hold exactly one file of its own type at its top level"), file + " (*" + suffix + ")");
            ext_index = static_cast<int>(i);
        }
    }
    if (ext_index < 0)
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The archive must hold exactly one file of its own type at its top level"), file + " (*" + suffix + ")");

    std::vector<uint8_t> bytes;
    if (!zip.read(static_cast<size_t>(ext_index), bytes))
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.error());
    text.assign(bytes.begin(), bytes.end());
    ext_name = file + "/" + zip.entries()[ext_index].name;
    return emulator::Result::ok();
}

//Unpacks everything beside the text into a directory of its own, named after
//the archive and its contents so that a changed archive never finds the files
//of an older one. That directory becomes MachineSource::ext_path, which
//find_file_location() searches first - which is the whole reason a packed
//machine needs no support in any device loader
emulator::Result unpack_archive(const std::string &file, const std::string &cache_path,
                                ZipReader &zip, const std::string &archive, std::string &dir_out)
{
    if (cache_path.empty())
        return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "No cache directory to unpack into"), file);
    const uint32_t crc = lodepng_crc32(reinterpret_cast<const unsigned char *>(archive.data()), archive.size());
    const std::string dir = cache_path + "ext/" + dsk_tools::get_filename(machine_file_stem(file)) + "-" + hex8(crc) + "/";
    for (size_t i = 0; i < zip.entries().size(); i++)
        if (!ZipReader::is_safe_name(zip.entries()[i].name))
            return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading the archive"), file + ": " + zip.entries()[i].name);
    make_dirs(dir);
    //Used now: the cache cleanup goes by the time of this directory, and a
    //load that finds every file already there writes nothing that would move it
    cache_touch(dir);
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
    dir_out = dir;
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

//Gives the edit the place an @before above it asked for. Only two edits have a
//place: a new device (before a device) and a mapper range (before a line of
//the same device). An @before covers the whole run of such edits that follows
//it - each goes in front of the same place, so the run keeps its order - and
//the first edit of another kind ends the run. An @before that placed nothing
//is a mistake worth saying; placed counts the edits of the run so far
emulator::Result ConfigExtension::attach_before(ExtEdit &e, const ExtEdit &pending, bool &has_before, int &placed) const
{
    if (!has_before) return emulator::Result::ok();
    const bool device_place = pending.before_key.empty();
    const bool fits = e.op == ExtEdit::AddDevice
        ? device_place
        : (e.op == ExtEdit::Set && e.param.is_list() && !device_place && pending.before_device == e.device);
    if (!fits)
    {
        if (placed == 0)
            return error_at(pending.line, QT_TRANSLATE_NOOP("EmulatorConfig", "@before must be followed by a device block or a mapper range"));
        has_before = false;
        return emulator::Result::ok();
    }
    e.before_device = pending.before_device;
    e.before_key = pending.before_key;
    e.before_value = pending.before_value;
    placed++;
    return emulator::Result::ok();
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

    //A device block is read by the tokenizer of the .cfg straight from the
    //text. It loses the last character of a text without a final line break,
    //which would be the closing brace of a block at the very end
    const std::string padded = text + "\n";

    //An @before and the run of edits it places
    ExtEdit pending_before;
    bool has_before = false;
    int before_placed = 0;

    while (p < text.size())
    {
        const size_t line_begin = p;
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
                if (has_before && before_placed == 0)
                    return error_at(pending_before.line, QT_TRANSLATE_NOOP("EmulatorConfig", "@before must be followed by a device block or a mapper range"));
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
            if (directive == "@before")
            {
                //Where the next run of edits goes: before a device, or before
                //a line of a device - the key as in a removal, with the value
                //when the key is not unique
                if (has_before && before_placed == 0)
                    return error_at(pending_before.line, QT_TRANSLATE_NOOP("EmulatorConfig", "@before must be followed by a device block or a mapper range"));
                before_placed = 0;
                if (arg.empty())
                    return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Directive without a value"), directive);
                pending_before = ExtEdit();
                pending_before.line = line_no;
                const size_t c = arg.find(':');
                if (c == std::string::npos)
                {
                    pending_before.before_device = arg;
                    if (arg.find_first_of(" \t=[]{}") != std::string::npos)
                        return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), arg);
                } else {
                    pending_before.before_device = str_trim(arg.substr(0, c));
                    const std::string body = arg.substr(c + 1) + "\n";
                    ConfigReader r(body);
                    const std::string name = r.next();
                    std::string range, next;
                    if (pending_before.before_device.empty() || name.empty() || name.find_first_of("=[]{}") != std::string::npos)
                        return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), arg);
                    parse_parameter_key(r, name, range, next);
                    pending_before.before_key = name + range;
                    if (next == "=")
                    {
                        pending_before.before_value = r.next();
                        next = r.next();
                    }
                    if (!next.empty())
                        return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Unexpected text after the property"), next);
                }
                has_before = true;
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

        //A new device, written as in a .cfg and read by the same code
        if (t[0] != '-' && is_device_block_start(strip_directive_comment(t)))
        {
            ExtEdit e;
            e.op = ExtEdit::AddDevice;
            e.line = line_no;
            ConfigReader r(padded, line_begin);
            e.device = r.next(":");
            if (r.next(":") != ":")
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no type found"), e.device);
            e.type = r.next();
            if (r.next() != "{")
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - no description found"), e.device);
            if (e.device == "system")
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The device already exists in the base configuration"), e.device);
            EmulatorConfigDevice dev(e.device, e.type);
            emulator::Result res = parse_device_parameters(r, dev);
            if (!res)
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Configuration error for device - incorrect parameters"), e.device);
            e.params = dev.parameters;

            //Nothing but a comment may follow the closing brace on its line
            const size_t q = r.position();
            size_t end = padded.find('\n', q);
            if (end == std::string::npos) end = padded.size();
            const std::string rest = str_trim(padded.substr(q, end - q));
            if (!rest.empty() && !starts_with(rest, "//"))
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "Unexpected text after the property"), rest);
            for (size_t i = line_begin; i < end && i < text.size(); i++)
                if (text[i] == '\n') line_no++;
            p = end < text.size() ? end + 1 : text.size();

            res = attach_before(e, pending_before, has_before, before_placed);
            if (!res) return res;
            edits.push_back(e);
            continue;
        }

        ExtEdit e;
        e.line = line_no;
        e.op = t[0] == '-' ? ExtEdit::Remove : ExtEdit::Set;
        const size_t start = e.op == ExtEdit::Remove ? 1 : 0;
        //The colon of device:property, looked for before the comment: in
        //"-hdd // note: not fitted" the only colon belongs to the note. The
        //device part comes first, so a colon found here is at the same place
        //in t
        const size_t colon = strip_directive_comment(t).find(':');
        //A removal without a property removes the device itself
        if (e.op == ExtEdit::Remove && colon == std::string::npos)
        {
            e.op = ExtEdit::RemoveDevice;
            e.device = str_trim(strip_directive_comment(t.substr(1)));
            if (e.device.empty() || e.device.find_first_of(" \t=[]{}") != std::string::npos)
                return error_at(line_no, QT_TRANSLATE_NOOP("EmulatorConfig", "Expected device:property"), t);
            emulator::Result res = attach_before(e, pending_before, has_before, before_placed);
            if (!res) return res;
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
        res = attach_before(e, pending_before, has_before, before_placed);
        if (!res) return res;
        edits.push_back(e);
    }

    if (has_before && before_placed == 0)
        return error_at(pending_before.line, QT_TRANSLATE_NOOP("EmulatorConfig", "@before must be followed by a device block or a mapper range"));
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
        //One @before for a run: the edit before this one went to the same
        //place and is of the same kind, so the run read back is the same
        const ExtEdit * prev = i > 0 ? &edits[i - 1] : nullptr;
        const bool same_run = prev != nullptr && prev->op == e.op && prev->device == e.device
            && prev->before_device == e.before_device && prev->before_key == e.before_key
            && prev->before_value == e.before_value;
        const bool same_kind_run = prev != nullptr && prev->op == e.op && e.op == ExtEdit::AddDevice
            && prev->before_device == e.before_device && prev->before_key.empty() && e.before_key.empty();
        if (!e.before_device.empty() && !same_run && !same_kind_run)
        {
            s += "@before " + e.before_device;
            if (!e.before_key.empty()) s += ":" + e.before_key;
            if (!e.before_value.empty()) s += " = " + config_quote_value(e.before_value);
            s += "\n";
        }
        if (e.op == ExtEdit::AddDevice) { s += config_device_text(e.device, e.type, e.params); continue; }
        if (e.op == ExtEdit::RemoveDevice) { s += "-" + e.device + "\n"; continue; }
        const bool set = e.op == ExtEdit::Set;
        std::string text = config_parameter_text(e.param, set);
        if (!set && !e.param.value.empty()) text += " = " + config_quote_value(e.param.value);
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

        if (e.op == ExtEdit::AddDevice)
        {
            //A name taken twice would leave one of the two unreachable: every
            //lookup by name finds the first
            if (config.get_device(e.device) != nullptr)
                return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The device already exists in the base configuration"), e.device);
            //At the end unless placed: the order is the order of clocking and
            //of reset, which some machines depend on
            size_t index = config.get_devices_count();
            if (!e.before_device.empty())
            {
                const int at = config.device_index(e.before_device);
                if (at < 0)
                    return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "No such device in the base configuration"), e.before_device);
                index = static_cast<size_t>(at);
            }
            EmulatorConfigDevice * added = config.insert_device(e.device, e.type, index);
            added->parameters = e.params;
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
            if (!e.before_key.empty())
            {
                //Placed: the order of the ranges decides who answers. A line
                //the base already has is refused rather than moved: in a run
                //under one @before, the change of options of an existing
                //line would otherwise move it along with the new ones. A line
                //this extension has itself added above is not the base's:
                //one range may go to one device twice, for reading and for
                //writing (the Орион М3 controller does)
                bool in_base = false;
                for (size_t k = 0; k < found.size() && !in_base; k++)
                {
                    const EmulatorConfigParameter &line = dev->parameters[found[k]];
                    bool added = false;
                    for (size_t j = 0; j < i && !added; j++)
                    {
                        const ExtEdit &p = edits[j];
                        added = p.op == ExtEdit::Set && p.device == e.device && p.param.key() == line.key()
                             && p.param.value == line.value && p.param.right_range == line.right_range
                             && p.param.right_extended == line.right_extended;
                    }
                    in_base = !added;
                }
                if (in_base)
                    return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "This line is already in the base configuration: change it outside @before, or remove it first"),
                                    e.device + ":" + key + " = " + e.param.value);
                const std::vector<size_t> at = dev->find_parameters(e.before_key, e.before_value.empty() ? nullptr : &e.before_value);
                if (at.empty())
                    return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "No such property in the base configuration, it must be written exactly as there"),
                                    "@before " + e.device + ":" + e.before_key);
                if (at.size() > 1)
                    return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The base configuration has several lines with this key, add the value of the one meant"),
                                    "@before " + e.device + ":" + e.before_key + " = " + dev->parameters[at[0]].value);
                dev->parameters.insert(dev->parameters.begin() + static_cast<std::ptrdiff_t>(at[0]), e.param);
                continue;
            }
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
            //~data[0-7], data against ~data) is not replaced silently: which
            //of them was meant is for the author to say. Another part of an
            //interface wired in parts (~config[3] beside ~config[0-1]) is no
            //such case: both name their bits, and it is added
            const std::string bare = e.param.bare_name();
            for (size_t j = 0; j < dev->parameters.size(); j++)
            {
                const EmulatorConfigParameter &b = dev->parameters[j];
                const bool another_part = b.name == e.param.name && !b.left_range.empty() && !e.param.left_range.empty();
                if (b.bare_name() == bare && !another_part)
                    return error_at(e.line, QT_TRANSLATE_NOOP("EmulatorConfig", "The base configuration has this property with other modifiers, remove it first"),
                                    e.device + ":" + key + " / -" + e.device + ":" + dev->parameters[j].key());
            }
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

bool is_state_file(const std::string &path)
{
    return ends_with_ci(path, ".ecats") || ends_with_ci(path, ".ecats.zip");
}

bool is_machine_file(const std::string &path)
{
    return ends_with_ci(path, ".cfg") || is_extension_file(path) || is_state_file(path);
}

std::string machine_file_stem(const std::string &path)
{
    if (ends_with_ci(path, ".ecats.zip")) return path.substr(0, path.size() - 10);
    if (ends_with_ci(path, ".ext.zip")) return path.substr(0, path.size() - 8);
    if (ends_with_ci(path, ".ecats")) return path.substr(0, path.size() - 6);
    if (ends_with_ci(path, ".ext") || ends_with_ci(path, ".cfg")) return path.substr(0, path.size() - 4);
    return path;
}

namespace {

//A machine file named as .cfg that is not there, but is as an .ext of the same
//name: a variant that used to be a whole .cfg and became an extension. Old
//scripts, recordings, ini settings and extensions written against the .cfg
//keep finding it
std::string cfg_or_ext(const std::string &path)
{
    if (ends_with_ci(path, ".cfg") && !dsk_tools::file_exists(path))
    {
        const std::string ext = path.substr(0, path.size() - 4) + ".ext";
        if (dsk_tools::file_exists(ext)) return ext;
    }
    return path;
}

//Where an @extends points: relative to computers/, a .cfg or a plain .ext. A
//packed extension or a state cannot be a base - neither is a file to build on
emulator::Result resolve_base(const ConfigExtension &ext, const MachinePaths &paths, std::string &base)
{
    base = cfg_or_ext(is_absolute_path(ext.extends) ? ext.extends : paths.computers_path + ext.extends);
    if (ends_with_ci(base, ".cfg") || ends_with_ci(base, ".ext")) return emulator::Result::ok();
    return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "An extension can only be based on a .cfg or an .ext file"), ext.extends);
}

std::string path_key(const std::string &path)
{
    std::string s = str_tolower(path);
    for (size_t i = 0; i < s.size(); i++) if (s[i] == '\\') s[i] = '/';
    return s;
}

//Builds what an extension stands on and applies the extension to it. A base
//that is itself an extension is built the same way first, down to the .cfg at
//the bottom, which comes back in base_cfg. seen holds the files of the chain:
//one met twice is a loop
emulator::Result load_extension_chain(const ConfigExtension &ext, const MachinePaths &paths,
                                      EmulatorConfig &config, std::string &base_cfg, bool system_only,
                                      std::vector<std::string> &seen, bool apply_self = true)
{
    std::string base;
    emulator::Result res = resolve_base(ext, paths, base);
    if (!res) return res;
    if (ends_with_ci(base, ".ext"))
    {
        const std::string key = path_key(base);
        for (size_t i = 0; i < seen.size(); i++)
            if (seen[i] == key)
                return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "The extensions are built on each other in a loop"), base);
        seen.push_back(key);
        const std::string text = dsk_tools::utf8_read_file(base);
        if (text.empty())
            return load_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), base);
        ConfigExtension parent;
        res = parent.parse(text, base);
        if (!res) return res;
        res = load_extension_chain(parent, paths, config, base_cfg, system_only, seen);
    } else {
        base_cfg = base;
        res = config.load_from_file(base, system_only);
    }
    if (!res || !apply_self) return res;
    return ext.apply(config, system_only);
}

} // namespace

emulator::Result load_extension_base(const ConfigExtension &ext, const MachinePaths &paths,
                                     EmulatorConfig &config, std::string &base_file)
{
    emulator::Result res = resolve_base(ext, paths, base_file);
    if (!res) return res;
    std::string base_cfg;
    std::vector<std::string> seen;
    return load_extension_chain(ext, paths, config, base_cfg, false, seen, false);
}

emulator::Result machine_base_file(const std::string &file, const MachinePaths &paths, std::string &base)
{
    base.clear();
    if (is_state_file(file))
    {
        //A state carries its own configuration, so it is its own base and
        //nothing else has to be fetched before it - which is what makes
        //index.html?load=<a state> work without the machine being in the build
        std::string text, archive, ext_name;
        ZipReader zip;
        emulator::Result res = read_extension_text(file, zip, archive, text, ext_name, ".ecats");
        if (!res) return res;
        MachineStateFile sf;
        res = sf.parse(text, ext_name);
        if (!res) return res;
        base = file;
        return emulator::Result::ok();
    }
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

    //The file it stands on directly, which may be an extension in turn: the
    //page fetches the bundle of that machine, and the bundle holds the rest
    return resolve_base(ext, paths, base);
}

emulator::Result load_machine_description(const std::string &asked, const MachinePaths &paths,
                                          EmulatorConfig &config, MachineSource &source,
                                          bool system_only)
{
    const std::string file = cfg_or_ext(asked);
    source = MachineSource();
    source.file = file;
    config.free_devices();

    emulator::Result res = emulator::Result::ok();

    if (is_state_file(file))
    {
        //A saved state: its own configuration and its own files, so nothing
        //outside it is read. The archive is unpacked exactly like a packed
        //extension, and the state text names its members relative to that
        source.is_state = true;
        std::string text, archive, ext_name;
        ZipReader zip;
        res = read_extension_text(file, zip, archive, text, ext_name, ".ecats");
        if (!res) return res;

        if (archive.empty())
            source.ext_path = dsk_tools::get_file_path(file);
        else if (!system_only)
        {
            res = unpack_archive(file, paths.cache_path, zip, archive, source.ext_path);
            if (!res) return res;
        }

        MachineStateFile sf;
        res = sf.parse(text, ext_name);
        if (!res) return res;

        res = config.load_from_text(sf.config_text, system_only);
        if (!res) return res;

        source.base_cfg = file;
        source.state = sf.state_text;
        source.state_version = sf.state_version;
        source.version = sf.version;
    }
    else if (!is_extension_file(file))
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
            res = unpack_archive(file, paths.cache_path, zip, archive, source.ext_path);
            if (!res) return res;
        }

        ConfigExtension ext;
        res = ext.parse(text, ext_name);
        if (!res) return res;

        //base_cfg is the .cfg at the bottom of the chain: its directory is
        //where the machine's own files are looked for
        std::vector<std::string> seen(1, path_key(file));
        res = load_extension_chain(ext, paths, config, source.base_cfg, system_only, seen);
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
            //Used now, even when the same bytes were already there
            cache_touch(path);
            p.value = path;
            p.right_extended.clear();
        }
    }
    return emulator::Result::ok();
}
