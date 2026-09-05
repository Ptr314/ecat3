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
#include "emulator/thread_compat.h"
#include "emulator/script/script_log.h"
#include "emulator/script/script_types.h"

class Emulator;

// Receives the output of the script engine as it is produced. Used by an
// external driver to get the result of a command back instead of, or as well
// as, the log file. write() is called from the emulation thread, inside the
// engine mutex, so an implementation must be prepared for that and must not
// call back into the engine.
class ScriptSink
{
public:
    virtual ~ScriptSink() {}
    //pc is the index of the command that produced the line, plus one: the
    //value get_done_pc() will have reached once that command is over. This is
    //what attributes output to a request
    virtual void write(const std::string &line, size_t pc) = 0;
};

// Executes a script.
//
// The engine is a non blocking state machine: tick() is called from the
// emulation thread and returns immediately, either after running a batch of
// instant commands or after arming a delay. All delays are counted in emulated
// time, taken from the CPU clock counter, so a script replays identically
// regardless of the host load.
//
// The engine owns the list of commands. Besides being loaded from a file the
// list can be edited by the GUI recorder (append, truncate, seek), so the same
// object serves both the command line and the record / replay buttons.
//
// Threads. tick() runs on the emulation thread, everything else on the
// interface thread. The state, the pointer and the two wake up moments are
// atomics: tick() checks them without a lock and returns at once when there is
// nothing to do, which is the case for nearly every instruction. Whenever a
// command is actually executed tick() holds m_mutex, and so does every call
// from the interface thread, so a pause(), stop() or a buffer edit never
// catches a command half way. The command list is only modified from the
// interface thread and only while the engine is not active, so the interface
// thread may read it without the lock while a replay runs.
//
// execute() accepts a single command, so the engine can also be driven by an
// external source, for example an MCP server, without a script file. The
// supported way to do that is interactive mode: submit() appends to the same
// buffer and the same tick() runs it, so an external driver gets the whole
// command vocabulary, the asynchronous verbs included, without a second
// execution path.
class ScriptEngine
{
public:
    explicit ScriptEngine(Emulator * e);
    ~ScriptEngine();

    //Replaces the buffer with the content of a file. Lines that do not parse
    //are skipped and reported by get_errors()
    emulator::Result load(const std::string &file_name);

    //Names the buffer after a file: the log and relative screenshot names go
    //next to it. Used after the GUI saves a recording
    void set_file_name(const std::string &file_name);

    const std::vector<std::string> & get_errors() const;
    const std::string & get_machine() const;    //Argument of MACHINE, empty if absent
    const std::string & get_path() const;       //Directory of the script

    //------------------------- Buffer editing -----------------------------//
    //Legal only while the engine is not active; otherwise the call is ignored
    void clear();
    void set_commands(const std::vector<ScriptCommand> &commands);
    void append(const ScriptCommand &c);
    void truncate(size_t pc);                   //Drops the commands from pc on
    void seek(size_t pc);                       //Moves the pointer, clamped to size()

    const std::vector<ScriptCommand> & get_commands() const;
    size_t size() const;
    size_t get_pc() const;                      //Index of the next command to run

    //---------------------- Interactive (external) mode -------------------//
    // Interactive mode changes two things: commands may be added while the
    // engine is running (through submit(), the buffer editors above keep their
    // guards), and reaching the end of the buffer parks the engine instead of
    // finishing it. Everything else - the vocabulary, the threading, the
    // buffer - is exactly what a script gets
    void   set_interactive(bool on);
    bool   is_interactive() const;

    //Appends a command and makes sure the engine will run it. Returns the
    //index it was stored at, which is what completion is reported against
    size_t submit(const ScriptCommand &c);

    //Number of commands that have completed, including their asynchronous
    //part: a command with index i is done when get_done_pc() > i
    size_t get_done_pc() const;

    int    get_state() const;                   //One of State, for diagnostics
    static const char * state_name(int state);

    //Abandons whatever the current command is still waiting for and parks the
    //engine at the end of the buffer. The keys held down are released
    void   interrupt();

    //The engine does not own the sink and never deletes it. Pass nullptr to
    //detach; the log file, if there is one, is written either way
    void   set_sink(ScriptSink * sink);

    //---------------------------- Control ---------------------------------//
    void start(uint64_t clock_now);             //From the beginning; reports parse errors to the log
    void resume(uint64_t clock_now);            //From the current pointer, after pause() or stop()
    void pause();                               //Keeps the position; releases the keys held by the script
    void stop();                                //Keeps the pointer; releases the keys held by the script
    void tick(uint64_t clock_counter);

    bool is_active() const;                     //Running or waiting for something
    bool is_paused() const;
    bool is_finished() const;
    bool is_exit_requested() const;
    int  get_exit_code() const;

    uint64_t get_ticks_per_ms() const;

    //Replay position for a progress display: the duration of the commands
    //before the point where the last start() / resume() happened plus the
    //emulated time elapsed since. Frozen while paused
    uint64_t get_position_ms(uint64_t clock_now) const;

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
        StatePaused,        //Stopped by the user, can be resumed
        StateFinished,
        StateWaiting        //Interactive: armed, but out of commands
    };

    Emulator *                  e;
    mutable compat_mutex        m_mutex;
    std::string                 m_file_name;
    std::string                 m_path;
    std::string                 m_machine;
    std::vector<ScriptCommand>  m_commands;
    std::vector<std::string>    m_errors;       //Parse errors, reported on start
    std::unique_ptr<ScriptLog>  m_log;
    LogFormat                   m_format;

    std::atomic<int>            m_state;        //One of State
    std::atomic<size_t>         m_pc;
    std::atomic<bool>           m_finished;
    std::atomic<bool>           m_exit_requested;
    int                         m_exit_code;

    std::atomic<bool>           m_interactive;  //Commands may be added while the engine runs
    std::atomic<size_t>         m_done_pc;      //Commands [0, m_done_pc) have fully completed
    std::atomic<ScriptSink*>    m_sink;         //Not owned

    uint64_t                    m_now;          //Last seen clock counter
    std::atomic<uint64_t>       m_resume_at;    //Clock value to continue at
    uint64_t                    m_ticks_per_ms; //Cached, tick() runs per instruction
    std::atomic<uint64_t>       m_poll_at;      //Next moment to re-check a polled state

    //Pause bookkeeping: the waits are stored as remaining ticks, because the
    //clock may be reset by a machine reload before the script is resumed
    int                         m_paused_state;
    uint64_t                    m_pause_remaining;
    uint64_t                    m_pause_wait_remaining;
    bool                        m_resume_pending;

    //Position bookkeeping, see get_position_ms()
    uint64_t                    m_play_base_ms;
    std::atomic<uint64_t>       m_play_from;
    std::atomic<bool>           m_play_from_pending;

    std::vector<unsigned int>   m_keys;         //Key sequence of KEY / TYPE
    std::vector<bool>           m_key_shift;    //Whether Shift is held for that key
    size_t                      m_key_index;
    bool                        m_key_pressed;
    unsigned int                m_key_delay;
    unsigned int                m_key_hold;
    std::vector<unsigned int>   m_held_keys;    //Pressed by KEYDOWN, not yet released

    size_t                      m_wait_pc;      //Index of the active WAITFOR
    uint64_t                    m_wait_deadline;

    uint64_t ms_to_ticks(unsigned int ms) const;
    void     delay_ms(unsigned int ms);
    void     finish();
    void     log_error(const ScriptCommand &c, const std::string &text);

    void     cache_ticks_per_ms();
    void     clear_locked();
    void     refresh_machine();
    void     release_keys();                    //Lets go of everything the script holds down
    bool     active_state(int s) const;

    bool     key_step();                        //true when the sequence is over
    bool     waitfor_step();                    //true when the condition is met or timed out
    void     start_keys(const std::vector<unsigned int> &keys, const std::vector<bool> &shift,
                        unsigned int delay, unsigned int hold);

    ComputerDevice * find_device(const ScriptCommand &c);
    bool     read_field(const ScriptCommand &c, DeviceFieldValue &out);
    void     parse_range(const std::string &params, unsigned int * from, unsigned int * to);

    emulator::Result do_key(const ScriptCommand &c);
    emulator::Result do_keyupdown(const ScriptCommand &c, bool press);
    emulator::Result do_type(const ScriptCommand &c);
    emulator::Result do_logdefs(const ScriptCommand &c);
    emulator::Result do_radix(const ScriptCommand &c);
    emulator::Result do_log(const ScriptCommand &c);
    emulator::Result do_command(const ScriptCommand &c);
    emulator::Result do_screen(const ScriptCommand &c);
    emulator::Result do_waitfor(const ScriptCommand &c);
};
