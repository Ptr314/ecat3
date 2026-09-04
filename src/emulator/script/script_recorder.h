// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Script recorder, header

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/script/script_types.h"

class ScriptEngine;

// Turns the actions of the user into script commands and appends them to the
// buffer of a ScriptEngine, so that a session can be saved as an .ecat file
// and replayed. Qt-free; every method is called on the interface thread.
//
// Time is emulated time, the same clock the engine uses for WAIT, so a
// recording replays at the speed of the machine, not of the host. The
// recorder keeps a cursor: the clock value the end of the buffer corresponds
// to. The gap between the cursor and the next event becomes a WAIT.
//
// Keys. A press followed by the release of the same key, with nothing in
// between, is written as a single KEY line (delay, hold, name). Anything that
// overlaps - a modifier held while other keys are typed, a chord in a game -
// is written as KEYDOWN / KEYUP with WAITs between them.
class ScriptRecorder
{
public:
    explicit ScriptRecorder(ScriptEngine * engine);

    //Starts or continues recording. An empty buffer receives MACHINE first.
    //A valid cursor is kept, so the time spent not recording is added as a
    //WAIT before the next event; an invalid one is set to clock_now
    void begin(uint64_t clock_now, uint64_t ticks_per_ms, const std::string &machine);

    //Pauses or stops: writes out the pending key, releases the held ones and
    //adds a WAIT up to this moment. The cursor stays valid
    void end(uint64_t clock_now);

    bool is_recording() const;

    //The end of the buffer happened at clock_now (a replay stopped there)
    void mark(uint64_t clock_now);

    //The buffer no longer relates to the clock (a file was opened, the buffer
    //was cleared, the machine was reloaded)
    void invalidate();

    //Events. Ignored unless recording
    void key(unsigned int code, unsigned int native, bool press, uint64_t clock_now);
    void command(const std::string &device, const std::string &member, const std::string &params, uint64_t clock_now);
    void verb(unsigned int verb, const std::vector<std::string> &args, uint64_t clock_now);

    //Emulated time since the cursor that is not in the buffer yet, ms
    uint64_t live_ms(uint64_t clock_now) const;

private:
    struct Held {
        unsigned int    code;
        unsigned int    native;
        std::string     name;
    };

    ScriptEngine *      m_engine;
    bool                m_recording;
    bool                m_cursor_valid;
    uint64_t            m_ticks_per_ms;
    uint64_t            m_cursor;

    bool                m_has_pending;
    unsigned int        m_pending_code;
    unsigned int        m_pending_native;
    uint64_t            m_pending_at;
    std::string         m_pending_name;

    std::vector<Held>   m_held;

    unsigned int ms_since_cursor(uint64_t clock_now) const;
    void advance(unsigned int ms);
    void emit_wait(uint64_t clock_now);
    void flush_pending(uint64_t clock_now, bool as_release);
    void push(const ScriptCommand &c);
    bool same_key(unsigned int code, unsigned int native, unsigned int other_code, unsigned int other_native) const;
};
