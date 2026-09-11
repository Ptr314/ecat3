// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Console entry point: the emulator without a graphical interface

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "globals.h"
#include "emulator/emulator.h"
#include "emulator/thread_compat.h"
#include "emulator/utils.h"
#include "headless/renderer_null.h"

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
            else if (ext == "cfg" && o.config.empty()) o.config = a;
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
        << "  -s, --script <file.ecat>  Script to run, see SCRIPTING.md\n"
        << "      --workdir <dir>       Directory to work in, the one holding computers/\n"
        << "      --no-sound            Do not open an audio device at all\n"
#ifdef ENABLE_MCP
        << "      --mcp                 Act as an MCP server on stdin/stdout, see MCP.md\n"
        << "      --mcp-trace           Print the MCP conversation to stderr\n"
#endif
        << "  -h, --help                Show this help\n"
        << "  -v, --version             Show the version\n"
        << "\n"
        << "A positional argument is taken as a script or a configuration by its extension.\n";
}

//Mirrors the platform layout the windowed build resolves in its constructor
struct Paths
{
    std::string work;
    std::string software;
    std::string data;
    std::string ini;
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
        std::cerr << "This build has no MCP server. Rebuild with -DENABLE_MCP=ON, see MCP.md."
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

        if (!o.script.empty())
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
