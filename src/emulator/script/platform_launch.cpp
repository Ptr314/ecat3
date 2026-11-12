// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Starting a program by the name of its platform, source

#include "emulator/script/platform_launch.h"

#include <algorithm>
#include <cstdio>

#include "dsk_tools/core.h"
// MSVC resolves the "utils.h" inside dsk_tools.h against the includer's directory,
// where it finds emulator/utils.h. Pull in the real one explicitly.
#include "libs/dsk_tools/src/utils.h"
#include "libs/lodepng/lodepng.h"
#include "libs/zip_reader.h"

#include "emulator/cache.h"
#include "emulator/config.h"
#include "emulator/config_ext.h"
#include "emulator/script/script_parser.h"
#include "emulator/utils.h"

const char * const PLATFORMS_TABLE = "platforms.cfg";

namespace {

emulator::Result launch_error(emulator::ErrorCode code, const char * message, const std::string &detail)
{
    return emulator::Result::error(code, "{Script|" + std::string(message) + "}" + (detail.empty() ? "" : " " + detail));
}

void append_utf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) out += static_cast<char>(cp);
    else {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

//The last part of a path
std::string base_name(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

//The extension of the last part of a path, with the dot, in lower case
std::string lower_ext(const std::string &path)
{
    const std::string name = base_name(path);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    return str_tolower(name.substr(dot));
}

//The last part of a path without its extension
std::string stem(const std::string &path)
{
    const std::string name = base_name(path);
    const size_t dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

//"1373-terra1" is "terra1": a catalog puts the number of the program in its
//database in front of the name of the file
std::string strip_catalog_id(const std::string &name)
{
    size_t i = 0;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') i++;
    if (i > 0 && i + 1 < name.size() && name[i] == '-') return name.substr(i + 1);
    return name;
}

std::string join(const std::vector<std::string> &items, const std::string &separator)
{
    std::string r;
    for (size_t i = 0; i < items.size(); i++) r += (i ? separator : "") + items[i];
    return r;
}

std::string hex8(uint32_t v)
{
    char buf[9];
    snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

//Takes the program out of an archive into <cache_path>launch/<archive>-<crc>/,
//under its own name. Only that one file: the rest of the archive (a readme, a
//picture) is of no use to the machine
emulator::Result unpack_program(const std::string &file, const std::string &cache_path,
                                const std::vector<std::string> &types, std::string &program)
{
    if (cache_path.empty())
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "No cache directory to unpack into"), file);

    //The archive is read whole, so its own size is bounded first. A missing
    //file has size -1, which the cast would turn into "too large"
    const long long size = dsk_tools::utf8_file_size(file);
    if (size < 0)
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "Error reading the archive"), file);
    if (static_cast<uint64_t>(size) > ARCHIVE_UNPACKED_LIMIT)
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "The archive is too large"), file);
    const std::string archive = dsk_tools::utf8_read_file(file);
    ZipReader zip;
    if (archive.empty() || !zip.open(archive))
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "Error reading the archive"),
                            file + (zip.error().empty() ? "" : ": " + zip.error()));
    if (zip.entries().size() > ARCHIVE_ENTRIES_LIMIT)
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "The archive holds too many files"), file);

    std::vector<std::string> names;
    for (size_t i = 0; i < zip.entries().size(); i++) names.push_back(zip.entries()[i].name);
    std::string problem;
    const int index = pick_archive_member(file, names, types, problem);
    if (index < 0) return emulator::Result::error(emulator::ErrorCode::FileError, problem);

    const ZipReader::Entry &entry = zip.entries()[static_cast<size_t>(index)];
    if (entry.size > ARCHIVE_UNPACKED_LIMIT)
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "The archive is too large"), file);
    std::vector<uint8_t> bytes;
    if (!zip.read(static_cast<size_t>(index), bytes))
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "Error reading the archive"),
                            file + ": " + zip.error());

    //Named after the archive and its contents, so that a changed archive never
    //finds the program of an older one
    const uint32_t crc = lodepng_crc32(reinterpret_cast<const unsigned char *>(archive.data()), archive.size());
    const std::string dir = cache_path + "launch/" + stem(file) + "-" + hex8(crc) + "/";
    cache_make_dirs(dir);
    //Used now: the cache cleanup goes by the time of this directory
    cache_touch(dir);
    program = dir + base_name(entry.name);
    if (!cache_write(program, bytes.data(), bytes.size()))
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "Error writing file"), program);
    return emulator::Result::ok();
}

void substitute(std::string &s, const std::string &name, const std::string &value)
{
    const std::string token = "${" + name + "}";
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::string::npos)
    {
        s.replace(pos, token.size(), value);
        pos += value.size();
    }
}

} // namespace

std::string platform_name_key(const std::string &name)
{
    std::string out;
    bool space = false;
    size_t i = 0;
    while (i < name.size())
    {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        uint32_t cp = c;
        size_t len = 1;
        //Two bytes cover Latin-1 and Cyrillic, which is all that has a case
        //to fold here; anything longer goes through byte by byte
        if ((c & 0xE0) == 0xC0 && i + 1 < name.size() && (static_cast<unsigned char>(name[i + 1]) & 0xC0) == 0x80)
        {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(name[i + 1]) & 0x3F);
            len = 2;
        }
        //A no-break space is a space too: it comes from text copied off a page
        if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0xA0)
        {
            space = true;
            i += len;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;

        if (len == 1)
        {
            out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            i++;
            continue;
        }
        if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;         //А-Я
        else if (cp >= 0x400 && cp <= 0x40F) cp += 0x50;    //Ѐ-Џ, Ё among them
        append_utf8(out, cp);
        i += len;
    }
    return out;
}

int pick_archive_member(const std::string &archive, const std::vector<std::string> &names,
                        const std::vector<std::string> &types, std::string &problem)
{
    problem.clear();
    const std::string want = platform_name_key(strip_catalog_id(stem(archive)));

    std::vector<size_t> found;
    for (size_t i = 0; i < names.size(); i++)
    {
        const std::string &n = names[i];
        if (n.empty() || n[n.size() - 1] == '/' || !ZipReader::is_safe_name(n)) continue;
        if (std::find(types.begin(), types.end(), lower_ext(n)) != types.end()) found.push_back(i);
    }

    for (size_t i = 0; i < found.size(); i++)
        if (platform_name_key(stem(names[found[i]])) == want) return static_cast<int>(found[i]);
    if (found.size() == 1) return static_cast<int>(found[0]);

    if (found.empty())
        problem = "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "The archive holds no file this platform runs")) + "} "
                  + base_name(archive) + " (" + join(types, ", ") + ")";
    else
    {
        std::vector<std::string> listed;
        for (size_t i = 0; i < found.size(); i++) listed.push_back(names[found[i]]);
        problem = "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "The archive holds several programs")) + "} "
                  + base_name(archive) + ": " + join(listed, ", ");
    }
    return -1;
}

emulator::Result resolve_platform(const std::string &platforms_path, const std::string &cache_path,
                                  const std::string &platform, const std::string &file,
                                  PlatformLaunch &out)
{
    out = PlatformLaunch();

    const std::string table_file = platforms_path + PLATFORMS_TABLE;
    if (!dsk_tools::file_exists(table_file))
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "The table of platforms is not found"), table_file);
    //A text without a final line break loses its last character in the
    //reader of configurations
    const std::string text = dsk_tools::utf8_read_file(table_file) + "\n";
    EmulatorConfig table;
    emulator::Result res = table.load_from_text(text);
    if (!res) return emulator::Result::error(res.code, res.message + " (" + table_file + ")");

    //By one of its names, or by the name of its block
    const std::string key = platform_name_key(platform);
    EmulatorConfigDevice * entry = nullptr;
    for (unsigned int i = 0; i < table.get_devices_count() && entry == nullptr; i++)
    {
        EmulatorConfigDevice * d = table.get_device(static_cast<int>(i));
        if (!key.empty() && platform_name_key(d->name) == key) entry = d;
        const std::vector<std::string> names = split_string(d->get_parameter("names", false).value, '|', true);
        for (size_t n = 0; n < names.size() && entry == nullptr; n++)
            if (!key.empty() && platform_name_key(names[n]) == key) entry = d;
    }
    if (entry == nullptr)
        return launch_error(emulator::ErrorCode::BadParameters, QT_TRANSLATE_NOOP("Script", "Unknown platform"), "'" + platform + "'");

    //Every other line of the block is a type of file and its script
    std::vector<std::string> types;
    std::vector<std::string> scripts;
    for (size_t i = 0; i < entry->parameters.size(); i++)
    {
        const EmulatorConfigParameter &p = entry->parameters[i];
        if (p.name == "names") continue;
        types.push_back("." + str_tolower(p.name));
        scripts.push_back(str_trim(p.value));
    }

    if (!dsk_tools::file_exists(file))
        return launch_error(emulator::ErrorCode::FileError, QT_TRANSLATE_NOOP("Script", "File is not found"), file);

    std::string program = file;
    if (lower_ext(file) == ".zip")
    {
        res = unpack_program(file, cache_path, types, program);
        if (!res) return res;
    }

    const std::vector<std::string>::const_iterator t = std::find(types.begin(), types.end(), lower_ext(program));
    if (t == types.end())
        return launch_error(emulator::ErrorCode::BadParameters, QT_TRANSLATE_NOOP("Script", "The platform does not run files of this type"),
                            platform + ": " + base_name(program) + " (" + join(types, ", ") + ")");

    std::string script = scripts[static_cast<size_t>(t - types.begin())];
    if (!is_absolute_path(script)) script = platforms_path + script;
    out.script = script;
    out.program = program;
    return emulator::Result::ok();
}

emulator::Result expand_platform_command(const ScriptCommand &c, const std::string &base_path,
                                         const std::string &platforms_path, const std::string &cache_path,
                                         std::vector<ScriptCommand> &out)
{
    out.clear();
    if (c.args.size() < 2 || c.args[0].empty() || c.args[1].empty())
        return launch_error(emulator::ErrorCode::BadParameters, QT_TRANSLATE_NOOP("Script", "PLATFORM expects a platform and a file"), "");

    const std::string platform = unescape_script_text(c.args[0]);
    std::string file = unescape_script_text(c.args[1]);
    //Next to the script that names it, as LOAD looks; else as given
    if (!is_absolute_path(file) && !base_path.empty() && dsk_tools::file_exists(base_path + file))
        file = base_path + file;

    PlatformLaunch launch;
    emulator::Result res = resolve_platform(platforms_path, cache_path, platform, file, launch);
    if (!res) return res;

    std::vector<std::string> errors;
    res = parse_script_file(launch.script, out, errors);
    if (!res) return res;
    if (!errors.empty())
        return emulator::Result::error(emulator::ErrorCode::ScriptError, launch.script + ": " + errors[0]);
    if (out.empty() || out[0].verb != SCRIPT_CMD_MACHINE || out[0].args.empty())
    {
        out.clear();
        return launch_error(emulator::ErrorCode::ScriptError,
                            QT_TRANSLATE_NOOP("Script", "A launch script must start with MACHINE"), launch.script);
    }

    //Forward slashes on every host: the path goes inside quoted arguments,
    //where a backslash may start an escape
    std::string program = launch.program;
    std::replace(program.begin(), program.end(), '\\', '/');
    for (size_t i = 0; i < out.size(); i++)
    {
        for (size_t a = 0; a < out[i].args.size(); a++) substitute(out[i].args[a], "file", program);
        substitute(out[i].params, "file", program);
    }
    return emulator::Result::ok();
}

emulator::Result expand_platform(std::vector<ScriptCommand> &commands, const std::string &base_path,
                                 const std::string &platforms_path, const std::string &cache_path)
{
    if (commands.empty() || commands[0].verb != SCRIPT_CMD_PLATFORM) return emulator::Result::ok();

    std::vector<ScriptCommand> launch;
    emulator::Result res = expand_platform_command(commands[0], base_path, platforms_path, cache_path, launch);
    if (!res) return res;
    commands.erase(commands.begin());
    commands.insert(commands.begin(), launch.begin(), launch.end());
    return emulator::Result::ok();
}

std::string platform_script_text(const std::string &platform, const std::string &file)
{
    std::string path = file;
    std::replace(path.begin(), path.end(), '\\', '/');
    return "PLATFORM \"" + escape_script_text(platform) + "\", \"" + escape_script_text(path) + "\"\n";
}

std::string platform_script_name(const std::string &cache_path)
{
    const std::string dir = cache_path + "launch/";
    cache_make_dirs(dir);
    return dir + "platform.ecat";
}
