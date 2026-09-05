// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MCP session: drives the emulator, knows nothing about transport

#include <sstream>

#include "emulator/emulator.h"
#include "emulator/script/script_parser.h"
#include "emulator/utils.h"
#include "mcp/mcp_session.h"

namespace {

    //A command that is still waiting while the emulated clock has not moved
    //for this long is not slow, it is stuck: the CPU is halted or the machine
    //never started, and no amount of further waiting will help
    const uint64_t STALL_MS = 250;

    //A runaway LOG in a loop must not eat the memory of a long session
    const size_t MAX_CAPTURED_LINES = 10000;

    std::string trim(const std::string &s)
    {
        size_t b = 0;
        size_t e = s.length();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
        while (e > b && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\r')) e--;
        return s.substr(b, e - b);
    }

    std::string join(const std::vector<std::string> &lines)
    {
        std::string s;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (!s.empty()) s += "\n";
            s += lines[i];
        }
        return s;
    }

} // namespace

namespace mcp {

//------------------------------ CaptureSink -------------------------------//

CaptureSink::CaptureSink()
{
}

void CaptureSink::write(const std::string &line, size_t pc)
{
    compat_lock_guard lock(m_mutex);
    if (m_lines.size() >= MAX_CAPTURED_LINES)
        m_lines.erase(m_lines.begin());
    m_lines.push_back(std::make_pair(pc, line));
}

std::vector<std::string> CaptureSink::drain(size_t pc)
{
    compat_lock_guard lock(m_mutex);
    std::vector<std::string> out;
    std::vector<std::pair<size_t, std::string> > rest;
    for (size_t i = 0; i < m_lines.size(); i++)
    {
        if (m_lines[i].first == pc)
            out.push_back(m_lines[i].second);
        else
        if (m_lines[i].first < pc)
            //Left over by an earlier request that timed out and kept running.
            //Reporting it is the diagnostic; dropping it hides the problem
            out.push_back("[from an earlier command] " + m_lines[i].second);
        else
            rest.push_back(m_lines[i]);
    }
    m_lines.swap(rest);
    return out;
}

std::vector<std::string> CaptureSink::drain_all()
{
    compat_lock_guard lock(m_mutex);
    std::vector<std::string> out;
    for (size_t i = 0; i < m_lines.size(); i++) out.push_back(m_lines[i].second);
    m_lines.clear();
    return out;
}

//------------------------------- McpSession -------------------------------//

McpSession::McpSession(Emulator * e, McpHost * host):
      e(e)
    , m_host(host)
    , m_armed(false)
    , m_last_status_clock(0)
    , m_last_status_ms(0)
{
}

McpSession::~McpSession()
{
    if (e != nullptr)
    {
        ScriptEngine * s = e->script_engine();
        if (s != nullptr) s->set_sink(nullptr);
    }
}

void McpSession::arm()
{
    compat_lock_guard lock(m_mutex);
    arm_locked();
}

void McpSession::arm_locked()
{
    m_armed = false;
    if (e == nullptr || !e->loaded) return;

    ScriptEngine * s = e->script_engine();
    s->set_sink(&m_sink);
    //A machine reload stops the engine and restarts the clock from zero, so
    //the queue is started afresh rather than resumed
    s->stop();
    s->clear();
    s->set_sink(&m_sink);           //clear() drops the log, not the sink, but be explicit
    s->set_interactive(true);
    s->start(e->clock_now());
    m_sink.drain_all();
    m_armed = true;
}

McpSession::WaitResult McpSession::wait_done(size_t index, unsigned int timeout_ms)
{
    ScriptEngine * s = e->script_engine();

    const uint64_t started  = compat_now_ms();
    const uint64_t deadline = started + timeout_ms;

    uint64_t last_clock  = e->clock_now();
    uint64_t last_change = started;

    for (;;)
    {
        if (s->get_done_pc() > index) return WaitDone;
        if (s->is_finished() || s->is_paused()) return WaitAborted;

        const uint64_t now   = compat_now_ms();
        const uint64_t clock = e->clock_now();
        if (clock != last_clock)
        {
            last_clock  = clock;
            last_change = now;
        }

        //Emulated time is what the delays are counted in, so a frozen clock
        //means the command can never complete, no matter how long we wait
        if (now - last_change >= STALL_MS) return WaitStalled;
        if (now >= deadline) return WaitTimeout;

        compat_sleep_ms(1);
    }
}

McpResult McpSession::run(const std::string &commands, unsigned int timeout_ms)
{
    compat_lock_guard lock(m_mutex);
    return run_locked(commands, timeout_ms);
}

McpResult McpSession::run_locked(const std::string &commands, unsigned int timeout_ms)
{
    if (!m_armed)
        return McpResult::bad("No machine is loaded. Call ecat_machine first.");

    //Everything is parsed before anything runs: a batch with a typo half way
    //down must not leave the machine part way through it
    std::vector<std::string> lines;
    std::vector<ScriptCommand> parsed;

    size_t start = 0;
    unsigned int line_no = 0;
    while (start <= commands.length())
    {
        const size_t nl = commands.find('\n', start);
        const std::string raw = (nl == std::string::npos)
            ? commands.substr(start)
            : commands.substr(start, nl - start);
        start = (nl == std::string::npos) ? commands.length() + 1 : nl + 1;
        line_no++;

        const std::string line = trim(raw);
        if (line.empty()) continue;

        ScriptCommand c;
        emulator::Result res = parse_script_line(line, line_no, c);
        //The parser already names the line in its message
        if (!res)
            return McpResult::bad(strip_message_context(res.message) + "\n  " + line);

        lines.push_back(line);
        parsed.push_back(c);
    }

    if (parsed.empty()) return McpResult::good("ok");

    //MACHINE throws away the device manager, the engine state and the queue,
    //so a batch that contains one is run in pieces around it
    for (size_t i = 0; i < parsed.size(); i++)
    {
        if (parsed[i].verb != SCRIPT_CMD_MACHINE) continue;

        std::string head;
        for (size_t j = 0; j < i; j++) head += lines[j] + "\n";
        std::string tail;
        for (size_t j = i + 1; j < parsed.size(); j++) tail += lines[j] + "\n";

        std::vector<std::string> output;
        if (!head.empty())
        {
            McpResult r = run_locked(head, timeout_ms);
            if (!r.ok) return r;
            if (r.text != "ok") output.push_back(r.text);
        }

        McpResult r = machine_locked(parsed[i].args.empty() ? std::string() : parsed[i].args[0]);
        if (!r.ok) return r;
        output.push_back(r.text);

        if (!tail.empty())
        {
            McpResult t = run_locked(tail, timeout_ms);
            if (!t.ok)
            {
                output.push_back(t.text);
                return McpResult::bad(join(output));
            }
            if (t.text != "ok") output.push_back(t.text);
        }
        return McpResult::good(join(output));
    }

    ScriptEngine * s = e->script_engine();

    //The whole batch is submitted before anything is awaited, and that is what
    //makes it behave like the same lines in a script: tick() runs the instant
    //commands of one batch without leaving its loop, so they all see the same
    //clock. Waiting on each command in turn would let the machine run on
    //between them, and two consecutive LOGs would not be one snapshot
    const size_t first = s->submit(parsed[0]);
    size_t last = first;
    for (size_t i = 1; i < parsed.size(); i++) last = s->submit(parsed[i]);

    const WaitResult w = wait_done(last, timeout_ms);

    //Output is collected per command, in order, whatever the outcome was
    std::vector<std::string> output;
    for (size_t i = 0; i < parsed.size(); i++)
    {
        std::vector<std::string> got = m_sink.drain(first + i + 1);
        for (size_t j = 0; j < got.size(); j++) output.push_back(got[j]);
    }

    if (w != WaitDone)
    {
        //Name the command the batch is actually stuck on, not the last one
        const size_t done = s->get_done_pc();
        const size_t stuck = (done > first && (done - first) < parsed.size()) ? (done - first) : 0;
        const std::string &line = lines[stuck];
        const std::string state = ScriptEngine::state_name(s->get_state());

        if (w == WaitStalled)
        {
            if (done < first)
                //The queue is ordered, so a command abandoned earlier blocks
                //everything behind it. Different problem, different cure
                output.push_back("[blocked] An earlier command is still waiting (engine state: "
                    + state + "), so this batch has not started."
                    " Call ecat_cancel to abandon it.");
            else
                output.push_back("[stalled] The emulated clock is not advancing, so \"" + line
                    + "\" can never complete (engine state: " + state + ")."
                    " The CPU is halted or the machine did not start: COMMAND cpu.run() restarts a"
                    " halted CPU, RESET or ecat_machine restarts the machine.");
        }
        else
        if (w == WaitTimeout)
            output.push_back("[timeout] \"" + line + "\" is still running after "
                + std::to_string(timeout_ms) + " ms. Its output, and that of the commands queued"
                " behind it, will be reported with the next call; use ecat_cancel to abandon it.");
        else
            output.push_back("[stopped] The script engine stopped while running \"" + line + "\".");

        return McpResult::bad(join(output));
    }

    return McpResult::good(output.empty() ? std::string("ok") : join(output));
}

McpResult McpSession::status()
{
    compat_lock_guard lock(m_mutex);

    const uint64_t now   = compat_now_ms();
    const uint64_t clock = (e != nullptr) ? e->clock_now() : 0;

    std::ostringstream ss;
    ss << "machine: " << (m_host ? m_host->current_machine() : std::string()) << "\n";
    ss << "loaded: " << ((e != nullptr && e->loaded) ? "yes" : "no") << "\n";
    ss << "armed: " << (m_armed ? "yes" : "no") << "\n";

    if (m_armed)
    {
        ScriptEngine * s = e->script_engine();
        ss << "state: " << ScriptEngine::state_name(s->get_state()) << "\n";
        ss << "pc: " << s->get_pc() << "\n";
        ss << "done_pc: " << s->get_done_pc() << "\n";
        ss << "commands: " << s->size() << "\n";
    }

    ss << "clock: " << clock << "\n";
    if (m_last_status_ms != 0)
    {
        const uint64_t d_clock = (clock >= m_last_status_clock) ? (clock - m_last_status_clock) : 0;
        const uint64_t d_ms    = (now >= m_last_status_ms) ? (now - m_last_status_ms) : 0;
        //The single most useful number here: a machine whose clock does not
        //move is wedged, and every WAIT submitted to it will hang
        ss << "clock advanced by " << d_clock << " ticks over the last " << d_ms << " ms of real time";
        if (d_clock == 0) ss << " - the CPU looks halted";
        ss << "\n";
    }

    m_last_status_clock = clock;
    m_last_status_ms    = now;

    return McpResult::good(ss.str());
}

McpResult McpSession::devices()
{
    compat_lock_guard lock(m_mutex);
    return devices_locked();
}

McpResult McpSession::devices_locked()
{
    if (e == nullptr || !e->loaded || e->dm == nullptr)
        return McpResult::bad("No machine is loaded. Call ecat_machine first.");

    std::ostringstream ss;
    ss << "name\ttype\tclass\n";
    for (unsigned int i = 0; i < e->dm->device_count; i++)
    {
        DeviceDescription * d = e->dm->get_device(i);
        if (d == nullptr || d->get() == nullptr) continue;
        ss << d->device_name << "\t" << d->device_type << "\t" << d->get()->device_class << "\n";
    }
    return McpResult::good(ss.str());
}

McpResult McpSession::describe(const std::string &device)
{
    compat_lock_guard lock(m_mutex);

    if (e == nullptr || !e->loaded || e->dm == nullptr)
        return McpResult::bad("No machine is loaded. Call ecat_machine first.");

    ComputerDevice * d = e->dm->get_device_by_name(device, false);
    if (d == nullptr)
        return McpResult::bad("There is no device named \"" + device + "\". Use ecat_devices for the list.");

    std::ostringstream ss;
    ss << device << " (" << d->type << ", class " << d->device_class << ")\n";

    const std::vector<DeviceFieldInfo> fields = d->get_device_fields();
    ss << "\nFields, read with LOG " << device << ".<field>";
    ss << " (a ranged one also takes LOG " << device << ".<field>(from,to)):\n";
    for (size_t i = 0; i < fields.size(); i++)
    {
        ss << "  " << fields[i].name;
        if (fields[i].ranged) ss << "(from,to)";
        if (!fields[i].description.empty()) ss << " - " << fields[i].description;
        ss << "\n";
    }

    const std::vector<DeviceCommandInfo> cmds = d->get_device_commands();
    ss << "\nCommands, run with COMMAND " << device << ".<command>(params):\n";
    for (size_t i = 0; i < cmds.size(); i++)
    {
        ss << "  " << cmds[i].name;
        if (!cmds[i].params.empty()) ss << " " << cmds[i].params;
        if (!cmds[i].description.empty()) ss << " - " << cmds[i].description;
        ss << "\n";
    }

    return McpResult::good(ss.str());
}

McpResult McpSession::machine(const std::string &config)
{
    compat_lock_guard lock(m_mutex);
    return machine_locked(config);
}

McpResult McpSession::machine_locked(const std::string &config)
{
    if (m_host == nullptr) return McpResult::bad("This build cannot switch machines.");
    if (config.empty())    return McpResult::bad("A configuration file name is required.");

    //Detach first: load_config() deletes the device manager and stops the
    //engine, and the sink must not be written to while that happens
    if (e != nullptr && e->loaded)
    {
        ScriptEngine * s = e->script_engine();
        s->set_interactive(false);
        s->set_sink(nullptr);
    }
    m_armed = false;

    const std::string err = m_host->load_machine(config);
    if (!err.empty()) return McpResult::bad(strip_message_context(err));

    m_host->show_window();
    arm_locked();
    if (!m_armed) return McpResult::bad("The configuration loaded, but the emulator is not running.");

    m_last_status_clock = 0;
    m_last_status_ms    = 0;

    McpResult list = devices_locked();
    return McpResult::good("Loaded " + m_host->current_machine() + "\n\n"
                           + (list.ok ? list.text : std::string()));
}

McpResult McpSession::cancel()
{
    compat_lock_guard lock(m_mutex);
    if (!m_armed) return McpResult::bad("No machine is loaded.");

    ScriptEngine * s = e->script_engine();
    s->interrupt();

    std::vector<std::string> left = m_sink.drain_all();
    std::string text = "Cancelled.";
    if (!left.empty()) text += "\nOutput left by the abandoned command:\n" + join(left);
    return McpResult::good(text);
}

McpResult McpSession::screenshot(unsigned int wait_ms, unsigned int timeout_ms,
                                 std::vector<unsigned char> &png)
{
    compat_lock_guard lock(m_mutex);

    if (!m_armed) return McpResult::bad("No machine is loaded. Call ecat_machine first.");

    uint64_t serial_before = 0;
    std::vector<unsigned char> previous;
    e->take_screenshot_png(previous, &serial_before);

    std::string script;
    if (wait_ms > 0) script += "WAIT " + std::to_string(wait_ms) + "\n";
    script += "SCREEN \"-\"";

    McpResult r = run_locked(script, timeout_ms);
    if (!r.ok) return r;

    uint64_t serial_after = 0;
    if (!e->take_screenshot_png(png, &serial_after))
        return McpResult::bad("The render thread produced no image.");
    if (serial_after == serial_before)
        return McpResult::bad("The screen was not captured: the image is the one from a previous call.");

    return McpResult::good(r.text);
}

} // namespace mcp
