// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scripting engine, source

#include <stdexcept>

#include "dsk_tools/dsk_tools.h"
// MSVC resolves the "utils.h" inside dsk_tools.h against the includer's directory,
// where it finds emulator/utils.h. Pull in the real one explicitly.
#include "libs/dsk_tools/src/utils.h"

#include "emulator/emulator.h"
#include "emulator/files.h"
#include "emulator/devices/common/keyboard.h"
#include "emulator/script/script_engine.h"
#include "emulator/script/script_parser.h"
#include "emulator/utils.h"

namespace {

    // Resolves the escapes a quoted argument may carry. Only the sequences that
    // are needed to write a quote or a backslash are handled: anything else is
    // left alone so that Windows paths survive unchanged.
    std::string unescape_text(const std::string &s)
    {
        std::string r;
        for (size_t i = 0; i < s.length(); i++)
        {
            if (s[i] == '\\' && i + 1 < s.length() && (s[i+1] == '"' || s[i+1] == '\\'))
                r += s[++i];
            else
                r += s[i];
        }
        return r;
    }

} // namespace

ScriptEngine::ScriptEngine(Emulator * e):
      e(e)
    , m_state(StateIdle)
    , m_pc(0)
    , m_finished(false)
    , m_exit_requested(false)
    , m_exit_code(0)
    , m_interactive(false)
    , m_done_pc(0)
    , m_sink(nullptr)
    , m_now(0)
    , m_resume_at(0)
    , m_ticks_per_ms(1000)
    , m_poll_at(0)
    , m_paused_state(StateRunning)
    , m_pause_remaining(0)
    , m_pause_wait_remaining(0)
    , m_resume_pending(false)
    , m_play_base_ms(0)
    , m_play_from(0)
    , m_play_from_pending(false)
    , m_key_index(0)
    , m_key_pressed(false)
    , m_key_delay(SCRIPT_KEY_DELAY)
    , m_key_hold(SCRIPT_KEY_HOLD)
    , m_wait_pc(0)
    , m_wait_deadline(0)
{
}

ScriptEngine::~ScriptEngine()
{
}

const std::string & ScriptEngine::get_machine() const          { return m_machine; }
const std::string & ScriptEngine::get_path() const             { return m_path; }
const std::vector<std::string> & ScriptEngine::get_errors() const { return m_errors; }
bool ScriptEngine::is_finished() const                         { return m_finished; }
bool ScriptEngine::is_exit_requested() const                   { return m_exit_requested; }
int  ScriptEngine::get_exit_code() const                       { return m_exit_code; }
uint64_t ScriptEngine::get_ticks_per_ms() const                { return m_ticks_per_ms; }
bool ScriptEngine::is_paused() const                           { return m_state == StatePaused; }
bool ScriptEngine::is_active() const                           { return active_state(m_state); }

bool ScriptEngine::active_state(int s) const
{
    //StateWaiting is deliberately not active: the lock free path of tick()
    //returns at once, the buffer editors keep their guards and is_active()
    //reports what the transport controls expect. A parked interactive session
    //therefore costs exactly what a finished script costs
    return s == StateRunning || s == StateDelay || s == StateKeys || s == StateWaitFor || s == StateScreenshot;
}

bool ScriptEngine::is_interactive() const                      { return m_interactive; }
size_t ScriptEngine::get_done_pc() const                       { return m_done_pc; }
int  ScriptEngine::get_state() const                           { return m_state; }

const char * ScriptEngine::state_name(int state)
{
    switch (state)
    {
        case StateIdle:       return "idle";
        case StateRunning:    return "running";
        case StateDelay:      return "delay";
        case StateKeys:       return "keys";
        case StateWaitFor:    return "waitfor";
        case StateScreenshot: return "screenshot";
        case StatePaused:     return "paused";
        case StateFinished:   return "finished";
        case StateWaiting:    return "waiting";
        default:              return "unknown";
    }
}

void ScriptEngine::set_sink(ScriptSink * sink)
{
    m_sink = sink;
}

const std::vector<ScriptCommand> & ScriptEngine::get_commands() const { return m_commands; }
size_t ScriptEngine::size() const                              { return m_commands.size(); }
size_t ScriptEngine::get_pc() const                            { return m_pc; }

emulator::Result ScriptEngine::load(const std::string &file_name)
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return emulator::Result::error(emulator::ErrorCode::ScriptError, "Script is running");

    clear_locked();

    std::vector<ScriptCommand> commands;
    std::vector<std::string> errors;
    emulator::Result res = parse_script_file(file_name, commands, errors);
    if (!res) return res;

    m_commands.swap(commands);
    m_errors.swap(errors);
    m_file_name = file_name;
    m_path = dsk_tools::get_file_path(file_name);
    m_log.reset(new ScriptLog(file_name));
    refresh_machine();

    return emulator::Result::ok();
}

void ScriptEngine::set_file_name(const std::string &file_name)
{
    compat_lock_guard lock(m_mutex);
    m_file_name = file_name;
    m_path = dsk_tools::get_file_path(file_name);
    m_log.reset(new ScriptLog(file_name));
}

void ScriptEngine::refresh_machine()
{
    //MACHINE is only meaningful as the first command: it selects the
    //configuration to load before the emulator starts
    m_machine.clear();
    if (!m_commands.empty() && m_commands[0].verb == SCRIPT_CMD_MACHINE && !m_commands[0].args.empty())
        m_machine = m_commands[0].args[0];
}

//---------------------------- Buffer editing ------------------------------//

void ScriptEngine::clear_locked()
{
    m_commands.clear();
    m_errors.clear();
    m_machine.clear();
    m_file_name.clear();
    m_path.clear();
    m_log.reset();
    m_pc = 0;
    m_done_pc = 0;
    m_state = StateIdle;
    m_finished = false;
    m_exit_requested = false;
    m_exit_code = 0;
    m_play_base_ms = 0;
    m_play_from_pending = false;
    m_resume_pending = false;
    m_held_keys.clear();
    m_key_pressed = false;
}

void ScriptEngine::clear()
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return;
    clear_locked();
}

void ScriptEngine::set_commands(const std::vector<ScriptCommand> &commands)
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return;
    m_commands = commands;
    if (m_pc > m_commands.size()) m_pc = m_commands.size();
    refresh_machine();
}

void ScriptEngine::append(const ScriptCommand &c)
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return;
    m_commands.push_back(c);
    if (m_commands.size() == 1) refresh_machine();
}

void ScriptEngine::truncate(size_t pc)
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return;
    if (pc < m_commands.size()) m_commands.resize(pc);
    if (m_pc > m_commands.size()) m_pc = m_commands.size();
    refresh_machine();
}

void ScriptEngine::seek(size_t pc)
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return;
    m_pc = (pc > m_commands.size())?m_commands.size():pc;
    m_play_base_ms = script_duration_ms(m_commands, 0, m_pc);
    m_play_from_pending = false;
}

//------------------------ Interactive (external) mode ---------------------//

void ScriptEngine::set_interactive(bool on)
{
    compat_lock_guard lock(m_mutex);
    m_interactive = on;
    if (on && m_state == StateFinished && !m_exit_requested)
    {
        //A session that ran out of commands is armed again
        m_state = StateWaiting;
        m_finished = false;
    }
    if (!on && m_state == StateWaiting) finish();
}

size_t ScriptEngine::submit(const ScriptCommand &c)
{
    compat_lock_guard lock(m_mutex);
    const size_t index = m_commands.size();
    //push_back may reallocate, and tick() holds a reference into m_commands
    //while a command runs. Both sides are inside this mutex, so no reference
    //is ever alive across the reallocation
    m_commands.push_back(c);
    if (m_commands.size() == 1) refresh_machine();
    //The engine parks at the end of the buffer, so it has to be nudged. Any
    //other state already has a command pending and will get here by itself
    if (m_state == StateWaiting) m_state = StateRunning;
    return index;
}

void ScriptEngine::interrupt()
{
    compat_lock_guard lock(m_mutex);
    if (!active_state(m_state) && m_state != StateWaiting) return;
    release_keys();
    //Everything still queued is dropped as well: a batch that was abandoned
    //half way has already been reported as failed, and running its tail later,
    //out of context, would be worse than not running it at all
    m_pc = m_commands.size();
    m_done_pc = m_pc.load();
    if (m_interactive) {
        m_state = StateWaiting;
    } else {
        m_state = StateFinished;
        m_finished = true;
    }
}

void ScriptEngine::log(const std::string &s)
{
    if (m_log) m_log->write(s);
    ScriptSink * sink = m_sink.load();
    if (sink) sink->write(s, m_pc.load());
}

void ScriptEngine::log_error(const ScriptCommand &c, const std::string &text)
{
    //Runtime errors never stop a script: they are recorded and execution goes on
    log("ERROR line " + std::to_string(c.line) + ": " + strip_message_context(text));
}

//------------------------------- Control ----------------------------------//

void ScriptEngine::cache_ticks_per_ms()
{
    CPU * cpu = dynamic_cast<CPU*>(e->dm->get_device_by_name("cpu", false));
    uint64_t freq = (cpu != nullptr && cpu->clock > 0)?cpu->clock:1000000;
    m_ticks_per_ms = freq / 1000;
    if (m_ticks_per_ms == 0) m_ticks_per_ms = 1;
}

void ScriptEngine::start(uint64_t clock_now)
{
    compat_lock_guard lock(m_mutex);

    cache_ticks_per_ms();
    release_keys();

    m_now = clock_now;
    m_poll_at = 0;
    m_resume_at = 0;
    m_pc = 0;
    m_done_pc = 0;
    m_finished = false;
    m_exit_requested = false;
    m_exit_code = 0;
    m_resume_pending = false;
    m_play_base_ms = 0;
    m_play_from_pending = true;
    m_state = StateRunning;

    for (size_t i = 0; i < m_errors.size(); i++)
        log("ERROR " + strip_message_context(m_errors[i]));
}

void ScriptEngine::resume(uint64_t clock_now)
{
    compat_lock_guard lock(m_mutex);
    if (active_state(m_state)) return;

    cache_ticks_per_ms();
    m_now = clock_now;
    m_finished = false;
    m_exit_requested = false;

    int next = StateRunning;
    if (m_state == StatePaused)
    {
        //The waits are restored relative to the clock of the first tick,
        //see tick(); here it is only decided which state to return to
        next = m_paused_state;
        if (next == StateKeys && m_key_index >= m_keys.size()) next = StateRunning;
        m_resume_pending = (next != StateRunning);
    }
    else
    {
        //After stop() or at the very beginning. An interactive buffer is a
        //queue, not a recording: rewinding it would replay the whole session
        if (!m_interactive && m_pc >= m_commands.size()) m_pc = 0;
        m_play_base_ms = script_duration_ms(m_commands, 0, m_pc);
        m_resume_pending = false;
    }

    //Zero wake up moments make the lock free path of tick() fall through to
    //the locked one, where the pending waits are re-armed
    m_poll_at = 0;
    m_resume_at = 0;
    m_play_from_pending = true;
    m_state = next;
}

void ScriptEngine::pause()
{
    compat_lock_guard lock(m_mutex);
    int s = m_state;
    if (!active_state(s)) return;

    //A key held by a KEY sequence is released and the sequence continues
    //from the next key on resume
    release_keys();

    m_paused_state = s;
    m_pause_remaining = (s == StateKeys)
        ? ms_to_ticks(m_key_delay)
        : ((m_resume_at > m_now)?(m_resume_at - m_now):0);
    m_pause_wait_remaining = (m_wait_deadline > m_now)?(m_wait_deadline - m_now):0;

    //Freeze the position display
    if (!m_play_from_pending && m_now > m_play_from)
        m_play_base_ms += (m_now - m_play_from) / m_ticks_per_ms;
    m_play_from_pending = false;

    //A driver blocked on the command that is being paused must not hang
    m_done_pc = m_pc.load();

    m_state = StatePaused;
}

void ScriptEngine::stop()
{
    compat_lock_guard lock(m_mutex);
    release_keys();
    if (!m_play_from_pending && active_state(m_state) && m_now > m_play_from)
        m_play_base_ms += (m_now - m_play_from) / m_ticks_per_ms;
    m_play_from_pending = false;
    m_done_pc = m_pc.load();
    m_state = StateFinished;
    m_finished = true;
}

void ScriptEngine::finish()
{
    release_keys();
    m_done_pc = m_pc.load();
    m_state = StateFinished;
    m_finished = true;
}

void ScriptEngine::release_keys()
{
    if (m_key_pressed && m_key_index < m_keys.size()) {
        e->key_event(static_cast<int>(m_keys[m_key_index]), 0, false);
        if (m_key_shift[m_key_index]) e->key_event(static_cast<int>(EmuKey::Shift), 0, false);
        m_key_pressed = false;
        m_key_index++;
    }
    for (size_t i = 0; i < m_held_keys.size(); i++)
        e->key_event(static_cast<int>(m_held_keys[i]), 0, false);
    m_held_keys.clear();
}

uint64_t ScriptEngine::get_position_ms(uint64_t clock_now) const
{
    uint64_t base = m_play_base_ms;
    if (m_play_from_pending || !active_state(m_state)) return base;
    uint64_t from = m_play_from;
    if (clock_now <= from) return base;
    return base + (clock_now - from) / (m_ticks_per_ms?m_ticks_per_ms:1);
}

uint64_t ScriptEngine::ms_to_ticks(unsigned int ms) const
{
    //m_ticks_per_ms is cached in start(): tick() is called for every executed
    //instruction, so a device lookup here would be far too expensive
    return static_cast<uint64_t>(ms) * m_ticks_per_ms;
}

void ScriptEngine::delay_ms(unsigned int ms)
{
    m_resume_at = m_now + ms_to_ticks(ms);
    m_state = StateDelay;
}

void ScriptEngine::tick(uint64_t clock_counter)
{
    //Lock free part: this is where nearly every call ends
    int s = m_state.load(std::memory_order_relaxed);
    if (!active_state(s)) return;
    if ((s == StateDelay || s == StateKeys) && clock_counter < m_resume_at.load(std::memory_order_relaxed)) return;
    if ((s == StateWaitFor || s == StateScreenshot) && clock_counter < m_poll_at.load(std::memory_order_relaxed)) return;

    compat_lock_guard lock(m_mutex);
    if (!active_state(m_state)) return;       //Paused or stopped meanwhile

    if (m_play_from_pending) {
        m_play_from = clock_counter;
        m_play_from_pending = false;
    }
    if (m_resume_pending) {
        //The waits interrupted by pause() continue from this moment
        m_resume_at = clock_counter + m_pause_remaining;
        m_wait_deadline = clock_counter + m_pause_wait_remaining;
        m_poll_at = clock_counter;
        m_resume_pending = false;
    }

    m_now = clock_counter;

    for (;;)
    {
        //StateRunning is only reached when the previous command is over,
        //asynchronous part included, so everything before m_pc has finished
        if (m_state == StateRunning) m_done_pc = m_pc.load();

        switch (m_state)
        {
            case StateDelay:
                if (m_now < m_resume_at) return;
                m_state = StateRunning;
                break;

            case StateKeys:
                if (m_now < m_resume_at) return;
                if (!key_step()) return;
                m_state = StateRunning;
                break;

            //The two polled states read a device field or take a mutex, which is
            //far too costly to do for every instruction, so they are sampled
            //once per emulated millisecond
            case StateWaitFor:
                if (m_now < m_poll_at) return;
                m_poll_at = m_now + m_ticks_per_ms;
                if (!waitfor_step()) return;
                m_state = StateRunning;
                break;

            case StateScreenshot:
                if (m_now < m_poll_at) return;
                m_poll_at = m_now + m_ticks_per_ms;
                if (e->is_screenshot_pending()) return;
                m_state = StateRunning;
                break;

            case StateRunning:
            {
                if (m_pc >= m_commands.size())
                {
                    //Interactive: park and wait for the driver to submit more.
                    //The keys held by KEYDOWN stay down on purpose - the pause
                    //between two submitted commands is not the end of anything
                    if (m_interactive) { m_state = StateWaiting; return; }
                    finish();
                    return;
                }
                const ScriptCommand &c = m_commands[m_pc++];
                emulator::Result res = execute(c);
                if (!res) log_error(c, res.message);
                break;
            }

            default:
                return;
        }
    }
}

//--------------------------- Command dispatch -----------------------------//

emulator::Result ScriptEngine::execute(const ScriptCommand &c)
{
    try {
        switch (c.verb)
        {
            case SCRIPT_CMD_NONE:
                return emulator::Result::ok();

            case SCRIPT_CMD_MACHINE:
                //Handled before the emulator starts, see Emulator::load_script()
                return emulator::Result::ok();

            case SCRIPT_CMD_WAIT:
                if (c.args.empty())
                    return emulator::Result::error(emulator::ErrorCode::BadParameters, "WAIT expects a delay");
                delay_ms(parse_numeric_value(c.args[0]));
                return emulator::Result::ok();

            case SCRIPT_CMD_KEY:
                return do_key(c);

            case SCRIPT_CMD_KEYDOWN:
                return do_keyupdown(c, true);

            case SCRIPT_CMD_KEYUP:
                return do_keyupdown(c, false);

            case SCRIPT_CMD_TYPE:
                return do_type(c);

            case SCRIPT_CMD_SCREEN:
                return do_screen(c);

            case SCRIPT_CMD_LOGDEFS:
                return do_logdefs(c);

            case SCRIPT_CMD_RADIX:
                return do_radix(c);

            case SCRIPT_CMD_LOG:
                return do_log(c);

            case SCRIPT_CMD_COMMAND:
                return do_command(c);

            case SCRIPT_CMD_WAITFOR:
                return do_waitfor(c);

            case SCRIPT_CMD_RESET:
                e->reset(c.args.empty() || str_tolower(c.args[0]) != "soft");
                return emulator::Result::ok();

            case SCRIPT_CMD_LOAD:
            {
                if (c.args.empty() || c.args[0].empty())
                    return emulator::Result::error(emulator::ErrorCode::BadParameters, "LOAD expects a file name");
                std::string file = find_file_location(e->get_system_data(), c.args[0]);
                if (file.empty()) file = c.args[0];
                return HandleExternalFile(e, file);
            }

            case SCRIPT_CMD_PRINT:
                log(c.args.empty()?"":unescape_text(c.args[0]));
                return emulator::Result::ok();

            case SCRIPT_CMD_LOGFILE:
                if (!c.args.empty() && m_log) m_log->set_name(c.args[0]);
                return emulator::Result::ok();

            case SCRIPT_CMD_EXIT:
                m_exit_code = (c.args.empty() || c.args[0].empty())?0:static_cast<int>(parse_numeric_value(c.args[0]));
                m_exit_requested = true;
                finish();
                return emulator::Result::ok();

            default:
                return emulator::Result::error(emulator::ErrorCode::ScriptError, "Unsupported command");
        }
    }
    catch (const std::exception &ex) {
        //parse_numeric_value() and friends throw on malformed values
        return emulator::Result::error(emulator::ErrorCode::BadParameters, ex.what());
    }
}

//------------------------------ Keyboard ----------------------------------//

void ScriptEngine::start_keys(const std::vector<unsigned int> &keys, const std::vector<bool> &shift,
                              unsigned int delay, unsigned int hold)
{
    m_keys = keys;
    m_key_shift = shift;
    m_key_shift.resize(m_keys.size(), false);
    m_key_index = 0;
    m_key_pressed = false;
    m_key_delay = delay;
    m_key_hold = hold;

    if (m_keys.empty()) return;

    m_resume_at = m_now + ms_to_ticks(m_key_delay);
    m_state = StateKeys;
}

bool ScriptEngine::key_step()
{
    if (m_key_index >= m_keys.size()) return true;

    if (!m_key_pressed) {
        //The keyboard devices watch the Shift key itself, not a modifier flag,
        //so it is pressed and released around the character that needs it
        if (m_key_shift[m_key_index]) e->key_event(static_cast<int>(EmuKey::Shift), 0, true);
        e->key_event(static_cast<int>(m_keys[m_key_index]), 0, true);
        m_key_pressed = true;
        m_resume_at = m_now + ms_to_ticks(m_key_hold);
        return false;
    }

    e->key_event(static_cast<int>(m_keys[m_key_index]), 0, false);
    if (m_key_shift[m_key_index]) e->key_event(static_cast<int>(EmuKey::Shift), 0, false);
    m_key_pressed = false;
    m_key_index++;

    if (m_key_index >= m_keys.size()) return true;

    m_resume_at = m_now + ms_to_ticks(m_key_delay);
    return false;
}

emulator::Result ScriptEngine::do_key(const ScriptCommand &c)
{
    //KEY n1, n2, key1, key2, ...
    if (c.args.size() < 3)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "KEY expects two delays and at least one key");

    unsigned int delay = parse_numeric_value(c.args[0]);
    unsigned int hold  = parse_numeric_value(c.args[1]);

    std::vector<unsigned int> keys;
    std::vector<bool> shift;
    for (size_t i = 2; i < c.args.size(); i++)
    {
        if (c.args[i].empty()) continue;

        //A key can be prefixed with "shift+" to be typed with Shift held.
        //The plus itself is a key name, so only a separator inside the name
        //counts as the prefix.
        std::string name = c.args[i];
        bool with_shift = false;
        const size_t plus = name.find('+');
        if (plus != std::string::npos && plus > 0 && plus + 1 < name.length()) {
            if (str_tolower(name.substr(0, plus)) == "shift") {
                with_shift = true;
                name = name.substr(plus + 1);
            }
        }

        unsigned int code = translate_key_name(unescape_text(name));
        if (code == _FFFF) {
            log_error(c, "unknown key '" + c.args[i] + "'");
            continue;
        }
        keys.push_back(code);
        shift.push_back(with_shift);
    }

    start_keys(keys, shift, delay, hold);
    return emulator::Result::ok();
}

emulator::Result ScriptEngine::do_keyupdown(const ScriptCommand &c, bool press)
{
    //KEYDOWN key / KEYUP key: a single key without a delay, so that chords
    //and long holds can be expressed. Whatever is still held when the script
    //stops is released by release_keys()
    if (c.args.empty() || c.args[0].empty())
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            std::string(press?"KEYDOWN":"KEYUP") + " expects a key name");

    unsigned int code = translate_key_name(unescape_text(c.args[0]));
    if (code == _FFFF) {
        log_error(c, "unknown key '" + c.args[0] + "'");
        return emulator::Result::ok();
    }

    e->key_event(static_cast<int>(code), 0, press);

    if (press) {
        m_held_keys.push_back(code);
    } else {
        for (size_t i = 0; i < m_held_keys.size(); i++)
            if (m_held_keys[i] == code) { m_held_keys.erase(m_held_keys.begin() + i); break; }
    }
    return emulator::Result::ok();
}

emulator::Result ScriptEngine::do_type(const ScriptCommand &c)
{
    //TYPE "text" [, n1, n2]
    if (c.args.empty())
        return emulator::Result::error(emulator::ErrorCode::BadParameters, "TYPE expects a text");

    unsigned int delay = (c.args.size() > 1 && !c.args[1].empty())?parse_numeric_value(c.args[1]):SCRIPT_KEY_DELAY;
    unsigned int hold  = (c.args.size() > 2 && !c.args[2].empty())?parse_numeric_value(c.args[2]):SCRIPT_KEY_HOLD;

    const std::string &text = c.args[0];
    std::vector<unsigned int> keys;
    std::vector<bool> shift;

    //Some characters live on the machine keyboard only together with Shift.
    //The keyboard itself knows which ones, so TYPE asks it instead of guessing
    //by the host layout.
    Keyboard * kbd = dynamic_cast<Keyboard*>(e->dm->get_device_by_name("keyboard", false));

    for (size_t i = 0; i < text.length(); i++)
    {
        char ch = text[i];
        std::string name;

        if (ch == '\\' && i + 1 < text.length())
        {
            //Escapes, so that a line feed can be typed from a single line script
            char n = text[++i];
            if      (n == 'n') name = "ret";
            else if (n == 't') name = "tab";
            else name = std::string(1, n);
        }
        else if (ch == ' ') name = "space";
        else name = std::string(1, ch);

        unsigned int code = translate_key_name(name);
        if (code == _FFFF) {
            log_error(c, "unknown character '" + name + "'");
            continue;
        }
        keys.push_back(code);
        shift.push_back(kbd != nullptr && kbd->needs_shift(code));
    }

    start_keys(keys, shift, delay, hold);
    return emulator::Result::ok();
}

//------------------------------- Devices ----------------------------------//

ComputerDevice * ScriptEngine::find_device(const ScriptCommand &c)
{
    //required=false, otherwise the lookup throws
    return e->dm->get_device_by_name(c.device, false);
}

void ScriptEngine::parse_range(const std::string &params, unsigned int * from, unsigned int * to)
{
    *from = 0;
    *to = 0;

    std::vector<std::string> p = split_params(params);
    if (p.empty() || p[0].empty()) return;

    *from = parse_numeric_value(p[0]);
    *to = (p.size() > 1 && !p[1].empty())?parse_numeric_value(p[1]):*from;
}

bool ScriptEngine::read_field(const ScriptCommand &c, DeviceFieldValue &out)
{
    ComputerDevice * d = find_device(c);
    if (d == nullptr) return false;

    unsigned int from, to;
    parse_range(c.params, &from, &to);
    return d->get_field(c.member, from, to, out);
}

emulator::Result ScriptEngine::do_logdefs(const ScriptCommand &c)
{
    if (c.args.size() < 2)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "LOGDEFS expects a width and a base");

    unsigned int width = parse_numeric_value(c.args[0]);
    unsigned int base = parse_numeric_value(c.args[1]);

    if (width != 8 && width != 16)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "LOGDEFS: width must be 8 or 16");
    if (base != 2 && base != 8 && base != 10 && base != 16)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "LOGDEFS: base must be 2, 8, 10 or 16");

    m_format.width = width;
    m_format.base = base;
    return emulator::Result::ok();
}

//The base of every number the script writes without a prefix. A machine brings
//its own with MACHINE, so this is an override; '_' marks a decimal value
emulator::Result ScriptEngine::do_radix(const ScriptCommand &c)
{
    if (c.args.empty() || c.args[0].empty())
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "RADIX expects a base");

    //Read in decimal: the base itself cannot be written in the base it sets
    unsigned int base = parse_numeric_value(c.args[0], 10);
    if (base != 2 && base != 8 && base != 10 && base != 16)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "RADIX: base must be 2, 8, 10 or 16");

    SystemData * sd = e->get_system_data();
    if (sd != nullptr) sd->radix = base;
    set_default_radix(base);
    return emulator::Result::ok();
}

emulator::Result ScriptEngine::do_log(const ScriptCommand &c)
{
    ComputerDevice * d = find_device(c);
    if (d == nullptr)
        return emulator::Result::error(emulator::ErrorCode::DeviceNotFound,
            "device '" + c.device + "' is not found");

    unsigned int from, to;
    parse_range(c.params, &from, &to);

    log(d->log_device(c.member, std::make_pair(from, to), m_format));
    return emulator::Result::ok();
}

emulator::Result ScriptEngine::do_command(const ScriptCommand &c)
{
    ComputerDevice * d = find_device(c);
    if (d == nullptr)
        return emulator::Result::error(emulator::ErrorCode::DeviceNotFound,
            "device '" + c.device + "' is not found");

    return d->send_command(c.member, c.params);
}

//----------------------------- Screenshots --------------------------------//

emulator::Result ScriptEngine::do_screen(const ScriptCommand &c)
{
    std::string name = (c.args.empty() || c.args[0].empty())
        ? ("screen-" + timestamp_string() + ".png")
        : c.args[0];

    //"-" asks for the image without a file: an external driver reads the bytes
    //back with Emulator::take_screenshot_png()
    if (name == "-")
        name.clear();
    else
    //Relative names are stored next to the script
    if (!is_absolute_path(name)) name = m_path + name;

    //The image is grabbed by the render thread, so the engine waits for it:
    //an EXIT right after SCREEN would otherwise lose the file
    e->request_screenshot(name);
    m_poll_at = m_now + m_ticks_per_ms;
    m_state = StateScreenshot;
    return emulator::Result::ok();
}

//------------------------------- WAITFOR ----------------------------------//

emulator::Result ScriptEngine::do_waitfor(const ScriptCommand &c)
{
    if (c.args.size() < 2)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "WAITFOR expects a comparison");

    if (find_device(c) == nullptr)
        return emulator::Result::error(emulator::ErrorCode::DeviceNotFound,
            "device '" + c.device + "' is not found");

    unsigned int timeout = (c.args.size() > 2 && !c.args[2].empty())
        ? parse_numeric_value(c.args[2])
        : SCRIPT_WAITFOR_TIMEOUT;

    m_wait_pc = m_pc - 1;
    m_wait_deadline = m_now + ms_to_ticks(timeout);
    m_poll_at = m_now;          //Evaluate the condition right away
    m_state = StateWaitFor;
    return emulator::Result::ok();
}

bool ScriptEngine::waitfor_step()
{
    const ScriptCommand &c = m_commands[m_wait_pc];

    DeviceFieldValue v;
    if (!read_field(c, v) || !v.numeric || v.values.empty()) {
        log_error(c, "WAITFOR: field '" + c.member + "' is not a number");
        return true;
    }

    //The operator is stored as a code by the parser, not written by a human
    unsigned int op = parse_numeric_value(c.args[0], 10);
    unsigned int expected = parse_numeric_value(c.args[1]);
    unsigned int actual = v.values[0];

    bool matched = false;
    switch (op)
    {
        case SCRIPT_OP_EQ: matched = (actual == expected); break;
        case SCRIPT_OP_NE: matched = (actual != expected); break;
        case SCRIPT_OP_LT: matched = (actual <  expected); break;
        case SCRIPT_OP_LE: matched = (actual <= expected); break;
        case SCRIPT_OP_GT: matched = (actual >  expected); break;
        case SCRIPT_OP_GE: matched = (actual >= expected); break;
        default: break;
    }

    if (matched) return true;

    if (m_now >= m_wait_deadline) {
        log_error(c, "WAITFOR timed out, " + c.device + "." + c.member + " = " +
                     format_number(actual, m_format.base, m_format.width));
        return true;
    }

    return false;
}
