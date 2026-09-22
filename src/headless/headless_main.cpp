// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Console entry point: the emulator without a graphical interface

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "globals.h"
#include "emulator/emulator.h"
#include "emulator/thread_compat.h"
#include "emulator/utils.h"
#include "headless/renderer_null.h"
#include "libs/zip_reader.h"
#include "libs/zip_writer.h"
#include "emulator/state.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"

#ifdef ENABLE_MCP
    #include "mcp/mcp_server.h"
    #include "mcp/mcp_session.h"
#endif

namespace fs = std::filesystem;

namespace {

struct Options
{
    std::string config;
    std::string script;
    std::string workdir;
    bool mcp = false;
    bool mcp_trace = false;
    bool no_sound = false;
    bool selftest = false;
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

//The same options as the windowed build, parsed by hand: this frontend has no
//Qt and therefore no QCommandLineParser
Options parse_options(int argc, char *argv[])
{
    Options o;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        const bool has_next = (i + 1 < argc);

        if (a == "-h" || a == "--help")          { o.help = true; }
        else if (a == "-v" || a == "--version")  { o.version = true; }
        else if (a == "--mcp")                   { o.mcp = true; }
        else if (a == "--mcp-trace")             { o.mcp_trace = true; }
        else if (a == "--no-sound")              { o.no_sound = true; }
        else if (a == "--selftest")              { o.selftest = true; }
        else if ((a == "-c" || a == "--config")  && has_next) { o.config  = argv[++i]; }
        else if ((a == "-s" || a == "--script")  && has_next) { o.script  = argv[++i]; }
        else if (a == "--workdir"                && has_next) { o.workdir = argv[++i]; }
        else if (!a.empty() && a[0] == '-')      { o.bad = true; o.bad_argument = a; }
        else
        {
            //A positional argument is dispatched by its extension, so that a
            //script or a configuration can simply be dropped on the executable
            const std::string ext = extension_of(a);
            if (ext == "ecat" && o.script.empty())     o.script = a;
            else if (is_machine_file(a) && o.config.empty()) o.config = a;
            else { o.bad = true; o.bad_argument = a; }
        }
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
        << "      --workdir <dir>       Directory to work in, the one holding computers/\n"
        << "      --no-sound            Do not open an audio device at all\n"
        << "      --selftest            Check the internal invariants and exit\n"
#ifdef ENABLE_MCP
        << "      --mcp                 Act as an MCP server on stdin/stdout, see docs/MCP.md\n"
        << "      --mcp-trace           Print the MCP conversation to stderr\n"
#endif
        << "  -h, --help                Show this help\n"
        << "  -v, --version             Show the version\n"
        << "\n"
        << "A positional argument is taken as a script or a configuration by its extension.\n";
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

bool check_config_round_trip(const std::string &file, std::string &message)
{
    EmulatorConfig original;
    emulator::Result res = original.load_from_file(file);
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

int run_selftest(const std::string &work_path, const std::string &data_path,
                 const std::string &software_path)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(work_path, ec), end; it != end; it.increment(ec))
    {
        if (ec) break;
        if (it->is_regular_file(ec) && lowercase(it->path().extension().string()) == ".cfg")
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
        if (check_config_round_trip(files[i], message)) continue;
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
    const bool hex_ok = check_hex_round_trip(message);
    if (!hex_ok) { std::cout << "FAIL hex: " << message << std::endl; failed++; }
    std::cout << "hex dump round trip: " << (hex_ok ? "ok" : "FAILED") << std::endl;

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
    const fs::path p(file_name);
    if (p.is_absolute()) return file_name;
    if (fs::exists(p)) return fs::absolute(p).generic_string();
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
    Options o = parse_options(argc, argv);

    if (o.bad)
    {
        std::cerr << "Unknown argument: " << o.bad_argument << std::endl;
        print_help();
        return 2;
    }
    if (o.help)    { print_help(); return 0; }
    if (o.version) { std::cout << "eCat3 " << PROJECT_VERSION << std::endl; return 0; }

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
        fs::current_path(o.workdir, ec);
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

    if (!o.mcp && o.script.empty() && o.config.empty())
    {
        std::cerr << "Nothing to do: the console build needs a script, a configuration or --mcp."
                  << std::endl;
        print_help();
        return 2;
    }

    const Paths paths = resolve_paths(argv[0]);

    NullRenderer renderer;
    //The order is work, data, software - not the order they are declared in
    Emulator emulator(paths.work, paths.data, paths.software, paths.ini, &renderer);
    //Set before any machine is loaded, and it holds across a change of machine
    if (o.no_sound) emulator.set_audio_enabled(false);
    emulator.user_ext_path = paths.user_ext;
    emulator.cache_path = paths.cache;
    HeadlessHost host(&emulator, paths.work);

    //A script is parsed before the machine is loaded: its MACHINE command may
    //name the configuration to start with
    std::string config = o.config;
    if (!o.script.empty())
    {
        emulator::Result res = emulator.load_script(
            fs::exists(o.script) ? fs::absolute(o.script).generic_string() : o.script);
        if (!res)
        {
            std::cerr << strip_message_context(res.message) << std::endl;
            return 2;
        }
        if (config.empty()) config = emulator.script_machine();
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
        const std::string err = host.load_machine(config);
        if (!err.empty())
        {
            std::cerr << err << std::endl;
            return 2;
        }

        //A configuration extension may carry a script of its own. A script
        //given explicitly takes its place
        bool run_script = !o.script.empty();
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
            while (!emulator.script_finished() && !host.should_quit())
                compat_sleep_ms(10);
            exit_code = emulator.script_exit_code();
        }
        else
        {
            //A machine with no script: run it until the process is killed
            while (!host.should_quit()) compat_sleep_ms(50);
        }
    }

    emulator.stop_emulation();
    return exit_code;
}
