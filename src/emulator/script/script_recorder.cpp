// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Script recorder, source

#include "emulator/devices/common/keyboard.h"
#include "emulator/script/script_engine.h"
#include "emulator/script/script_parser.h"
#include "emulator/script/script_recorder.h"

ScriptRecorder::ScriptRecorder(ScriptEngine * engine):
      m_engine(engine)
    , m_recording(false)
    , m_cursor_valid(false)
    , m_ticks_per_ms(1000)
    , m_cursor(0)
    , m_has_pending(false)
    , m_pending_code(0)
    , m_pending_native(0)
    , m_pending_at(0)
{
}

bool ScriptRecorder::is_recording() const { return m_recording; }

void ScriptRecorder::begin(uint64_t clock_now, uint64_t ticks_per_ms, const std::string &machine)
{
    m_ticks_per_ms = (ticks_per_ms > 0)?ticks_per_ms:1;

    if (m_engine->size() == 0)
    {
        if (!machine.empty()) {
            ScriptCommand c;
            c.verb = SCRIPT_CMD_MACHINE;
            c.args.push_back(machine);
            push(c);
        }
        m_cursor = clock_now;
        m_cursor_valid = true;
    }
    else if (!m_cursor_valid)
    {
        m_cursor = clock_now;
        m_cursor_valid = true;
    }
    else
    {
        //The time spent not recording is written down at once, so that the
        //next event starts with a fresh delay
        emit_wait(clock_now);
    }

    m_has_pending = false;
    m_held.clear();
    m_recording = true;
}

void ScriptRecorder::end(uint64_t clock_now)
{
    if (!m_recording) return;

    flush_pending(clock_now, true);

    if (!m_held.empty()) {
        emit_wait(clock_now);
        for (size_t i = 0; i < m_held.size(); i++) {
            ScriptCommand c;
            c.verb = SCRIPT_CMD_KEYUP;
            c.args.push_back(m_held[i].name);
            push(c);
        }
        m_held.clear();
    }

    //The recording covers everything the user saw, up to the moment of the stop
    emit_wait(clock_now);
    m_recording = false;
}

void ScriptRecorder::mark(uint64_t clock_now)
{
    m_cursor = clock_now;
    m_cursor_valid = true;
}

void ScriptRecorder::invalidate()
{
    m_cursor_valid = false;
    m_has_pending = false;
    m_held.clear();
}

uint64_t ScriptRecorder::live_ms(uint64_t clock_now) const
{
    return m_recording?ms_since_cursor(clock_now):0;
}

//------------------------------- Events -----------------------------------//

bool ScriptRecorder::same_key(unsigned int code, unsigned int native, unsigned int other_code, unsigned int other_native) const
{
    //The code of a key depends on the modifiers held at the moment, so a
    //press and its release may differ ("@" pressed, "2" released once Shift
    //went up first). The physical key is the reliable pair, when known
    if (native != 0 && other_native != 0) return native == other_native;
    return code == other_code;
}

void ScriptRecorder::key(unsigned int code, unsigned int native, bool press, uint64_t clock_now)
{
    if (!m_recording) return;

    if (press)
    {
        flush_pending(clock_now, false);

        std::string name = key_name(code);
        if (name.empty()) return;            //Not addressable by a script
        name = escape_script_text(name);     //The quote and the backslash keys

        m_has_pending = true;
        m_pending_code = code;
        m_pending_native = native;
        m_pending_at = clock_now;
        m_pending_name = name;
        return;
    }

    if (m_has_pending && same_key(code, native, m_pending_code, m_pending_native))
    {
        //A plain press and release: one KEY line
        unsigned int delay = ms_since_cursor(m_pending_at);
        unsigned int hold = (clock_now > m_pending_at)?static_cast<unsigned int>((clock_now - m_pending_at) / m_ticks_per_ms):0;

        ScriptCommand c;
        c.verb = SCRIPT_CMD_KEY;
        c.args.push_back(std::to_string(delay));
        c.args.push_back(std::to_string(hold));
        c.args.push_back(m_pending_name);
        push(c);
        advance(delay + hold);

        m_has_pending = false;
        return;
    }

    for (size_t i = 0; i < m_held.size(); i++)
    {
        if (!same_key(code, native, m_held[i].code, m_held[i].native)) continue;

        std::string name = m_held[i].name;
        m_held.erase(m_held.begin() + i);

        flush_pending(clock_now, false);
        emit_wait(clock_now);

        ScriptCommand c;
        c.verb = SCRIPT_CMD_KEYUP;
        c.args.push_back(name);
        push(c);
        return;
    }

    //A release of a key pressed before the recording started: nothing to do
}

void ScriptRecorder::command(const std::string &device, const std::string &member, const std::string &params, uint64_t clock_now)
{
    if (!m_recording) return;

    flush_pending(clock_now, false);
    emit_wait(clock_now);

    ScriptCommand c;
    c.verb = SCRIPT_CMD_COMMAND;
    c.device = device;
    c.member = member;
    c.params = params;
    push(c);
}

void ScriptRecorder::verb(unsigned int verb, const std::vector<std::string> &args, uint64_t clock_now)
{
    if (!m_recording) return;

    flush_pending(clock_now, false);
    emit_wait(clock_now);

    ScriptCommand c;
    c.verb = verb;
    c.args = args;
    push(c);
}

//------------------------------- Helpers ----------------------------------//

unsigned int ScriptRecorder::ms_since_cursor(uint64_t clock_now) const
{
    //A clock behind the cursor means the clock was reset or read torn: the
    //event is taken as immediate rather than producing an absurd delay
    if (!m_cursor_valid || clock_now <= m_cursor) return 0;
    return static_cast<unsigned int>((clock_now - m_cursor) / m_ticks_per_ms);
}

void ScriptRecorder::advance(unsigned int ms)
{
    //Whole milliseconds only, so that the rounding remainder is carried over
    //to the next event instead of being lost
    m_cursor += static_cast<uint64_t>(ms) * m_ticks_per_ms;
}

void ScriptRecorder::emit_wait(uint64_t clock_now)
{
    unsigned int ms = ms_since_cursor(clock_now);
    if (ms == 0) return;

    ScriptCommand c;
    c.verb = SCRIPT_CMD_WAIT;
    c.args.push_back(std::to_string(ms));
    push(c);
    advance(ms);
}

void ScriptRecorder::flush_pending(uint64_t clock_now, bool as_release)
{
    if (!m_has_pending) return;
    m_has_pending = false;

    unsigned int delay = ms_since_cursor(m_pending_at);

    if (as_release)
    {
        //The recording ends while the key is down: close it as a plain KEY
        unsigned int hold = (clock_now > m_pending_at)?static_cast<unsigned int>((clock_now - m_pending_at) / m_ticks_per_ms):0;
        ScriptCommand c;
        c.verb = SCRIPT_CMD_KEY;
        c.args.push_back(std::to_string(delay));
        c.args.push_back(std::to_string(hold));
        c.args.push_back(m_pending_name);
        push(c);
        advance(delay + hold);
        return;
    }

    //Something else happened while the key is down: it becomes a KEYDOWN
    if (delay > 0) {
        ScriptCommand w;
        w.verb = SCRIPT_CMD_WAIT;
        w.args.push_back(std::to_string(delay));
        push(w);
        advance(delay);
    }

    ScriptCommand c;
    c.verb = SCRIPT_CMD_KEYDOWN;
    c.args.push_back(m_pending_name);
    push(c);

    Held h;
    h.code = m_pending_code;
    h.native = m_pending_native;
    h.name = m_pending_name;
    m_held.push_back(h);
}

void ScriptRecorder::push(const ScriptCommand &c)
{
    m_engine->append(c);
}
