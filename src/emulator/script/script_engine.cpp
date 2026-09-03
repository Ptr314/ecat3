// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scripting engine, source

#include <stdexcept>

#include "dsk_tools/dsk_tools.h"
#include "../libs/dsk_tools/src/utils.h"

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
    , m_now(0)
    , m_resume_at(0)
    , m_ticks_per_ms(1000)
    , m_poll_at(0)
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

const std::string & ScriptEngine::get_machine() const { return m_machine; }
const std::string & ScriptEngine::get_path() const    { return m_path; }
bool ScriptEngine::is_finished() const                { return m_finished; }
bool ScriptEngine::is_exit_requested() const          { return m_exit_requested; }
int  ScriptEngine::get_exit_code() const              { return m_exit_code; }

emulator::Result ScriptEngine::load(const std::string &file_name)
{
    m_file_name = file_name;
    m_path = dsk_tools::get_file_path(file_name);

    emulator::Result res = parse_script_file(file_name, m_commands, m_errors);
    if (!res) return res;

    m_log.reset(new ScriptLog(file_name));

    //MACHINE is only meaningful as the first command: it selects the
    //configuration to load before the emulator starts
    if (!m_commands.empty() && m_commands[0].verb == SCRIPT_CMD_MACHINE && !m_commands[0].args.empty())
        m_machine = m_commands[0].args[0];

    return emulator::Result::ok();
}

void ScriptEngine::log(const std::string &s)
{
    if (m_log) m_log->write(s);
}

void ScriptEngine::log_error(const ScriptCommand &c, const std::string &text)
{
    //Runtime errors never stop a script: they are recorded and execution goes on
    log("ERROR line " + std::to_string(c.line) + ": " + strip_message_context(text));
}

void ScriptEngine::start(uint64_t clock_now)
{
    CPU * cpu = dynamic_cast<CPU*>(e->dm->get_device_by_name("cpu", false));
    uint64_t freq = (cpu != nullptr && cpu->clock > 0)?cpu->clock:1000000;
    m_ticks_per_ms = freq / 1000;
    if (m_ticks_per_ms == 0) m_ticks_per_ms = 1;

    m_now = clock_now;
    m_poll_at = clock_now;
    m_pc = 0;
    m_state = StateRunning;
    m_finished = false;
    m_exit_requested = false;

    for (size_t i = 0; i < m_errors.size(); i++)
        log("ERROR " + strip_message_context(m_errors[i]));
}

void ScriptEngine::stop()
{
    m_state = StateFinished;
    m_finished = true;
}

void ScriptEngine::finish()
{
    m_state = StateFinished;
    m_finished = true;
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
    if (m_state == StateIdle || m_state == StateFinished) return;

    m_now = clock_counter;

    for (;;)
    {
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
                if (m_pc >= m_commands.size()) { finish(); return; }
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

            case SCRIPT_CMD_TYPE:
                return do_type(c);

            case SCRIPT_CMD_SCREEN:
                return do_screen(c);

            case SCRIPT_CMD_LOGDEFS:
                return do_logdefs(c);

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

void ScriptEngine::start_keys(const std::vector<unsigned int> &keys, unsigned int delay, unsigned int hold)
{
    m_keys = keys;
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
        e->key_event(static_cast<int>(m_keys[m_key_index]), 0, true);
        m_key_pressed = true;
        m_resume_at = m_now + ms_to_ticks(m_key_hold);
        return false;
    }

    e->key_event(static_cast<int>(m_keys[m_key_index]), 0, false);
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
    for (size_t i = 2; i < c.args.size(); i++)
    {
        if (c.args[i].empty()) continue;
        unsigned int code = translate_key_name(c.args[i]);
        if (code == _FFFF) {
            log_error(c, "unknown key '" + c.args[i] + "'");
            continue;
        }
        keys.push_back(code);
    }

    start_keys(keys, delay, hold);
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
    }

    start_keys(keys, delay, hold);
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

    unsigned int op = parse_numeric_value(c.args[0]);
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
