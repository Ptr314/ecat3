// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Console entry point: the emulator without a graphical interface

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "globals.h"
#include "emulator/emulator.h"
#include "emulator/config_ext.h"
#include "emulator/cache.h"
#include "emulator/config_fields.h"
#include "emulator/thread_compat.h"
#include "emulator/utils.h"
#include "headless/renderer_null.h"
#include "libs/zip_reader.h"
#include "libs/zip_writer.h"
#include "emulator/state.h"
#include "emulator/devices/common/tape_zx.h"
#include "emulator/devices/cpu/vm1_timing.h"
#include "emulator/disasm.h"
#include "emulator/disasm_pdp11.h"
#include "emulator/disasm_buffer.h"
#include "emulator/audio/rate_match.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"

#include "emulator/script/platform_launch.h"
#include "emulator/script/script_parser.h"

#ifdef ENABLE_MCP
    #include "mcp/mcp_server.h"
    #include "mcp/mcp_session.h"
#endif

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shellapi.h>
    //Leftovers of 16-bit Windows that windows.h still defines; the selftest
    //has fields of these names
    #undef far
    #undef near
#endif

namespace fs = std::filesystem;

namespace {

//True once the emulation thread has ended on an error: nothing will move any
//more, and a script waiting for the machine would wait until the process is
//killed. Says why, once - a run that died is exactly what stderr is for
bool emulation_died(const Emulator &emulator)
{
    bool fatal = false;
    const std::string why = emulator.machine_error(&fatal);
    if (!fatal) return false;
    static bool told = false;
    if (!told) {
        told = true;
        std::cerr << "Emulation stopped: " << strip_message_context(why) << std::endl;
    }
    return true;
}

struct Options
{
    std::string config;
    std::string script;
    std::string platform;   //--platform: the program is started by a launch script
    std::string program;    //The file it runs, a positional argument
    std::string workdir;
    bool mcp = false;
    bool mcp_trace = false;
    bool no_sound = false;
    bool selftest = false;
    std::string speed;
    std::string print_config;
    bool help = false;
    bool version = false;
    bool bad = false;
    std::string bad_argument;
};

std::string lowercase(const std::string &s)
{
    std::string r = s;
    for (size_t i = 0; i < r.length(); i++)
        r[i] = static_cast<char>(tolower(static_cast<unsigned char>(r[i])));
    return r;
}

std::string extension_of(const std::string &s)
{
    const size_t dot = s.find_last_of('.');
    const size_t sep = s.find_last_of("/\\");
    if (dot == std::string::npos) return std::string();
    if (sep != std::string::npos && dot < sep) return std::string();
    return lowercase(s.substr(dot + 1));
}

//The arguments in UTF-8, which is what the core takes a file name in. On
//Windows argv is in the code page of the system, where "БК0010" of --platform
//or a program in a Cyrillic folder does not survive; the wide command line is
//converted instead
std::vector<std::string> utf8_arguments(int argc, char *argv[])
{
    std::vector<std::string> out;
#ifdef _WIN32
    int n = 0;
    LPWSTR * w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (w != nullptr)
    {
        for (int i = 0; i < n; i++)
        {
            const int size = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(size > 0 ? size - 1 : 0, '\0');
            if (size > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], size, nullptr, nullptr);
            out.push_back(s);
        }
        LocalFree(w);
        return out;
    }
#endif
    for (int i = 0; i < argc; i++) out.push_back(argv[i]);
    return out;
}

//A UTF-8 name as a path of the host, and back
fs::path u8(const std::string &s) { return fs::u8path(s); }

//An absolute name for a file given relative to the current directory: a
//script built from it starts in another one
std::string absolute_utf8(const std::string &file)
{
    if (file.empty() || is_absolute_path(file)) return file;
    std::error_code ec;
    const fs::path p = fs::absolute(u8(file), ec);
    return ec ? file : p.generic_u8string();
}

//The same options as the windowed build, parsed by hand: this frontend has no
//Qt and therefore no QCommandLineParser
Options parse_options(const std::vector<std::string> &args)
{
    Options o;
    const int argc = static_cast<int>(args.size());
    for (int i = 1; i < argc; i++)
    {
        const std::string &a = args[i];
        const bool has_next = (i + 1 < argc);

        if (a == "-h" || a == "--help")          { o.help = true; }
        else if (a == "-v" || a == "--version")  { o.version = true; }
        else if (a == "--mcp")                   { o.mcp = true; }
        else if (a == "--mcp-trace")             { o.mcp_trace = true; }
        else if (a == "--no-sound")              { o.no_sound = true; }
        else if (a == "--selftest")              { o.selftest = true; }
        else if ((a == "-c" || a == "--config")  && has_next) { o.config  = args[++i]; }
        else if ((a == "-s" || a == "--script")  && has_next) { o.script  = args[++i]; }
        else if (a == "--platform"               && has_next) { o.platform = args[++i]; }
        else if (a.compare(0, 11, "--platform=") == 0)        { o.platform = a.substr(11); }
        else if (a == "--workdir"                && has_next) { o.workdir = args[++i]; }
        else if (a == "--speed"                  && has_next) { o.speed   = args[++i]; }
        else if (a == "--print-config"           && has_next) { o.print_config = args[++i]; }
        else if (!a.empty() && a[0] == '-')      { o.bad = true; o.bad_argument = a; }
        else
        {
            //A positional argument is dispatched by its extension, so that a
            //script or a configuration can simply be dropped on the executable.
            //Anything else is a program for --platform to run
            const std::string ext = extension_of(a);
            if (ext == "ecat" && o.script.empty())     o.script = a;
            else if (is_machine_file(a) && o.config.empty()) o.config = a;
            else if (o.program.empty()) o.program = a;
            else { o.bad = true; o.bad_argument = a; }
        }
    }
    //A program without a platform has no machine to go into
    if (!o.bad && !o.program.empty() && o.platform.empty())
    {
        o.bad = true;
        o.bad_argument = o.program + " (a program is run with --platform)";
    }
    return o;
}

void print_help()
{
    std::cout
        << "eCat3 " << PROJECT_VERSION << ", a universal emulator of retro computers (console build)\n"
        << "\n"
        << "Usage: eCat3-headless [options] [file]\n"
        << "\n"
        << "  -c, --config <file.cfg>   Machine configuration to load\n"
        << "  -s, --script <file.ecat>  Script to run, see docs/SCRIPTING.md\n"
        << "      --platform <name> <file>  Run a program or a disk (.bin, .bkd, .img, a\n"
        << "                            .zip...) on the machine of a platform (bk10,\n"
        << "                            bk11m...), see launch/platforms.cfg\n"
        << "      --workdir <dir>       Directory to work in, the one holding computers/\n"
        << "      --no-sound            Do not open an audio device at all\n"
        << "      --speed <factor>      Speed of emulation: 0.25 to 1000, or max (as fast as\n"
        << "                            the host can); a script runs the same at any speed\n"
        << "      --selftest            Check the internal invariants and exit\n"
        << "      --print-config <file>     Print the configuration a machine file resolves to\n"
        << "                            (an extension applied to its base) and exit\n"
#ifdef ENABLE_MCP
        << "      --mcp                 Act as an MCP server on stdin/stdout, see docs/MCP.md\n"
        << "      --mcp-trace           Print the MCP conversation to stderr\n"
#endif
        << "  -h, --help                Show this help\n"
        << "  -v, --version             Show the version\n"
        << "\n"
        << "A positional argument is taken as a script or a configuration by its extension,\n"
        << "anything else as the program of --platform.\n";
}

//--------------------------------- Selftest --------------------------------//
//A saved state (.ecats) carries the configuration of its machine inside
//itself, written by serialize_config(). A value that does not survive the
//round trip through the parser is a state that loads as a different machine -
//and the ways that can happen are all silent: a lost duplicate mapper line, a
//type written where the system section must have none, a value the tokenizer
//breaks apart. So every configuration of the installation is checked

std::string parameter_difference(const EmulatorConfigDevice * a, const EmulatorConfigDevice * b)
{
    if (a->name != b->name) return "device name '" + a->name + "' became '" + b->name + "'";
    if (a->type != b->type) return "type of '" + a->name + "' became '" + b->type + "'";
    if (a->parameters.size() != b->parameters.size())
        return "'" + a->name + "' had " + std::to_string(a->parameters.size())
             + " parameters, now " + std::to_string(b->parameters.size());
    for (size_t i = 0; i < a->parameters.size(); i++)
    {
        const EmulatorConfigParameter &p = a->parameters[i];
        const EmulatorConfigParameter &q = b->parameters[i];
        if (p.name == q.name && p.left_range == q.left_range && p.value == q.value
            && p.right_range == q.right_range && p.right_extended == q.right_extended) continue;
        return "'" + a->name + "' parameter " + std::to_string(i) + ": '"
             + config_parameter_text(p) + "' became '" + config_parameter_text(q) + "'";
    }
    return std::string();
}

bool check_config_round_trip(const std::string &file, const std::string &computers_path, std::string &message)
{
    //An extension of the installation is checked as the machine it builds:
    //its base with its edits applied, the chain included
    EmulatorConfig original;
    emulator::Result res = emulator::Result::ok();
    if (is_extension_file(file))
    {
        MachinePaths mp;
        mp.computers_path = computers_path;
        MachineSource source;
        res = load_machine_description(file, mp, original, source);
    }
    else
        res = original.load_from_file(file);
    if (!res) { message = strip_message_context(res.message); return false; }

    EmulatorConfig again;
    res = again.load_from_text(serialize_config(original));
    if (!res) { message = "re-reading what was written: " + strip_message_context(res.message); return false; }

    if (original.get_devices_count() != again.get_devices_count())
    {
        message = "had " + std::to_string(original.get_devices_count()) + " devices, now "
                + std::to_string(again.get_devices_count());
        return false;
    }
    for (unsigned int i = 0; i < original.get_devices_count(); i++)
    {
        message = parameter_difference(original.get_device(static_cast<int>(i)),
                                       again.get_device(static_cast<int>(i)));
        if (!message.empty()) return false;
    }
    return true;
}

//Имена клавиш машины: в таблице они стоят в ячейках матрицы и в заголовке
//("hold: key_shift_1 once"), а на рисунке - в идентификаторах элементов. Все
//они начинаются с "key_", чтобы не спутаться с именами клавиш хоста
std::set<std::string> key_ids_of_table(const std::string &text)
{
    std::set<std::string> out;
    std::vector<std::string> lines = split_string(text, '\n', true);
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string line = lines[i];
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = line.substr(0, comment);
        for (size_t c = 0; c < line.size(); c++)
            if (line[c] == '\t' || line[c] == '|' || line[c] == ':' || line[c] == '\r') line[c] = ' ';
        std::vector<std::string> words = split_string(line, ' ', true);
        for (size_t w = 0; w < words.size(); w++)
        {
            std::string id = str_trim(words[w]);
            //"key_0/S" - это та же клавиша key_0, взятая с модификатором:
            //на рисунке она одна, а строк в таблице у нее несколько
            const size_t slash = id.find('/');
            if (slash != std::string::npos) id = id.substr(0, slash);
            if (id.compare(0, 4, "key_") == 0) out.insert(id);
        }
    }
    return out;
}

std::set<std::string> key_ids_of_picture(const std::string &text)
{
    std::set<std::string> out;
    const std::string tag = "id=\"key_";
    size_t pos = 0;
    while ((pos = text.find(tag, pos)) != std::string::npos)
    {
        const size_t start = pos + 4;                   // за id="
        const size_t end = text.find('"', start);
        if (end == std::string::npos) break;
        out.insert(text.substr(start, end - start));
        pos = end;
    }
    return out;
}

std::string id_list(const std::set<std::string> &ids, size_t limit = 5)
{
    std::string out;
    size_t n = 0;
    for (std::set<std::string>::const_iterator it = ids.begin(); it != ids.end() && n < limit; ++it, ++n)
        out += (out.empty() ? "" : " ") + *it;
    if (ids.size() > limit) out += " ... (" + std::to_string(ids.size()) + ")";
    return out;
}

//Клавиша рисунка доходит до матрицы по имени: идентификатор элемента в SVG
//должен совпасть с именем в таблице клавиш машины. Разъедутся - клавиша молча
//перестанет нажиматься, и этого не видит ни сборка, ни набор тестов. Так на
//УК-НЦ осталась мертвой ИСП: в рисунке у нее был идентификатор копии Inkscape
bool check_keyboard_picture(const std::string &file, const std::string &data_path,
                            const std::string &software_path, std::string &message)
{
    EmulatorConfig config;
    emulator::Result res = config.load_from_file(file);
    if (!res) { message = strip_message_context(res.message); return false; }

    //Так же, как их выставляет Emulator::load_config(): пути с разделителем
    //на конце, иначе find_file_location() склеит их с именем файла впритык
    SystemData sd;
    sd.system_file = file;
    sd.system_path = dsk_tools::get_file_path(file);
    sd.data_path = data_path;
    sd.software_path = software_path;

    for (unsigned int i = 0; i < config.get_devices_count(); i++)
    {
        EmulatorConfigDevice * d = config.get_device(static_cast<int>(i));
        const std::string keys = d->get_parameter("keys", false).value;
        const std::string picture = d->get_parameter("picture", false).value;
        if (keys.empty() || picture.empty()) continue;

        const std::string keys_file = find_file_location(&sd, keys);
        const std::string picture_file = find_file_location(&sd, picture);
        if (keys_file.empty())    { message = "key table not found: " + keys; return false; }
        if (picture_file.empty()) { message = "picture not found: " + picture; return false; }

        const std::set<std::string> table = key_ids_of_table(dsk_tools::utf8_read_file(keys_file));
        const std::set<std::string> drawn = key_ids_of_picture(dsk_tools::utf8_read_file(picture_file));
        if (table.empty()) { message = "no key names in " + keys; return false; }
        if (drawn.empty()) { message = "no key elements in " + picture; return false; }

        //Проверяется одна сторона: имя из таблицы обязано быть на рисунке,
        //иначе до этой клавиши не добраться мышью. Обратное - законно: у Ириши
        //на панели нарисованы переключатели терминала 15ИЭ, которых машина не
        //читает, и в таблице их нет намеренно
        std::set<std::string> missing;
        for (std::set<std::string>::const_iterator it = table.begin(); it != table.end(); ++it)
            if (drawn.find(*it) == drawn.end()) missing.insert(*it);

        if (!missing.empty())
        {
            message = "in " + keys + ", not drawn in " + picture + ": " + id_list(missing);
            return false;
        }
    }
    return true;
}

//The archive a saved state is packed into is written by ZipWriter and read
//back by ZipReader, which verifies both the size and the CRC of every entry -
//so reading our own archive is a complete check of the headers
bool check_zip_round_trip(std::string &message)
{
    struct Sample { const char * name; std::string data; };
    std::vector<Sample> samples;
    samples.push_back({"state.ecats", std::string()});                      //Empty
    samples.push_back({"a.txt", "x"});                                      //One byte
    samples.push_back({"rom/monitor.rom", std::string(4096, '\0')});        //Compresses away
    samples.push_back({"media/disk.raw", std::string()});
    //Incompressible: deflate comes out larger and the entry has to be stored
    std::string noise;
    for (unsigned i = 0; i < 8192; i++)
        noise += static_cast<char>((i * 1103515245u + 12345u) >> 16);
    samples[3].data = noise;

    ZipWriter w;
    for (size_t i = 0; i < samples.size(); i++) w.add(samples[i].name, samples[i].data);
    std::string archive;
    if (!w.build(archive)) { message = w.error(); return false; }


    ZipReader r;
    if (!r.open(archive)) { message = r.error(); return false; }
    if (r.entries().size() != samples.size())
    {
        message = "wrote " + std::to_string(samples.size()) + " entries, read back "
                + std::to_string(r.entries().size());
        return false;
    }
    for (size_t i = 0; i < samples.size(); i++)
    {
        std::vector<uint8_t> back;
        if (!r.read(samples[i].name, back)) { message = r.error(); return false; }
        const std::string &want = samples[i].data;
        if (back.size() != want.size()
            || (!want.empty() && memcmp(back.data(), want.data(), want.size()) != 0))
        {
            message = std::string("entry '") + samples[i].name + "' came back different";
            return false;
        }
    }

    //An archive is a test reference only if building it twice gives the same
    //bytes, so the fixed time stamp and the entry order are part of the deal
    ZipWriter w2;
    for (size_t i = 0; i < samples.size(); i++) w2.add(samples[i].name, samples[i].data);
    std::string archive2;
    if (!w2.build(archive2)) { message = w2.error(); return false; }
    if (archive2 != archive) { message = "two identical archives came out different"; return false; }

    //And it must refuse what the reader would reject
    ZipWriter bad;
    bad.add("../escape.txt", std::string("x"));
    std::string ignored;
    if (bad.build(ignored)) { message = "an unsafe entry name was accepted"; return false; }

    return true;
}

//SAVE and LOAD of a Spectrum tape are one waveform read in both directions:
//Pulser hands out the half periods, Decoder puts the block back together. A
//round trip here catches a misplaced boundary between two lengths, which on a
//machine would only show as a checksum error deep inside a game
bool check_zx_tape_round_trip(std::string &message)
{
    struct Case { const char * what; std::vector<uint8_t> data; };
    std::vector<Case> cases;

    //A header: the first byte below $80 is what gives it the long pilot tone
    std::vector<uint8_t> header;
    header.push_back(0x00);
    for (unsigned i = 0; i < 17; i++) header.push_back(static_cast<uint8_t>(0x20 + i));
    cases.push_back({"header", header});

    //Every byte value, so that no bit pattern is misread
    std::vector<uint8_t> all;
    all.push_back(0xFF);
    for (unsigned i = 0; i < 256; i++) all.push_back(static_cast<uint8_t>(i));
    cases.push_back({"every byte value", all});

    //All zeroes and all ones: the two lengths, one after the other with no
    //change to resynchronise on
    std::vector<uint8_t> flat(64, 0x00);
    flat[0] = 0xFF;
    for (size_t i = 32; i < flat.size(); i++) flat[i] = 0xFF;
    cases.push_back({"one length at a time", flat});

    //The second pass stretches every third half period by a third, the way a
    //DMA burst does on the Арго: the decoder is supposed to survive that, and
    //that is the whole reason it measures the pilot tone and sums the halves
    for (size_t c = 0; c < cases.size() * 2; c++)
    {
        const Case &t = cases[c % cases.size()];
        const bool bursts = c >= cases.size();
        zx_tape::Pulser  pulser;
        zx_tape::Decoder decoder;
        pulser.start(t.data.data(), t.data.size());

        std::vector<uint8_t> back;
        unsigned int guard = 0;
        for (unsigned int v = pulser.next(); v != 0; v = pulser.next())
        {
            if (++guard > 2000000) { message = std::string(t.what) + ": the waveform does not end"; return false; }
            if (bursts && (guard % 3) == 0) v += v / 3;
            if (decoder.add(v)) { back = decoder.bytes; decoder.clear(); }
        }
        //The last bit of a block is followed by nothing: on a tape that is the
        //silence after it, and here it is the end of the waveform
        if (back.empty() && decoder.close()) back = decoder.bytes;

        if (back != t.data)
        {
            message = std::string(t.what) + (bursts?" with DMA bursts":"") + ": got " + std::to_string(back.size())
                    + " bytes of " + std::to_string(t.data.size());
            for (size_t i = 0; i < back.size() && i < t.data.size(); i++)
                if (back[i] != t.data[i])
                {
                    message += ", first difference at " + std::to_string(i);
                    break;
                }
            return false;
        }
    }
    return true;
}

//The hex dump is what a saved state writes RAM as, and the '*' folding is the
//part of it that can silently put bytes at the wrong address
bool check_hex_round_trip(std::string &message)
{
    struct Case { const char * what; std::vector<uint8_t> data; unsigned int bits; };
    std::vector<Case> cases;

    cases.push_back({"empty RAM", std::vector<uint8_t>(4096, 0), 8});
    cases.push_back({"filled RAM", std::vector<uint8_t>(4096, 0xFF), 8});

    //Data, a long identical stretch, data again: the folding has to resume at
    //the right address on the far side of the '*'
    std::vector<uint8_t> mixed(4096, 0);
    for (unsigned i = 0; i < 64; i++) mixed[i] = static_cast<uint8_t>(i);
    for (unsigned i = 3000; i < 3100; i++) mixed[i] = static_cast<uint8_t>(i & 0xFF);
    cases.push_back({"data around a gap", mixed, 8});

    //Not a multiple of the line, so the last line is short and must not
    //become a folding pattern
    std::vector<uint8_t> odd(1000, 0);
    for (size_t i = 0; i < odd.size(); i++) odd[i] = static_cast<uint8_t>((i * 7) & 0xFF);
    cases.push_back({"a size that is not a whole line", odd, 8});

    std::vector<uint8_t> words(512, 0);
    for (size_t i = 0; i < words.size(); i++) words[i] = static_cast<uint8_t>(i & 0xFF);
    cases.push_back({"16 bit values", words, 16});

    //Every byte value, so that no digit pair is misread
    std::vector<uint8_t> all(256, 0);
    for (unsigned i = 0; i < 256; i++) all[i] = static_cast<uint8_t>(i);
    cases.push_back({"every byte value", all, 8});

    for (size_t c = 0; c < cases.size(); c++)
    {
        const Case &t = cases[c];
        const std::string dump = encode_hex_dump(t.data.data(), t.data.size(), t.bits, "  ");
        //A restore starts from a device that has just been reset, so the
        //buffer it decodes into is not the one it was encoded from
        std::vector<uint8_t> back(t.data.size(), 0x5A);
        std::string error;
        if (!decode_hex_dump(dump, back.data(), back.size(), t.bits, error))
        {
            message = std::string(t.what) + ": " + error;
            return false;
        }
        for (size_t i = 0; i < t.data.size(); i++)
            if (back[i] != t.data[i])
            {
                message = std::string(t.what) + ": byte " + std::to_string(i) + " came back as "
                        + std::to_string(back[i]) + " instead of " + std::to_string(t.data[i]);
                return false;
            }
    }

    //An untouched 64K of RAM has to cost a handful of lines, not four thousand
    const std::vector<uint8_t> empty(65536, 0);
    const std::string dump = encode_hex_dump(empty.data(), empty.size(), 8, "");
    if (std::count(dump.begin(), dump.end(), '\n') > 4)
    {
        message = "folding did not work: 64K of zeroes took "
                + std::to_string(std::count(dump.begin(), dump.end(), '\n')) + " lines";
        return false;
    }
    return true;
}

// A device block and @before in an extension: where things land is the whole
// point, and nothing a script can read shows the order of the devices. Then
// the extension is written back and read again, which must change nothing
bool check_ext_devices(std::string &message)
{
    const std::string base_text =
        "system {\n\tversion = base\n}\n"
        "cpu : i8080 {\n\tclock = 1000000\n}\n"
        "ram : ram {\n\tsize = 64k\n}\n"
        "mapper : memory-mapper {\n\t@memory[$0000-$7FFF] = ram\n\t@memory[$8000-$FFFF] = ram\n}\n";
    const std::string ext_text =
        "@extends base.cfg\n@version variant\n"
        "// first a block at the end, then one placed\n"
        "rom : rom {\n\tdata = {$C3, $00, $00}\n\tsize = 3 // a comment\n}\n"
        "// one @before for a run of blocks, which keeps its order\n"
        "@before ram\n"
        "port : port { default = $12 }\n"
        "\n"
        "port2 : port { default = 1 }\n"
        "@before mapper:@memory[$8000-$FFFF] = ram\n"
        "mapper:@memory[$8000-$8002] = rom {mode = r}\n"
        "mapper:@memory[$8003-$8003] = port2 {mode = r}\n"
        "// the same range to the same device again, for writing: not a line of the base\n"
        "mapper:@memory[$8003-$8003] = port2 {mode = w}\n"
        "// an edit of another kind ends the run: the next range goes to the end\n"
        "rom:size = 4\n"
        "mapper:@memory[$F000-$F000] = port\n";

    EmulatorConfig config;
    emulator::Result res = config.load_from_text(base_text);
    if (!res) { message = "base: " + res.message; return false; }
    ConfigExtension ext;
    res = ext.parse(ext_text, "selftest.ext");
    if (!res) { message = "parse: " + res.message; return false; }
    res = ext.apply(config);
    if (!res) { message = "apply: " + res.message; return false; }

    std::string order;
    for (unsigned int i = 0; i < config.get_devices_count(); i++)
        order += (i ? "," : "") + config.get_device(static_cast<int>(i))->name;
    if (order != "system,cpu,port,port2,ram,mapper,rom") { message = "device order " + order; return false; }

    EmulatorConfigDevice * mapper = config.get_device("mapper");
    std::string lines;
    for (size_t i = 0; i < mapper->parameters.size(); i++)
        lines += (i ? "," : "") + mapper->parameters[i].left_range + "=" + mapper->parameters[i].value;
    if (lines != "[$0000-$7FFF]=ram,[$8000-$8002]=rom,[$8003-$8003]=port2,[$8003-$8003]=port2,[$8000-$FFFF]=ram,[$F000-$F000]=port")
    { message = "mapper order " + lines; return false; }

    EmulatorConfigDevice * rom = config.get_device("rom");
    if (rom->get_parameter("size", false).value != "4" || rom->get_parameter("data", false).right_extended.empty())
    { message = "the block of rom or the edit after it is lost"; return false; }

    //Written back and read again: the same machine
    const std::string first = serialize_config(config);
    ConfigExtension again;
    res = again.parse(ext.serialize(), "selftest-again.ext");
    if (!res) { message = "parse of the written extension: " + res.message + "\n" + ext.serialize(); return false; }
    EmulatorConfig config2;
    config2.load_from_text(base_text);
    res = again.apply(config2);
    if (!res) { message = "apply of the written extension: " + res.message; return false; }
    if (serialize_config(config2) != first) { message = "the written extension builds another machine"; return false; }

    //What must be refused
    struct Bad { const char * what; const char * text; };
    static const Bad bad[] = {
        { "a device the base has",      "ram : ram { size = 1k }\n" },
        { "@before a missing device",   "@before nothing\nx : port { default = 0 }\n" },
        { "@before a missing line",     "@before mapper:@memory[$1000-$1FFF]\nmapper:@memory[$1000-$1000] = ram\n" },
        { "@before a plain property",   "@before ram:size\nram:size = 1k\n" },
        { "@before of another device",  "@before cpu:clock\nmapper:@memory[$1000-$1000] = ram\n" },
        { "@before a removal",          "@before ram\n-cpu:clock\n" },
        { "@before with nothing after", "@before ram\n" },
        { "@before that placed nothing","@before ram\n@before cpu\nx : port { default = 0 }\n" },
        { "a placed line the base has", "@before mapper:@memory[$0000-$7FFF]\nmapper:@memory[$8000-$FFFF] = ram\n" },
    };
    for (const Bad &b : bad)
    {
        EmulatorConfig c;
        c.load_from_text(base_text);
        ConfigExtension e;
        res = e.parse(std::string("@extends base.cfg\n@version bad\n") + b.text, "bad.ext");
        if (res) res = e.apply(c);
        if (res) { message = std::string("accepted: ") + b.what; return false; }
    }
    return true;
}

// The editor of the machine chooser rewrites an extension from its model: the
// device blocks and the @before lines must survive a save with no change
bool check_ext_editor_keeps_blocks(const std::string &computers_path, std::string &message)
{
    const std::string base = "uknc/reset-test.cfg";
    if (!dsk_tools::file_exists(computers_path + base)) return true;
    const std::string text =
        "@extends " + base + "\n@version editor\n"
        "counter2 : rom {\n\tdata = {$9F,$0A,$42,$00,$FD,$01}\n}\n"
        "@before mapper:@memory[100000-100005]\n"
        "mapper:@memory[100000-100005] = counter2 {mode = r}\n"
        "@before ram\nspare : port {\n\tsize = _16\n}\n"
        "display:mode = mono\n";
    const std::string path = (fs::temp_directory_path() / "ecat3-selftest-editor.ext").generic_string();
    {
        dsk_tools::UTF8_ofstream f(path, std::ios::binary);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    MachinePaths paths;
    paths.computers_path = computers_path;
    ExtEditModel model;
    emulator::Result res = model.open(path, paths);
    std::error_code ec;
    fs::remove(path, ec);
    if (!res) { message = "open: " + res.message; return false; }
    const std::string saved = model.build();

    std::string machine[2];
    const std::string texts[2] = { text, saved };
    for (int k = 0; k < 2; k++)
    {
        EmulatorConfig c;
        res = c.load_from_file(computers_path + base);
        ConfigExtension e;
        if (res) res = e.parse(texts[k], "editor.ext");
        if (res) res = e.apply(c);
        if (!res) { message = (k ? "the saved text: " : "the original: ") + res.message + (k ? "\n" + saved : ""); return false; }
        machine[k] = serialize_config(c);
    }
    if (machine[0] != machine[1]) { message = "a save without changes builds another machine:\n" + saved; return false; }
    return true;
}

// The power-on phase of the ВП1-037 is a field of the editor: the variants of
// phases, the one the config sets when it is not among them, and random; the
// choice is written as a vp037:phase line the machine loads
bool check_ext_editor_phase(const std::string &computers_path, std::string &message)
{
    struct Case { const char * cfg; const char * base; const char * values; };
    const Case cases[] = {
        { "bk/BK-0011M.cfg",   "0", "0,1,2,random" },
        { "bk/BK-0010-01.cfg", "0", "0,2,3,random" },
    };
    for (const Case &c : cases)
    {
        if (!dsk_tools::file_exists(computers_path + c.cfg)) continue;
        MachinePaths paths;
        paths.computers_path = computers_path;
        ExtEditModel model;
        emulator::Result res = model.open(computers_path + c.cfg, paths);
        if (!res) { message = std::string(c.cfg) + ": " + res.message; return false; }
        DeviceConfigField * f = nullptr;
        for (DeviceConfigField &d : model.fields)
            if (d.device == "vp037" && d.field.name == "phase") f = &d;
        if (f == nullptr) { message = std::string(c.cfg) + ": no phase field"; return false; }
        std::string values;
        for (const ConfigFieldValue &v : f->field.values) values += (values.empty() ? "" : ",") + v.value;
        if (values != c.values || f->base_value != c.base) {
            message = std::string(c.cfg) + ": phase " + f->base_value + " of " + values;
            return false;
        }
        f->present = true;
        f->value = "random";
        const std::string text = model.build();
        EmulatorConfig config;
        res = config.load_from_file(computers_path + c.cfg);
        ConfigExtension e;
        if (res) res = e.parse(text, "phase.ext");
        if (res) res = e.apply(config);
        if (!res) { message = std::string(c.cfg) + ": " + res.message + "\n" + text; return false; }
        const std::string written = config.get_device("vp037")->get_parameter("phase", false).value;
        if (written != "random") { message = std::string(c.cfg) + ": phase = " + written + "\n" + text; return false; }
    }
    return true;
}

// The editor saves a new extension as an .ext.zip and edits one in place: the
// text goes back under its own name and the files beside it stay. A copy of a
// user's archive that only sets fields takes its base; one that carries files
// cannot be stood on (@extends takes no archive) and is refused
bool check_ext_editor_zip(const std::string &computers_path, std::string &message)
{
    const std::string base = "agat/Agat-9-yazs.ext";
    if (!dsk_tools::file_exists(computers_path + base)) return true;
    MachinePaths paths;
    paths.computers_path = computers_path;

    const std::string dir = fs::temp_directory_path().generic_string() + "/";
    const std::string path = unique_ext_file(dir, "ecat3-selftest-zip");
    if (path.size() < 8 || path.compare(path.size() - 8, 8, ".ext.zip") != 0) { message = "a new file is named " + path; return false; }
    const std::string script = "WAIT _100\nEXIT\n";
    std::error_code ec;
    auto fail = [&](const std::string &m) { fs::remove(path, ec); message = m; return false; };

    ExtEditModel model;
    emulator::Result res = model.open(computers_path + base, paths, true);
    if (!res) return fail("open: " + res.message);
    model.version = "Zip";
    model.ext.script = script;
    res = write_extension_file(path, model.build());
    if (!res) return fail("write: " + res.message);

    // A file beside the text, as a packed extension may carry
    {
        ZipReader r;
        if (!r.open(dsk_tools::utf8_read_file(path)) || r.entries().size() != 1) return fail("a new archive holds more than its text");
        std::vector<uint8_t> text;
        r.read(0, text);
        ZipWriter w;
        w.add(r.entries()[0].name, std::string(text.begin(), text.end()));
        w.add("files/data.bin", std::string("\x01\x02\x03", 3));
        std::string out;
        w.build(out);
        dsk_tools::UTF8_ofstream f(path, std::ios::binary);
        f.write(out.data(), static_cast<std::streamsize>(out.size()));
    }

    ExtEditModel edit;
    res = edit.open(path, paths);
    if (!res) return fail("open of the archive: " + res.message);
    if (edit.file != path || edit.extends != base || edit.version != "Zip" || edit.ext.script != script || edit.other_files != 1)
        return fail("the archive came back as another extension");
    edit.version = "Zip 2";
    res = write_extension_file(path, edit.build());
    if (!res) return fail("rewrite: " + res.message);
    {
        ZipReader r;
        std::vector<uint8_t> data;
        if (!r.open(dsk_tools::utf8_read_file(path)) || r.entries().size() != 2 || !r.read("files/data.bin", data) || data.size() != 3)
            return fail("a rewrite lost the files beside the text");
    }
    EmulatorConfig c;
    MachineSource source;
    res = load_machine_description(path, paths, c, source, true);
    if (!res || source.script != script) return fail("the rewritten archive does not load: " + res.message);

    ExtEditModel copy;
    if (copy.open(path, paths, true)) return fail("a copy of an archive carrying files was not refused");

    // Without the file it is a user's copy: the next one stands on its base
    res = write_extension_file(path + ".plain.ext.zip", edit.build());
    if (res) res = copy.open(path + ".plain.ext.zip", paths, true);
    fs::remove(path + ".plain.ext.zip", ec);
    if (!res) return fail("copy of an archive: " + res.message);
    if (copy.extends != base || copy.ext.script != script || !copy.file.empty())
        return fail("a copy of an archive stands on " + copy.extends);

    fs::remove(path, ec);
    return true;
}

// A file embedded into an archive is a file of the archive, and the text names
// it: the chooser reads a short text. The editor sees it inline, as in a plain
// .ext; a rewrite keeps it, a file no longer embedded leaves the archive, and
// the machine finds it where the archive is unpacked
bool check_ext_editor_inline_zip(const std::string &computers_path, std::string &message)
{
    const std::string base = "agat/Agat-9-yazs.ext";
    if (!dsk_tools::file_exists(computers_path + base)) return true;
    const std::string dir = fs::temp_directory_path().generic_string() + "/";
    MachinePaths paths;
    paths.computers_path = computers_path;
    paths.cache_path = dir + "ecat3-selftest-cache/";

    const std::string path = unique_ext_file(dir, "ecat3-selftest-inline");
    const std::string image = dir + "ecat3-selftest-image.dsk";
    const std::string bytes("\x00\x01\x02\xff selftest", 13);
    {
        dsk_tools::UTF8_ofstream f(image, std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    std::error_code ec;
    auto fail = [&](const std::string &m) {
        fs::remove(path, ec); fs::remove(image, ec); fs::remove_all(paths.cache_path, ec);
        message = m; return false;
    };
    auto file_field = [](ExtEditModel &m) -> DeviceConfigField * {
        for (size_t i = 0; i < m.fields.size(); i++)
            if (m.fields[i].field.type == CONFIG_FIELD_FILE && m.fields[i].field.embeddable) return &m.fields[i];
        return nullptr;
    };
    auto members = [&](std::vector<std::string> &names, std::string &text) {
        names.clear();
        ZipReader r;
        if (!r.open(dsk_tools::utf8_read_file(path))) return false;
        for (size_t i = 0; i < r.entries().size(); i++) {
            names.push_back(r.entries()[i].name);
            if (r.entries()[i].name.size() > 4 && r.entries()[i].name.compare(r.entries()[i].name.size() - 4, 4, ".ext") == 0) {
                std::vector<uint8_t> t;
                if (!r.read(i, t)) return false;
                text.assign(t.begin(), t.end());
            }
        }
        return true;
    };

    ExtEditModel model;
    emulator::Result res = model.open(computers_path + base, paths, true);
    if (!res) return fail("open: " + res.message);
    DeviceConfigField * f = file_field(model);
    if (f == nullptr) return fail(base + " has no file field to embed into");
    const std::string field = f->device + ":" + f->field.name;
    res = ExtEditModel::embed_file(*f, image);
    if (!res) return fail("embed: " + res.message);
    model.version = "Inline";
    res = write_extension_file(path, model.build());
    if (!res) return fail("write: " + res.message);

    std::vector<std::string> names;
    std::string text;
    if (!members(names, text) || names.size() != 2) return fail("the embedded file is not a file of the archive");
    if (text.find("base64:") != std::string::npos || text.find("ecat3-selftest-image.dsk") == std::string::npos)
        return fail("the text still holds the data, or does not name the file:\n" + text);

    // Inline again to the editor, and not a file it would refuse to copy
    ExtEditModel edit;
    res = edit.open(path, paths);
    if (!res) return fail("open of the archive: " + res.message);
    f = file_field(edit);
    if (f == nullptr || f->device + ":" + f->field.name != field || !f->is_inline() || edit.other_files != 0)
        return fail("the editor does not see " + field + " embedded");
    if (f->extended != inline_block(std::vector<uint8_t>(bytes.begin(), bytes.end())))
        return fail("the embedded data came back changed");
    edit.version = "Inline 2";
    res = write_extension_file(path, edit.build());
    if (!res) return fail("rewrite: " + res.message);
    if (!members(names, text) || names.size() != 2) return fail("a rewrite changed the files of the archive");

    // The machine finds the file
    {
        EmulatorConfig c;
        MachineSource source;
        res = load_machine_description(path, paths, c, source, false);
        if (!res) return fail("the archive does not load: " + res.message);
        if (dsk_tools::utf8_read_file(source.ext_path + "ecat3-selftest-image.dsk") != bytes)
            return fail("the unpacked archive has no " + source.ext_path + "ecat3-selftest-image.dsk");
    }

    // Embedded no more: the file leaves the archive
    ExtEditModel plain;
    res = plain.open(path, paths);
    if (!res) return fail("open of the rewritten archive: " + res.message);
    f = file_field(plain);
    f->value = "other.dsk";
    f->extended.clear();
    res = write_extension_file(path, plain.build());
    if (!res) return fail("write without the file: " + res.message);
    if (!members(names, text) || names.size() != 1) return fail("a file no longer embedded stayed in the archive");

    fs::remove(path, ec); fs::remove(image, ec); fs::remove_all(paths.cache_path, ec);
    return true;
}

// The chooser's Copy of an extension stands on it: the editor writes an @extends
// of the copied file and none of its edits, and saving that unchanged builds
// the very machine copied. A copy repeating the edits on the bottom .cfg would
// load on the desktop, but the web page fetches the bundle of the machine an
// extension names, and that of the plain base lacks the variant's files
bool check_ext_editor_copy(const std::string &computers_path, std::string &message)
{
    const std::string original = "agat/Agat-9-yazs.ext";
    if (!dsk_tools::file_exists(computers_path + original)) return true;
    MachinePaths paths;
    paths.computers_path = computers_path;
    ExtEditModel model;
    emulator::Result res = model.open(computers_path + original, paths, true);
    if (!res) { message = "open: " + res.message; return false; }
    if (model.extends != original) { message = "the copy stands on " + model.extends; return false; }
    for (size_t i = 0; i < model.fields.size(); i++)
        if (model.fields[i].changed()) { message = "the copy differs in " + model.fields[i].device + ":" + model.fields[i].field.name; return false; }
    const std::string saved = model.build();
    if (saved.find("@extends " + original) == std::string::npos) { message = "saved without its base:\n" + saved; return false; }

    const std::string path = (fs::temp_directory_path() / "ecat3-selftest-copy.ext").generic_string();
    {
        dsk_tools::UTF8_ofstream f(path, std::ios::binary);
        f.write(saved.data(), static_cast<std::streamsize>(saved.size()));
    }
    std::string machine[2];
    const std::string files[2] = { computers_path + original, path };
    for (int k = 0; k < 2; k++)
    {
        EmulatorConfig c;
        MachineSource source;
        res = load_machine_description(files[k], paths, c, source);
        if (!res) break;
        machine[k] = serialize_config(c);
    }
    std::error_code ec;
    fs::remove(path, ec);
    if (!res) { message = "load: " + res.message + "\n" + saved; return false; }
    // The version is the copy's own; everything else must be the same
    for (int k = 0; k < 2; k++) {
        const size_t v = machine[k].find("version");
        if (v != std::string::npos) machine[k].erase(v, machine[k].find('\n', v) - v);
    }
    if (machine[0] != machine[1]) { message = "the copy builds another machine:\n" + saved; return false; }

    // Only the script of the file opened runs, so the copy carries it along,
    // whether it stands on the file copied or takes that file's base. A user's
    // copy that only sets a field is not stood on; a protected one is
    const DeviceConfigField *field = nullptr;
    for (size_t i = 0; i < model.fields.size() && field == nullptr; i++)
        if (model.fields[i].present && model.fields[i].extended.empty()) field = &model.fields[i];
    if (field == nullptr) { message = "no field to edit in " + original; return false; }
    const std::string script = "WAIT _100\nEXIT\n";
    const std::string scripted = (fs::temp_directory_path() / "ecat3-selftest-scripted.ext").generic_string();
    for (int is_protected = 0; is_protected < 2; is_protected++)
    {
        {
            const std::string text = "@extends " + original + "\n@version Scripted\n"
                                   + (is_protected ? "@protected\n" : "")
                                   + field->device + ":" + field->field.name + " = " + field->value + "\n"
                                   + "@script\n" + script;
            dsk_tools::UTF8_ofstream f(scripted, std::ios::binary);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        ExtEditModel copy;
        res = copy.open(scripted, paths, true);
        // Before build(), which drops an edit giving the base's own value
        const bool edit_kept = res && copy.ext.edits.size() == 1;
        const std::string copied = res ? copy.build() : std::string();
        fs::remove(scripted, ec);
        if (!res) { message = "open of a scripted extension: " + res.message; return false; }
        if (copy.ext.script != script || copied.find("@script\n" + script) == std::string::npos) {
            message = "the copy lost the script:\n" + copied; return false;
        }
        const bool on_base = copy.extends == original;
        if (on_base == (is_protected != 0) || edit_kept == (is_protected != 0)) {
            message = std::string(is_protected ? "a copy of a protected extension" : "a copy of a copy that sets a field")
                    + " stands on " + copy.extends + ":\n" + copied;
            return false;
        }
        if (!copy.file.empty()) { message = "the copy has a file name: " + copy.file; return false; }
    }
    return true;
}

// The К1801ВМ1 timing table (timing = vm1): every opcode lands on a form or on
// the fallback, every template ends with the prefetch, and a few forms repeated
// in a loop give what the chip and the real machines give. Fast memory is the
// "fast/fast" column of BK0010-11-instructions-speed.xlsx (the chip simulated
// gives the same), the ВП1-037 ones are photos of a real БК0010 and БК0011М
bool check_vm1_timing(std::string &message)
{
    for (unsigned int w = 0; w < 0x10000; w++)
        for (int taken = 0; taken < 2; taken++) {
            const unsigned int f = Vm1BusTiming::form_of((uint16_t)w, taken != 0);
            if (f > Vm1BusTiming::F_COUNT) { message = "opcode " + oct_str(w, 6) + " maps outside the table"; return false; }
            const Vm1BusTiming::Step * s = nullptr;
            const unsigned int n = Vm1BusTiming::steps_of(f, &s);
            if (n > 0 && (s[n - 1].kind != Vm1BusTiming::K_READ || s[n - 1].role != Vm1BusTiming::R_STREAM)) {
                message = "the form of " + oct_str(w, 6) + " does not end with the prefetch"; return false;
            }
        }

    BusReply vp037;             // the БК configurations, deploy/computers/bk
    vp037.window_ns = 1333; vp037.setup_read_ns = 250; vp037.setup_write_ns = 400;
    vp037.setup_rmw_ns = 700; vp037.reply_ns = 550;

    // clocks of one instruction, the form repeated with code and data behind reply
    auto loop = [](unsigned int clock, uint16_t w, const BusReply * reply) -> double {
        Vm1BusTiming t(clock);
        const unsigned int form = Vm1BusTiming::form_of(w, true);
        const Vm1BusTiming::Step * s = nullptr;
        const unsigned int n = Vm1BusTiming::steps_of(form, &s);
        unsigned int sum = 0;
        for (int i = 0; i < 48; i++) {
            for (unsigned int k = 0; k + 1 < n; k++)
                t.record(0, s[k].kind == Vm1BusTiming::K_WRITE, s[k].role == Vm1BusTiming::R_STREAM, reply);
            const int c = t.finish(form, reply, false);
            if (i >= 24) sum += (unsigned int)c;
        }
        return sum / 24.0;
    };
    struct Case { const char * name; uint16_t w; double fast, bk10, bk11; };
    static const Case cases[] = {
        { "CMP R1,R2",   0020102,  8, 12, 16 },
        { "CMP (R1),R2", 0021102, 18, 28, 32 },
        { "MOV R1,(R2)", 0010112, 26, 36, 32 },
        { "TST -(R1)",   0005741,  0, 28, 32 },
        { "CLR @-(R2)",  0005052,  0, 48, 48 },
        { "COM (R0)",    0005110,  0, 32, 37.333 },
    };
    for (const Case & c : cases) {
        const double got[3] = { loop(3000000, c.w, nullptr), loop(3000000, c.w, &vp037), loop(4000000, c.w, &vp037) };
        const double want[3] = { c.fast, c.bk10, c.bk11 };
        static const char * where[3] = { "static memory", "БК0010", "БК0011М" };
        for (int k = 0; k < 3; k++)
            if (want[k] > 0 && (got[k] < want[k] - 0.01 || got[k] > want[k] + 0.01)) {
                message = std::string(c.name) + " on " + where[k] + ": " + std::to_string(got[k])
                        + " clocks instead of " + std::to_string(want[k]);
                return false;
            }
    }
    return true;
}

// The КМ1801ВМ2 timing table (timing = vm2): every opcode lands on a form or on
// the fallback, and a few loops give what the simulated chip gives
// (tools/vm2-timing), with a static memory and with the КЦГД video memory: a
// processor window every 12 clocks, RPLY 4 clocks after it
bool check_vm2_timing(std::string &message)
{
    for (unsigned int w = 0; w < 0x10000; w++)
        for (int taken = 0; taken < 2; taken++)
            for (int v = 0; v < 2; v++)
                if (Vm1BusTiming::form2_of((uint16_t)w, taken != 0, v != 0) > Vm1BusTiming::F2_COUNT) {
                    message = "opcode " + oct_str(w, 6) + " maps outside the table"; return false;
                }

    struct Window : public WaitSource {
        uint64_t now = 0;           //where the device is clocked to
        unsigned int wait_states(unsigned int address, bool, unsigned int offset) override {
            if (address >= 0100000) return 0;
            const uint64_t t = now + offset;
            const uint64_t z = t + 12 - (t + 6) % 12;   //the first window point after t
            return (unsigned int)(z + 4 - t);
        }
    };
    // One loop iteration: the forms and what the core reads and writes in each
    struct Access { unsigned int address; bool write; bool stream; };
    struct Form { uint16_t w; bool taken; std::vector<Access> acc; };
    auto loop = [](const std::vector<Form> & forms, bool window) -> double {
        Vm1BusTiming t(7700000, Vm1BusTiming::CHIP_VM2);
        Window vram;
        WaitSource * wait = window ? &vram : nullptr;
        unsigned int sum = 0;
        for (int i = 0; i < 48; i++)
            for (const Form & f : forms) {
                //The opcode fetch first, as the core reads it: finish() skips it
                t.record(01000, false, true, nullptr, wait);
                for (const Access & a : f.acc)
                    t.record(a.address, a.write, a.stream, nullptr, a.address < 0100000 ? wait : nullptr);
                const int c = t.finish(Vm1BusTiming::form2_of(f.w, f.taken, false), nullptr, true, wait, 01000);
                vram.now += (unsigned int)c;
                if (i >= 24) sum += (unsigned int)c;
            }
        return sum / 24.0;
    };
    const Form sob = { 0077104, true, {} };
    struct Case { const char * name; std::vector<Form> forms; double fast, kcgd; };
    const std::vector<Case> cases = {
        { "TST @#a; BMI; SOB", { { 0005737, false, { { 01006, false, true }, { 0167774, false, false } } },
                                 { 0100775, false, {} }, sob }, 62, 132 },
        { "MOV (R2),R4; SOB",  { { 0011204, false, { { 060000, false, false } } }, sob }, 44, 96 },
        { "MUL R1,R4; SOB",    { { 0070401, false, {} }, sob }, 120, 144 },
        // The chip: 48 on a static memory - after INC the microprogram of SOB
        // starts while the write is still on the bus, which the model does
        // not see from its templates (tools/vm2-timing/README.md)
        { "INC (R2); SOB",     { { 0005212, false, { { 060000, false, false }, { 060000, true, false } } }, sob }, 50, 120 },
        { "MOV R4,(R2); SOB",  { { 0010412, false, { { 060000, true, false } } }, sob }, 44, 96 },
    };
    for (const Case & c : cases) {
        const double got[2] = { loop(c.forms, false), loop(c.forms, true) };
        const double want[2] = { c.fast, c.kcgd };
        static const char * where[2] = { "static memory", "КЦГД video memory" };
        for (int k = 0; k < 2; k++)
            if (got[k] < want[k] - 0.01 || got[k] > want[k] + 0.01) {
                message = std::string(c.name) + " on " + where[k] + ": " + std::to_string(got[k])
                        + " clocks instead of " + std::to_string(want[k]);
                return false;
            }
    }
    return true;
}

//The sound fitted to the audio device, which the suite cannot see since it runs
//silent: the slices of the emulation and the periods of a device are played
//against each other on paper, with a tone in them
struct RateCase
{
    double drift;                   //the clocks apart
    unsigned int period, target;    //frames
    double stall;                   //seconds lost every 3 s and caught up
    double stop;                    //seconds the machine stands at 30 s
    double big_stall;               //seconds lost once at 30 s and caught up
    double fixed;                   //frames of a set target from the start
    double set_at_30;               //frames of a set target from 30 s
    double away;                    //seconds from 20 s at a speed the host holds 80% of
    double blocked;                 //seconds of every 0.1 s the device is late, as a page's main thread
};

struct RateResult
{
    unsigned int early = 0, late = 0;   //periods the buffer failed, before and after 50 s
    double mean = 0, target = 0;        //what it holds after 50 s, what it is kept at
    double wobble = 0;                  //the rate off the drift after 50 s, at most
    double swing = 0;                   //the same from 5 s on
    double far = 0;                     //seconds from 30 s the level is 20 ms off the target
    double breaks = 0;                  //the worst break in the tone
};

RateResult play_rate_match(const RateCase &c)
{
    const double rate = 44100, total = 100, settled = 50, slice = 0.001;
    const double amplitude = 8000, w = 2 * 3.14159265358979 * 1000 / rate;
    AudioRateMatch match;
    match.setup(rate, c.target, rate * 0.15);
    if (c.fixed > 0) match.set_fixed(c.fixed);
    RateResult res;
    double made = 0, level = 0, phase = 0;
    double t_slice = 0, t_period = 0, late = 0, next_stall = 6, level_sum = 0;
    double y1 = 0, y2 = 0;
    uint32_t noise = 12345;
    long count = 0, level_n = 0;
    bool stopped = false, stalled = false, started = false, set = false;
    auto out = [&](float v, float) {
        level += 1;
        //Of a sine the second difference is the sine itself scaled: a slipped
        //frame jumps by the amplitude times w, 1100 here
        if (count++ > 8) res.breaks = std::max(res.breaks, std::fabs(v - 2 * y1 + y2 + w * w * y1));
        y2 = y1;
        y1 = v;
    };
    while (t_period < total) {
        //A device held up takes its periods one after another afterwards
        const double held = std::fmod(t_period, 0.1);
        const double t_take = (held < c.blocked) ? t_period - held + c.blocked : t_period;
        const bool away = c.away > 0 && t_period >= 20 && t_period < 20 + c.away;
        if (t_slice + late <= t_take) {
            made += slice * rate * (1 + c.drift) * (away ? 0.8 : 1);
            for (; made >= 1; made -= 1) {
                match.push((float)(amplitude * std::sin(phase)), 0, out);
                phase += w;
                if (phase > 6.28318530717959) phase -= 6.28318530717959;
            }
            t_slice += slice;
            noise = noise * 1103515245u + 12345u;
            late = 0.005 * ((noise >> 16) & 255) / 255;
            if (c.stall > 0 && t_slice >= next_stall) {
                late = c.stall;
                next_stall += 3;
            }
            if (c.big_stall > 0 && !stalled && t_slice >= 30) {
                late = c.big_stall;
                stalled = true;
            }
            if (c.stop > 0 && !stopped && t_slice >= 30) {
                t_slice += c.stop;
                stopped = true;
            }
            continue;
        }
        t_period += c.period / rate;
        if (away) match.hold_off();
        if (c.set_at_30 > 0 && !set && t_period >= 30) {
            match.set_fixed(c.set_at_30);
            set = true;
        }
        if (level < c.period || match.filling(level, c.period)) {
            //The same at every start, which is no failure, and at 1x alone
            if (!away) match.underrun(c.period);
            if (started && !away && !set && !stopped) {
                if (t_period < settled) res.early++; else res.late++;
            }
            continue;
        }
        started = true;
        level -= c.period;
        match.measure(level, c.period);
        level -= match.surplus(level, c.period);
        if (t_period >= 5) res.swing = std::max(res.swing, std::fabs(match.rate() - c.drift));
        if (t_period >= 30 && std::fabs(level - match.target()) > 0.02 * rate) res.far += c.period / rate;
        if (t_period < settled) continue;
        level_sum += level;
        level_n++;
        res.wobble = std::max(res.wobble, std::fabs(match.rate() - c.drift));
    }
    res.mean = level_sum / level_n;
    res.target = match.target();
    return res;
}

//The level must come to the target and stay, the rate must settle at the
//difference of the clocks and stand still, and the tone must have no breaks:
//slipping frames holds the level just as well, and the tones of the Агат ЯЗС
//howl with it. A host that stalls must get its reserve, and one that merely
//stopped or ran at another speed must not
bool check_rate_match(std::string &message)
{
    RateCase base = {};
    base.drift = 0.0001;
    base.period = 441;
    base.target = 441;

    const double drifts[] = {0.0001, -0.0001, 0.002, -0.002};
    for (double drift : drifts) {
        //A window: 1 ms slices, 10 ms periods, up to 5 ms late on a busy host
        RateCase c = base;
        c.drift = drift;
        const RateResult r = play_rate_match(c);
        const std::string at = ", drift " + std::to_string(drift);
        if (r.early + r.late != 0) {
            message = "the buffer ran dry on a steady host" + at;
            return false;
        }
        //Clocks 0.2% apart take a busy host's reserve down to nothing once,
        //before the loop has found the drift: one raise, and a fair one
        if (r.target > c.target * (std::fabs(drift) < 0.001 ? 1 : 1.5)) {
            message = "the target moved on a steady host: " + std::to_string(r.target) + at;
            return false;
        }
        if (std::fabs(r.mean - r.target) > 0.25 * r.target) {
            message = "the level stays at " + std::to_string(r.mean) + " of " + std::to_string(r.target) + at;
            return false;
        }
        if (r.wobble > 0.0002) {
            message = "the rate does not stand still: " + std::to_string(r.wobble) + at;
            return false;
        }
        if (r.breaks > 8) {
            message = "a break in the tone: " + std::to_string(r.breaks) + at;
            return false;
        }
    }

    //30 ms lost every 3 s: heard at first and then never again, and the raises
    //do not move the sound on the way
    RateCase c = base;
    c.stall = 0.03;
    RateResult r = play_rate_match(c);
    if (r.early == 0) {
        message = "the stalls of the model did not reach the device";
        return false;
    }
    if (r.late != 0 || r.target <= 441) {
        message = "the target did not grow out of the stalls: " + std::to_string(r.target)
            + ", failed " + std::to_string(r.late) + " times after that";
        return false;
    }
    if (r.swing > 0.002 || r.breaks > 8) {
        message = "the sound moves with the stalls: " + std::to_string(r.swing);
        return false;
    }

    //A machine stopped for 2 s, and a turbo the host cannot hold, are no trouble
    //of the reserve
    c = base;
    c.stop = 2;
    r = play_rate_match(c);
    if (r.target != 441) {
        message = "a stopped machine raised the target to " + std::to_string(r.target);
        return false;
    }
    c = base;
    c.away = 15;
    r = play_rate_match(c);
    if (r.target != 441) {
        message = "another speed raised the target to " + std::to_string(r.target);
        return false;
    }

    //A stall too long for any reserve: over with at once, not over half a minute
    c = base;
    c.big_stall = 0.3;
    r = play_rate_match(c);
    if (r.far > 1) {
        message = "a long stall left the level off for " + std::to_string(r.far) + " s";
        return false;
    }

    //A target set in the window covers the same stalls from the first one on,
    //and one too small for them is not raised behind the user's back, nor does
    //it leave the sound at the limit of the rate
    c = base;
    c.stall = 0.03;
    c.fixed = 2205;
    r = play_rate_match(c);
    if (r.target != 2205 || r.early + r.late != 0 || r.swing > 0.001) {
        message = "a set target: " + std::to_string(r.target) + ", failed " + std::to_string(r.early + r.late)
            + " times, rate off by " + std::to_string(r.swing);
        return false;
    }
    c.fixed = 441;
    r = play_rate_match(c);
    if (r.target != 441 || r.wobble > 0.001) {
        message = "a set target grew to " + std::to_string(r.target) + ", rate off by " + std::to_string(r.wobble);
        return false;
    }

    //Changed on the run, it is there at once both ways
    c = base;
    c.fixed = 6615;
    c.set_at_30 = 441;
    r = play_rate_match(c);
    if (r.target != 441 || r.far > 1) {
        message = "setting a smaller target took " + std::to_string(r.far) + " s";
        return false;
    }
    c = base;
    c.set_at_30 = 6615;
    r = play_rate_match(c);
    if (r.target != 6615 || r.far > 1) {
        message = "setting a larger target took " + std::to_string(r.far) + " s";
        return false;
    }

    //A web page: the emulation has its own thread, the device, a
    //ScriptProcessorNode, is late with the main thread
    c = base;
    c.drift = 0.0005;
    c.period = 2048;
    c.target = 661;
    r = play_rate_match(c);
    if (r.early + r.late != 0 || r.target != 661 || r.wobble > 0.001) {
        message = "a web page: failed " + std::to_string(r.early + r.late) + " times, rate off by " + std::to_string(r.wobble);
        return false;
    }
    c.blocked = 0.04;
    r = play_rate_match(c);
    if (r.late != 0) {
        message = "a web page with a busy main thread failed " + std::to_string(r.late) + " times";
        return false;
    }
    return true;
}

//The debugger's listing: the decoders report where a jump goes, the backward
//disassembly reads an instruction with an operand as one (not the operand as
//an instruction of its own), and the arrows of nested jumps get lanes
bool check_disassembly(const std::string &data_path, std::string &message)
{
    std::vector<uint8_t> mem(0x10000, 0);
    auto peek = [&mem](unsigned int a) -> uint8_t { return mem[a & 0xFFFF]; };

    //PDP-11: MOV #0,R0 / JSR PC,+10 / BR . at 001000; zeros (HALT) below
    const uint16_t pdp[] = {0012700, 0000000, 0004767, 0000010, 0000777};
    for (unsigned int i = 0; i < 5; i++) {
        mem[01000 + i * 2] = pdp[i] & 0xFF;
        mem[01000 + i * 2 + 1] = pdp[i] >> 8;
    }
    DisAsmPDP11 dp;
    DisasmBuffer bp(&dp, peek, 0x10000);
    bp.reset(01010, 1);
    if (bp.extend_up(2) != 2 || bp.at(0).address != 01000 || bp.at(1).address != 01004) {
        message = "PDP-11 backward disassembly: " + oct_str(bp.at(0).address, 6);
        return false;
    }
    const DisasmLine &jsr = bp.at(1).d;
    if (jsr.flow != FLOW_CALL || !jsr.has_target || jsr.target != 01020) {
        message = "PDP-11 JSR target";
        return false;
    }
    const DisasmLine &br = bp.at(2).d;
    if (br.flow != FLOW_JUMP || br.conditional || br.target != 01010) {
        message = "PDP-11 BR target";
        return false;
    }

    //8080: MVI A,0 / CALL 1234 / JMP 0100 at 0100, NOPs below
    std::fill(mem.begin(), mem.end(), 0);
    const uint8_t i80[] = {0x3E, 0x00, 0xCD, 0x34, 0x12, 0xC3, 0x00, 0x01};
    for (unsigned int i = 0; i < sizeof(i80); i++) mem[0x100 + i] = i80[i];
    DisAsm d80;
    if (!d80.load_file(data_path + "i8080.dis")) {
        message = "i8080.dis";
        return false;
    }
    DisasmBuffer b80(&d80, peek, 0x10000);
    b80.reset(0x102, 2);
    if (b80.extend_up(1) != 1 || b80.at(0).address != 0x100) {
        message = "8080 backward disassembly: " + hex_str(b80.at(0).address, 4);
        return false;
    }
    if (b80.at(1).d.flow != FLOW_CALL || b80.at(1).d.target != 0x1234
        || b80.at(2).d.flow != FLOW_JUMP || b80.at(2).d.target != 0x100) {
        message = "8080 jump targets";
        return false;
    }

    //65C02: BBR0 $12 jumps from the end of its three bytes
    DisAsm d65;
    if (!d65.load_file(data_path + "65c02.dis")) {
        message = "65c02.dis";
        return false;
    }
    uint8_t bbr[15] = {0x0F, 0x12, 0x05};
    DisasmLine l65;
    d65.decode(&bbr, 0x200, 3, &l65);
    if (l65.flow != FLOW_JUMP || !l65.conditional || l65.target != 0x208
        || l65.text.find("0208") == std::string::npos) {
        message = "65C02 BBR: " + l65.text;
        return false;
    }

    //Arrows: 0000 JMP 0009, 0003 JNZ 0006, 0006 JMP 0000 - spans of 1, 2 and
    //3 rows, one inside the other
    std::fill(mem.begin(), mem.end(), 0);
    const uint8_t jumps[] = {0xC3, 0x09, 0x00, 0xC2, 0x06, 0x00, 0xC3, 0x00, 0x00};
    for (unsigned int i = 0; i < sizeof(jumps); i++) mem[i] = jumps[i];
    DisasmBuffer bj(&d80, peek, 0x10000);
    bj.reset(0, 6);
    const std::vector<DisasmBuffer::Arrow> arrows = bj.layout_jumps(0, 5, 8);
    int lanes[3] = {-1, -1, -1};
    for (const DisasmBuffer::Arrow &a : arrows)
        if (a.from >= 0 && a.from < 3) lanes[a.from] = a.lane;
    if (arrows.size() != 3 || lanes[1] != 0 || lanes[2] != 1 || lanes[0] != 2) {
        message = "jump arrow lanes";
        return false;
    }
    return true;
}

//The launch of a program by its platform: the names a catalog sends match the
//table however they are written, the program is found in an archive by the
//rules of platform_launch.h, and the table of the installation holds together -
//no name in two blocks, every script there and starting with MACHINE
bool check_platforms(const std::string &work_path, std::string &message)
{
    struct Key { const char * a; const char * b; };
    const Key same[] = {
        {"  БК0010   (11М) ", "бк0010 (11м)"},
        {"BK11M", "bk11m"},
        {"ЁЖ", "ёж"},
        {"БК0011М\xC2\xA0+ СМК512", "бк0011м + смк512"},
    };
    for (size_t i = 0; i < sizeof(same) / sizeof(same[0]); i++)
        if (platform_name_key(same[i].a) != platform_name_key(same[i].b))
        {
            message = std::string("'") + same[i].a + "' and '" + same[i].b + "' are different platforms";
            return false;
        }
    if (platform_name_key("БК0010") == platform_name_key("БК0011"))
    {
        message = "two platforms compare equal";
        return false;
    }

    struct Pick { const char * archive; std::vector<std::string> names; int want; };
    const std::vector<std::string> bin(1, ".bin");
    const Pick picks[] = {
        //By the name of the archive without the number of the catalog, in any case
        {"1373-terra1.zip", {"readme.txt", "other.bin", "TERRA1.BIN"}, 2},
        //The name differs ("BABY207_bin" holds "BABY207.bin"): the only program
        {"3-BABY207_bin.zip", {"BABY207.bin", "BABY207.txt"}, 0},
        //A number without a dash is a name
        {"2048.zip", {"1.bin", "2048.bin"}, 1},
        //Directories and names that would land outside are not candidates
        {"5-game.zip", {"dir/", "../game.bin", "dir/game.bin"}, 2},
        //Nothing to run, or no way to choose
        {"9-krakout4661.zip", {"krakout.img"}, -1},
        {"7-two.zip", {"a.bin", "b.bin"}, -1},
    };
    for (size_t i = 0; i < sizeof(picks) / sizeof(picks[0]); i++)
    {
        std::string problem;
        const int got = pick_archive_member(picks[i].archive, picks[i].names, bin, problem);
        if (got != picks[i].want || (got < 0) != !problem.empty())
        {
            message = std::string(picks[i].archive) + ": file " + std::to_string(got) + " picked, "
                    + std::to_string(picks[i].want) + " expected";
            return false;
        }
    }

    //The table of the installation, next to computers/
    std::string root = work_path;
    while (!root.empty() && root[root.size() - 1] == '/') root.erase(root.size() - 1);
    const std::string launch_dir = root.substr(0, root.find_last_of('/') + 1) + "launch/";
    EmulatorConfig table;
    const std::string table_file = launch_dir + PLATFORMS_TABLE;
    if (!dsk_tools::file_exists(table_file)) { message = table_file + " is not found"; return false; }
    emulator::Result res = table.load_from_text(dsk_tools::utf8_read_file(table_file) + "\n");
    if (!res) { message = table_file + ": " + strip_message_context(res.message); return false; }
    std::map<std::string, std::string> owner;
    for (unsigned int i = 0; i < table.get_devices_count(); i++)
    {
        EmulatorConfigDevice * d = table.get_device(static_cast<int>(i));
        std::vector<std::string> names = split_string(d->get_parameter("names", false).value, '|', true);
        names.push_back(d->name);
        for (size_t n = 0; n < names.size(); n++)
        {
            const std::string key = platform_name_key(names[n]);
            if (owner.count(key) && owner[key] != d->name)
            {
                message = "the name '" + str_trim(names[n]) + "' is in both " + owner[key] + " and " + d->name;
                return false;
            }
            owner[key] = d->name;
        }
        for (size_t p = 0; p < d->parameters.size(); p++)
        {
            if (d->parameters[p].name == "names") continue;
            const std::string script = launch_dir + str_trim(d->parameters[p].value);
            std::vector<ScriptCommand> commands;
            std::vector<std::string> errors;
            res = parse_script_file(script, commands, errors);
            if (!res || !errors.empty() || commands.empty() || commands[0].verb != SCRIPT_CMD_MACHINE
                || commands[0].args.empty())
            {
                message = d->name + ": " + script + " does not start with MACHINE";
                return false;
            }
            const std::string machine = work_path + commands[0].args[0];
            if (!dsk_tools::file_exists(machine))
            {
                message = d->name + ": " + script + " names " + machine + ", which is not there";
                return false;
            }
        }
    }
    return true;
}

int run_selftest(const std::string &work_path, const std::string &data_path,
                 const std::string &software_path)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(work_path, ec), end; it != end; it.increment(ec))
    {
        if (ec) break;
        const std::string ext = lowercase(it->path().extension().string());
        if (it->is_regular_file(ec) && (ext == ".cfg" || ext == ".ext"))
            files.push_back(it->path().generic_string());
    }
    std::sort(files.begin(), files.end());

    if (files.empty())
    {
        std::cout << "No configurations found in " << work_path
                  << " - run from the deploy directory or pass --workdir" << std::endl;
        return 2;
    }

    unsigned int failed = 0;
    for (size_t i = 0; i < files.size(); i++)
    {
        std::string message;
        if (check_config_round_trip(files[i], work_path, message)) continue;
        std::cout << "FAIL " << files[i] << ": " << message << std::endl;
        failed++;
    }
    std::cout << "config round trip: " << (files.size() - failed) << " of " << files.size()
              << " configurations" << (failed ? " - FAILED" : " - ok") << std::endl;

    unsigned int drawn_failed = 0;
    unsigned int drawn_checked = 0;
    for (size_t i = 0; i < files.size(); i++)
    {
        std::string message;
        EmulatorConfig probe;
        if (!probe.load_from_file(files[i])) continue;
        bool has_picture = false;
        for (unsigned int k = 0; k < probe.get_devices_count() && !has_picture; k++)
            has_picture = !probe.get_device(static_cast<int>(k))->get_parameter("picture", false).value.empty()
                       && !probe.get_device(static_cast<int>(k))->get_parameter("keys", false).value.empty();
        if (!has_picture) continue;
        drawn_checked++;
        if (check_keyboard_picture(files[i], data_path, software_path, message)) continue;
        std::cout << "FAIL " << files[i] << ": " << message << std::endl;
        drawn_failed++;
    }
    failed += drawn_failed;
    std::cout << "keyboard pictures: " << (drawn_checked - drawn_failed) << " of " << drawn_checked
              << " machines" << (drawn_failed ? " - FAILED" : " - ok") << std::endl;

    std::string message;
    const bool zip_ok = check_zip_round_trip(message);
    if (!zip_ok) { std::cout << "FAIL zip: " << message << std::endl; failed++; }
    std::cout << "zip round trip: " << (zip_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    bool ext_ok = check_ext_devices(message);
    if (ext_ok) ext_ok = check_ext_editor_keeps_blocks(work_path, message);
    if (ext_ok) ext_ok = check_ext_editor_copy(work_path, message);
    if (ext_ok) ext_ok = check_ext_editor_zip(work_path, message);
    if (ext_ok) ext_ok = check_ext_editor_inline_zip(work_path, message);
    if (ext_ok) ext_ok = check_ext_editor_phase(work_path, message);
    if (!ext_ok) { std::cout << "FAIL ext devices: " << message << std::endl; failed++; }
    std::cout << "extension device blocks: " << (ext_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool hex_ok = check_hex_round_trip(message);
    if (!hex_ok) { std::cout << "FAIL hex: " << message << std::endl; failed++; }
    std::cout << "hex dump round trip: " << (hex_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool zx_ok = check_zx_tape_round_trip(message);
    if (!zx_ok) { std::cout << "FAIL zx tape: " << message << std::endl; failed++; }
    std::cout << "ZX tape round trip: " << (zx_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool vm1_ok = check_vm1_timing(message);
    if (!vm1_ok) { std::cout << "FAIL vm1 timing: " << message << std::endl; failed++; }
    std::cout << "К1801ВМ1 timing table: " << (vm1_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool vm2_ok = check_vm2_timing(message);
    if (!vm2_ok) { std::cout << "FAIL vm2 timing: " << message << std::endl; failed++; }
    std::cout << "КМ1801ВМ2 timing table: " << (vm2_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool disasm_ok = check_disassembly(data_path, message);
    if (!disasm_ok) { std::cout << "FAIL disassembly: " << message << std::endl; failed++; }
    std::cout << "debugger listing: " << (disasm_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool rate_ok = check_rate_match(message);
    if (!rate_ok) { std::cout << "FAIL sound rate: " << message << std::endl; failed++; }
    std::cout << "sound rate match: " << (rate_ok ? "ok" : "FAILED") << std::endl;

    message.clear();
    const bool platforms_ok = check_platforms(work_path, message);
    if (!platforms_ok) { std::cout << "FAIL platforms: " << message << std::endl; failed++; }
    std::cout << "platform launch: " << (platforms_ok ? "ok" : "FAILED") << std::endl;

    return failed ? 1 : 0;
}

//Mirrors the platform layout the windowed build resolves in its constructor
struct Paths
{
    std::string work;
    std::string software;
    std::string data;
    std::string ini;
    std::string user_ext;
    std::string cache;
};

Paths resolve_paths(const std::string &argv0)
{
    std::string app_path;
    try {
        app_path = fs::absolute(fs::path(argv0)).parent_path().generic_string();
    } catch (const std::exception &) {
        app_path = ".";
    }
    const std::string current_path = fs::current_path().generic_string();

    std::string root;
    Paths p;

#if defined(_WIN32)
    p.ini = (fs::exists(fs::path(app_path) / "ecat.ini"))
        ? app_path + "/ecat.ini"
        : current_path + "/ecat.ini";
    root = (fs::is_directory(fs::path(current_path) / "computers")) ? current_path : app_path;
#else
    root = (fs::is_directory(fs::path(current_path) / "computers"))
        ? current_path
        : (fs::path(app_path).parent_path() / "share" / "ecat").generic_string();

    #if defined(__APPLE__)
        const char * home = getenv("HOME");
        p.ini = std::string(home ? home : ".") + "/.ecat.ini";
    #else
        const char * home = getenv("HOME");
        p.ini = std::string(home ? home : ".") + "/.config/ecat.ini";
    #endif

    //The console build never creates the per user copy the windowed one makes:
    //if it is not there, the one next to the machines is used as it is
    if (!fs::exists(p.ini)) p.ini = root + "/ecat.ini";
#endif

    //deploy/ecat.ini is the developer's own file and is not in git: a fresh
    //checkout carries only the distributed defaults, and without their
    //[TapeFiles] no tape image loads. The error code is swallowed, because a
    //parallel test run may be making the same copy at the same moment
    {
        std::error_code ec;
        if (!fs::exists(p.ini, ec) && fs::exists(root + "/.ecat.ini", ec))
            fs::copy_file(root + "/.ecat.ini", p.ini, ec);
    }

    p.work     = root + "/computers/";
    p.software = root + "/software/";
    p.data     = root + "/data/";
    p.user_ext = default_user_ext_path(root);
    {
        std::error_code ec;
        const fs::path tmp = fs::temp_directory_path(ec);
        p.cache = ec ? root + "/ecat3-cache/" : (tmp / "ecat3-cache").generic_string() + "/";
    }
    return p;
}

//Absolute, or resolving against the current directory, is taken as is;
//everything else is relative to computers/, exactly as in the windowed build
std::string resolve_startup_path(const std::string &file_name, const std::string &work_path)
{
    if (file_name.empty()) return file_name;
    const fs::path p = u8(file_name);
    if (p.is_absolute()) return file_name;
    if (fs::exists(p)) return fs::absolute(p).generic_u8string();
    return work_path + file_name;
}

// Brings machines up and down. In the console build there is no interface
// thread to marshal to: the MCP server runs on the main thread and calls
// straight in, which is why this frontend needs no bridge at all.
class HeadlessHost
#ifdef ENABLE_MCP
    : public mcp::McpHost
#endif
{
public:
    HeadlessHost(Emulator * e, const std::string &work_path):
          m_emulator(e)
        , m_work_path(work_path)
        , m_quit(false)
        , m_exit_code(0)
    {}

    std::string load_machine(const std::string &config)
#ifdef ENABLE_MCP
        override
#endif
    {
        if (config.empty()) return "A configuration file name is required.";

        if (m_emulator->loaded)
        {
            m_emulator->stop_script();
            m_emulator->stop_emulation();
        }

        const std::string path = resolve_startup_path(config, m_work_path);
        emulator::Result res = m_emulator->load_config(path);
        if (!res)
        {
            m_current.clear();
            return strip_message_context(res.message);
        }

        m_emulator->set_volume(atoi(m_emulator->read_setup("Sound", "volume", "50").c_str()));
        m_emulator->set_muted(m_emulator->read_setup("Sound", "muted", "0") == "1");

        //nullptr for the widget pointer: the null renderer ignores it
        m_emulator->init_video(nullptr);
        m_emulator->run();

        m_current = path;
        return std::string();
    }

    void quit(int code)
#ifdef ENABLE_MCP
        override
#endif
    {
        m_exit_code = code;
        m_quit = true;
    }

    std::string current_machine()
#ifdef ENABLE_MCP
        override
#endif
    {
        return m_current;
    }

    bool should_quit() const { return m_quit; }
    int  exit_code() const   { return m_exit_code; }

private:
    Emulator *  m_emulator;
    std::string m_work_path;
    std::string m_current;
    bool        m_quit;
    int         m_exit_code;
};

} // namespace

int main(int argc, char *argv[])
{
    Options o = parse_options(utf8_arguments(argc, argv));

    if (o.bad)
    {
        std::cerr << "Unknown argument: " << o.bad_argument << std::endl;
        print_help();
        return 2;
    }
    if (o.help)    { print_help(); return 0; }
    if (o.version) { std::cout << "eCat3 " << PROJECT_VERSION << std::endl; return 0; }

    //The launch script is the script of the run and names the machine itself
    if (!o.platform.empty() && (!o.script.empty() || !o.config.empty() || o.mcp || o.program.empty()))
    {
        std::cerr << (o.program.empty()
                      ? "--platform needs the file of a program to run"
                      : "--platform cannot be combined with --script, --config or --mcp")
                  << std::endl;
        return 2;
    }
    //Relative to the directory the user is in, before --workdir moves away
    o.program = absolute_utf8(o.program);

#ifndef ENABLE_MCP
    if (o.mcp)
    {
        std::cerr << "This build has no MCP server. Rebuild with -DENABLE_MCP=ON, see docs/MCP.md."
                  << std::endl;
        return 2;
    }
#endif

    if (!o.workdir.empty())
    {
        std::error_code ec;
        fs::current_path(u8(o.workdir), ec);
        if (ec)
        {
            std::cerr << "Unable to change the working directory to " << o.workdir << std::endl;
            return 2;
        }
    }

    if (o.selftest)
    {
        const Paths sp = resolve_paths(argv[0]);
        return run_selftest(sp.work, sp.data, sp.software);
    }

    if (!o.mcp && o.script.empty() && o.config.empty() && o.print_config.empty() && o.platform.empty())
    {
        std::cerr << "Nothing to do: the console build needs a script, a configuration or --mcp."
                  << std::endl;
        print_help();
        return 2;
    }

    const Paths paths = resolve_paths(argv[0]);

    //The machine as the emulator would build it, in .cfg syntax: what a saved
    //state carries. Two files that print the same are the same machine, which
    //is how a .cfg rewritten as an extension is checked
    if (!o.print_config.empty())
    {
        MachinePaths mp;
        mp.computers_path = paths.work;
        mp.cache_path = paths.cache;
        EmulatorConfig cfg;
        MachineSource source;
        emulator::Result res = load_machine_description(resolve_startup_path(o.print_config, paths.work), mp, cfg, source);
        if (!res)
        {
            std::cerr << strip_message_context(res.message) << std::endl;
            return 2;
        }
        std::cout << serialize_config(cfg);
        return 0;
    }

    NullRenderer renderer;
    //The order is work, data, software - not the order they are declared in
    Emulator emulator(paths.work, paths.data, paths.software, paths.ini, &renderer);
    //Set before any machine is loaded, and it holds across a change of machine
    if (o.no_sound) emulator.set_audio_enabled(false);
    if (!o.speed.empty())
    {
        const int q = Emulator::parse_speed(o.speed);
        if (q < 0)
        {
            std::cerr << "Invalid speed: " << o.speed << " (0.25 to 1000 in steps of 0.25, or max)" << std::endl;
            return 2;
        }
        emulator.set_speed(q);
    }
    emulator.user_ext_path = paths.user_ext;
    emulator.cache_path = paths.cache;
    //Unpacked archives and inline data nobody has used for a month
    cache_cleanup(emulator.cache_path, CACHE_MAX_AGE_DAYS);
    HeadlessHost host(&emulator, paths.work);

    //A script is parsed before the machine is loaded: its MACHINE command may
    //name the configuration to start with
    std::string config = o.config;
    if (!o.script.empty())
    {
        emulator::Result res = emulator.load_script(
            dsk_tools::file_exists(o.script) ? absolute_utf8(o.script) : o.script);
        if (!res)
        {
            std::cerr << strip_message_context(res.message) << std::endl;
            return 2;
        }
        if (config.empty()) config = emulator.script_machine();
    }
    //--platform is one line of script, PLATFORM: loading it puts the launch
    //script of the platform in its place, and that one names the machine. The
    //name is in the cache, so a log of its errors does not land in deploy/
    const bool launch = !o.platform.empty();
    if (launch)
    {
        emulator::Result res = emulator.load_script_text(platform_script_text(o.platform, o.program),
                                                         platform_script_name(emulator.cache_path));
        if (!res)
        {
            std::cerr << strip_message_context(res.message) << std::endl;
            return 2;
        }
        config = emulator.script_machine();
    }
    if (config.empty()) config = emulator.read_setup("Startup", "default", "");

    int exit_code = 0;

    if (o.mcp)
    {
#ifdef ENABLE_MCP
        //An MCP session leaves the working tree as it found it
        emulator.set_settings_readonly(true);

        mcp::McpSession session(&emulator, &host);
        mcp::McpServer server(&session, PROJECT_VERSION, o.mcp_trace);

        //A machine named on the command line is loaded up front; otherwise the
        //client asks for one with ecat_machine
        if (!config.empty())
        {
            const std::string err = host.load_machine(config);
            if (!err.empty()) std::cerr << err << std::endl;
            else session.arm();
        }

        //Blocks until stdin reaches end of file
        server.run();
        exit_code = host.exit_code();
#endif
    }
    else
    {
        //An explicit script starts with the machine, at its first clock (an
        //embedded one is known only once the machine is loaded)
        if (!o.script.empty() || launch) emulator.arm_script(true);
        const std::string err = host.load_machine(config);
        if (!err.empty())
        {
            std::cerr << err << std::endl;
            return 2;
        }

        //A configuration extension may carry a script of its own. A script
        //given explicitly takes its place
        bool run_script = !o.script.empty() || launch;
        if (!run_script && emulator.has_embedded_script())
        {
            emulator::Result res = emulator.load_embedded_script();
            if (!res)
            {
                std::cerr << strip_message_context(res.message) << std::endl;
                return 2;
            }
            run_script = true;
        }

        if (run_script)
        {
            emulator.start_script();
            //The windowed build polls the engine from a 100 ms timer; here the
            //main thread has nothing else to do
            while (!emulator.script_finished() && !host.should_quit() && !emulation_died(emulator))
                compat_sleep_ms(10);
            //A launch script ends with the program started: the program goes
            //on until the process is killed, as a machine with no script does.
            //One of its commands that failed (a disk image the drive does not
            //take) means the program did not start: that is the end of the run
            if (launch && emulator.script_finished() && !emulator.script_exit_requested()
                && !emulation_died(emulator))
            {
                const std::string error = emulator.script_error();
                if (!error.empty())
                {
                    std::cerr << "The program was not started: " << strip_message_context(error) << std::endl;
                    emulator.stop_emulation();
                    return 2;
                }
            }
            if (launch && !emulator.script_exit_requested())
                while (!host.should_quit() && !emulation_died(emulator)) compat_sleep_ms(50);
            exit_code = emulation_died(emulator) ? 3 : emulator.script_exit_code();
        }
        else
        {
            //A machine with no script: run it until the process is killed
            while (!host.should_quit() && !emulation_died(emulator)) compat_sleep_ms(50);
            if (emulation_died(emulator)) exit_code = 3;
        }
    }

    emulator.stop_emulation();
    return exit_code;
}
