// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MCP session: drives the emulator, knows nothing about transport

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "emulator/script/script_engine.h"
#include "emulator/thread_compat.h"

class Emulator;

namespace mcp {

// Collects the output of the script engine and hands back the lines produced
// by one submitted command. write() runs on the emulation thread inside the
// engine mutex, so it only takes a mutex of its own and never calls back
class CaptureSink : public ScriptSink
{
public:
    CaptureSink();
    void write(const std::string &line, size_t pc) override;

    //Removes and returns everything recorded for one command
    std::vector<std::string> drain(size_t pc);
    //Everything recorded so far, whatever it belongs to
    std::vector<std::string> drain_all();

private:
    compat_mutex m_mutex;
    std::vector<std::pair<size_t, std::string> > m_lines;
};

// Implemented by the frontend: the operations that have to run where the
// frontend keeps its own state. In the windowed build that is the GUI thread,
// in the console one the main thread
class McpHost
{
public:
    virtual ~McpHost() {}
    //Loads a machine configuration. Returns an error message, empty on success
    virtual std::string load_machine(const std::string &config) = 0;
    //Asks the frontend to terminate
    virtual void quit(int code) = 0;
    //Makes the window visible, if the build has one
    virtual void show_window() {}
    //Name of the configuration currently loaded, empty if none
    virtual std::string current_machine() = 0;
};

struct McpResult
{
    bool        ok;
    std::string text;

    static McpResult good(const std::string &t) { McpResult r; r.ok = true;  r.text = t; return r; }
    static McpResult bad(const std::string &t)  { McpResult r; r.ok = false; r.text = t; return r; }
    McpResult(): ok(true) {}
};

// One live emulator session. Every public method takes m_mutex, and so does
// the machine reload, which is why device enumeration is safe here: a config
// reload rebuilds the device manager and must not run in parallel with a read
class McpSession
{
public:
    McpSession(Emulator * e, McpHost * host);
    ~McpSession();

    //Arms interactive mode and attaches the sink. Must be called again after
    //every machine reload: load_config() stops the engine and run() restarts
    //the clock from zero
    void arm();

    McpResult run(const std::string &commands, unsigned int timeout_ms);
    McpResult status();
    McpResult devices();
    McpResult describe(const std::string &device);
    McpResult machine(const std::string &config);
    McpResult cancel();
    //On success png holds the encoded image
    McpResult screenshot(unsigned int wait_ms, unsigned int timeout_ms,
                         std::vector<unsigned char> &png);

private:
    enum WaitResult { WaitDone, WaitTimeout, WaitStalled, WaitAborted };

    Emulator *   e;
    McpHost *    m_host;
    CaptureSink  m_sink;
    compat_mutex m_mutex;
    bool         m_armed;

    uint64_t     m_last_status_clock;
    uint64_t     m_last_status_ms;

    void       arm_locked();
    //Waits for a submitted command to complete, on the wall clock
    WaitResult wait_done(size_t index, unsigned int timeout_ms);
    McpResult  run_locked(const std::string &commands, unsigned int timeout_ms);
    McpResult  machine_locked(const std::string &config);
    McpResult  devices_locked();
};

} // namespace mcp
