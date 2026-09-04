// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scripting engine, header

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "emulator/core.h"
#include "emulator/result.h"
#include "emulator/script/script_log.h"
#include "emulator/script/script_types.h"

class Emulator;

// Executes a script.
//
// The engine is a non blocking state machine: tick() is called from the
// emulation thread and returns immediately, either after running a batch of
// instant commands or after arming a delay. All delays are counted in emulated
// time, taken from the CPU clock counter, so a script replays identically
// regardless of the host load.
//
// execute() accepts a single command, so the engine can also be driven by an
// external source, for example an MCP server, without a script file.
class ScriptEngine
{
public:
    explicit ScriptEngine(Emulator * e);
    ~ScriptEngine();

    emulator::Result load(const std::string &file_name);

    const std::string & get_machine() const;    //Argument of MACHINE, empty if absent
    const std::string & get_path() const;       //Directory of the script

    void start(uint64_t clock_now);
    void tick(uint64_t clock_counter);
    void stop();

    bool is_finished() const;
    bool is_exit_requested() const;
    int  get_exit_code() const;

    emulator::Result execute(const ScriptCommand &c);
    void log(const std::string &s);

private:
    enum State {
        StateIdle,          //Not started yet
        StateRunning,       //Ready to execute the next command
        StateDelay,         //Waiting for a moment in emulated time
        StateKeys,          //Playing a key sequence
        StateWaitFor,       //Polling a device field
        StateScreenshot,    //Waiting for the render thread to store an image
        StateFinished
    };

    Emulator *                  e;
    std::string                 m_file_name;
    std::string                 m_path;
    std::string                 m_machine;
    std::vector<ScriptCommand>  m_commands;
    std::vector<std::string>    m_errors;       //Parse errors, reported on start
    std::unique_ptr<ScriptLog>  m_log;
    LogFormat                   m_format;

    State                       m_state;
    size_t                      m_pc;
    std::atomic<bool>           m_finished;
    std::atomic<bool>           m_exit_requested;
    int                         m_exit_code;

    uint64_t                    m_now;          //Last seen clock counter
    uint64_t                    m_resume_at;    //Clock value to continue at
    uint64_t                    m_ticks_per_ms; //Cached, tick() runs per instruction
    uint64_t                    m_poll_at;      //Next moment to re-check a polled state

    std::vector<unsigned int>   m_keys;         //Key sequence of KEY / TYPE
    std::vector<bool>           m_key_shift;    //Whether Shift is held for that key
    size_t                      m_key_index;
    bool                        m_key_pressed;
    unsigned int                m_key_delay;
    unsigned int                m_key_hold;

    size_t                      m_wait_pc;      //Index of the active WAITFOR
    uint64_t                    m_wait_deadline;

    uint64_t ms_to_ticks(unsigned int ms) const;
    void     delay_ms(unsigned int ms);
    void     finish();
    void     log_error(const ScriptCommand &c, const std::string &text);

    bool     key_step();                        //true when the sequence is over
    bool     waitfor_step();                    //true when the condition is met or timed out
    void     start_keys(const std::vector<unsigned int> &keys, const std::vector<bool> &shift,
                        unsigned int delay, unsigned int hold);

    ComputerDevice * find_device(const ScriptCommand &c);
    bool     read_field(const ScriptCommand &c, DeviceFieldValue &out);
    void     parse_range(const std::string &params, unsigned int * from, unsigned int * to);

    emulator::Result do_key(const ScriptCommand &c);
    emulator::Result do_type(const ScriptCommand &c);
    emulator::Result do_logdefs(const ScriptCommand &c);
    emulator::Result do_log(const ScriptCommand &c);
    emulator::Result do_command(const ScriptCommand &c);
    emulator::Result do_screen(const ScriptCommand &c);
    emulator::Result do_waitfor(const ScriptCommand &c);
};
