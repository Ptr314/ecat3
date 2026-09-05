// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MCP server: JSON-RPC 2.0 over stdin/stdout, source

#include <iostream>
#include <vector>

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
#endif

#include "libs/picojson/picojson.h"
#include "mcp/mcp_base64.h"
#include "mcp/mcp_server.h"
#include "mcp/mcp_session.h"

namespace {

    //The revision of the protocol this server was written against. A client
    //that asks for another one gets its own version echoed back, which is what
    //the specification asks for as long as the shape of the messages matches
    const char * DEFAULT_PROTOCOL = "2025-06-18";

    const unsigned int DEFAULT_TIMEOUT_MS = 15000;

    picojson::value str(const std::string &s) { return picojson::value(s); }

    picojson::value obj(const picojson::object &o) { return picojson::value(o); }

    //Reads an object member, returning a null value when it is absent
    picojson::value member(const picojson::value &v, const char * name)
    {
        if (!v.is<picojson::object>()) return picojson::value();
        const picojson::object &o = v.get<picojson::object>();
        picojson::object::const_iterator it = o.find(name);
        if (it == o.end()) return picojson::value();
        return it->second;
    }

    std::string member_string(const picojson::value &v, const char * name)
    {
        picojson::value m = member(v, name);
        return m.is<std::string>() ? m.get<std::string>() : std::string();
    }

    unsigned int member_uint(const picojson::value &v, const char * name, unsigned int def)
    {
        picojson::value m = member(v, name);
        if (!m.is<double>()) return def;
        const double d = m.get<double>();
        if (d < 0) return def;
        return static_cast<unsigned int>(d);
    }

    picojson::value text_content(const std::string &text)
    {
        picojson::object c;
        c["type"] = str("text");
        c["text"] = str(text);
        return obj(c);
    }

    picojson::value image_content(const std::string &base64, const std::string &mime)
    {
        picojson::object c;
        c["type"]     = str("image");
        c["data"]     = str(base64);
        c["mimeType"] = str(mime);
        return obj(c);
    }

    //A tool definition: name, what it is for, and its JSON Schema
    picojson::value tool(const std::string &name, const std::string &description,
                         const picojson::object &properties, const picojson::array &required)
    {
        picojson::object schema;
        schema["type"] = str("object");
        schema["properties"] = obj(properties);
        if (!required.empty()) schema["required"] = picojson::value(required);

        picojson::object t;
        t["name"]        = str(name);
        t["description"] = str(description);
        t["inputSchema"] = obj(schema);
        return obj(t);
    }

    picojson::value property(const std::string &type, const std::string &description)
    {
        picojson::object p;
        p["type"]        = str(type);
        p["description"] = str(description);
        return obj(p);
    }

    const char * RUN_DESCRIPTION =
        "Runs one or more .ecat script commands against the running machine, in order, and returns "
        "everything they printed. This is the main tool: the machine is driven with the same "
        "vocabulary a .ecat script uses.\n"
        "Commands (one per line, arguments comma separated, numbers decimal or $hex or #binary):\n"
        "  WAIT ms                        - delay, counted in EMULATED time\n"
        "  KEY delay,hold,key1,key2,...   - press keys in turn (names: a..z, 0..9, ret, esc, space,\n"
        "                                   tab, back, del, up, down, left, right, f1..f12, shift+x)\n"
        "  KEYDOWN key / KEYUP key        - hold and release, for chords\n"
        "  TYPE \"text\"[,delay,hold]       - type text; \\n is Return, \\t is Tab\n"
        "  LOG dev.field[(from[,to])]     - print a device property, e.g. LOG cpu.registers,\n"
        "                                   LOG mapper.map, LOG ram0.value($100,$110)\n"
        "  LOGDEFS width,base             - width 8 or 16, base 2/8/10/16 for the LOG output\n"
        "  COMMAND dev.cmd(params)        - perform a device action, e.g. COMMAND cpu.step(),\n"
        "                                   COMMAND cpu.setreg(PC,$100), COMMAND fdd0.load(\"a.dsk\")\n"
        "  WAITFOR dev.field op value[,timeout] - block until a property matches (== != < <= > >=)\n"
        "  RESET [cold|soft]              - reset the machine\n"
        "  LOAD \"file\"                    - load a file the way the File/Open menu does\n"
        "  MACHINE \"dir/file.cfg\"         - switch to another machine\n"
        "  PRINT \"text\"                   - put a line into the output\n"
        "Single stepping needs no special tool: COMMAND cpu.stop(), COMMAND cpu.step(), "
        "LOG cpu.pc, LOG cpu.registers, COMMAND cpu.breakpoint($100), COMMAND cpu.run().\n"
        "Use ecat_devices for the device names of the loaded machine and ecat_describe for the "
        "fields and commands of one device rather than guessing them.\n"
        "Delays are emulated time: if the CPU is halted the clock stops and a WAIT never finishes, "
        "which is reported as a stall rather than a silent hang.";

} // namespace

namespace mcp {

McpServer::McpServer(McpSession * session, const std::string &version, bool trace):
      m_session(session)
    , m_version(version)
    , m_trace(trace)
    , m_finished(false)
{
}

void McpServer::trace(const char * direction, const std::string &json)
{
    if (!m_trace) return;
    //stdout belongs to the protocol, so the human readable trace goes to stderr
    std::cerr << direction << " " << json << std::endl;
}

void McpServer::send(const std::string &json)
{
    compat_lock_guard lock(m_out_mutex);
    trace("<--", json);
    std::cout << json << "\n";
    //Pipes are fully buffered: without this the client waits forever
    std::cout.flush();
}

void McpServer::run()
{
#ifdef _WIN32
    //Text mode would turn every LF into CRLF on the way out and invalidate
    //any byte count the client keeps
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!line.empty() && line[line.length()-1] == '\r') line.erase(line.length()-1);
        if (line.empty()) continue;
        handle_line(line);
    }

    //End of file: the client is gone
    m_finished = true;
}

void McpServer::handle_line(const std::string &line)
{
    trace("-->", line);

    picojson::value request;
    const std::string err = picojson::parse(request, line);

    picojson::object response;
    response["jsonrpc"] = str("2.0");

    if (!err.empty() || !request.is<picojson::object>())
    {
        picojson::object e;
        e["code"]    = picojson::value(static_cast<double>(-32700));
        e["message"] = str("Parse error: " + (err.empty() ? std::string("not an object") : err));
        response["id"]    = picojson::value();
        response["error"] = obj(e);
        send(obj(response).serialize());
        return;
    }

    const std::string method = member_string(request, "method");
    const picojson::value id = member(request, "id");
    const picojson::value params = member(request, "params");

    //A request without an id is a notification: it is acted upon but never
    //answered, and answering one is a protocol error
    const bool notification = id.is<picojson::null>();

    picojson::object result;
    bool have_result = true;
    int  error_code  = 0;
    std::string error_message;

    if (method == "initialize")
    {
        const std::string asked = member_string(params, "protocolVersion");
        picojson::object caps;
        caps["tools"] = obj(picojson::object());

        picojson::object info;
        info["name"]    = str("ecat3");
        info["version"] = str(m_version);

        result["protocolVersion"] = str(asked.empty() ? std::string(DEFAULT_PROTOCOL) : asked);
        result["capabilities"]    = obj(caps);
        result["serverInfo"]      = obj(info);
    }
    else
    if (method == "ping")
    {
        //Empty result
    }
    else
    if (method.compare(0, 14, "notifications/") == 0)
    {
        have_result = false;
    }
    else
    if (method == "tools/list")
    {
        picojson::array tools;

        {
            picojson::object props;
            props["config"] = property("string",
                "Configuration file, as found under deploy/computers, for example "
                "\"bk/BK-0011M.cfg\" or \"irisha/Irisha-kngmd.cfg\".");
            picojson::array req;
            req.push_back(str("config"));
            tools.push_back(tool("ecat_machine",
                "Loads a machine and starts it. Call this first; the emulator window, if this build "
                "has one, appears on the first call. Returns the device list of the loaded machine.",
                props, req));
        }
        {
            picojson::object props;
            props["commands"]   = property("string", "One or more .ecat commands, one per line.");
            props["timeout_ms"] = property("integer",
                "How long to wait for each command, in real milliseconds. Default 15000.");
            picojson::array req;
            req.push_back(str("commands"));
            tools.push_back(tool("ecat_run", RUN_DESCRIPTION, props, req));
        }
        {
            picojson::object props;
            props["wait_ms"] = property("integer",
                "Emulated milliseconds to wait before grabbing the screen, to let it settle. Default 0.");
            tools.push_back(tool("ecat_screenshot",
                "Captures the emulator screen and returns it as a PNG image.",
                props, picojson::array()));
        }
        {
            tools.push_back(tool("ecat_status",
                "Reports the machine, the script engine state and whether the emulated clock is "
                "advancing. A clock that does not advance means the CPU is halted and any WAIT "
                "will hang, so check this when a command stalls.",
                picojson::object(), picojson::array()));
        }
        {
            tools.push_back(tool("ecat_devices",
                "Lists the devices of the loaded machine: name, type and class. The names are what "
                "LOG and COMMAND address.",
                picojson::object(), picojson::array()));
        }
        {
            picojson::object props;
            props["device"] = property("string", "Device name, as reported by ecat_devices.");
            picojson::array req;
            req.push_back(str("device"));
            tools.push_back(tool("ecat_describe",
                "Lists the readable fields and the commands of one device, so that field names "
                "never have to be guessed.",
                props, req));
        }
        {
            tools.push_back(tool("ecat_cancel",
                "Abandons whatever the machine is still waiting for and parks the session, for a "
                "WAIT or WAITFOR that can no longer finish.",
                picojson::object(), picojson::array()));
        }

        result["tools"] = picojson::value(tools);
    }
    else
    if (method == "tools/call")
    {
        const std::string name = member_string(params, "name");
        const picojson::value args = member(params, "arguments");

        picojson::array content;
        bool is_error = false;

        if (m_session == nullptr)
        {
            content.push_back(text_content("The emulator session is not available."));
            is_error = true;
        }
        else
        if (name == "ecat_machine")
        {
            McpResult r = m_session->machine(member_string(args, "config"));
            content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        if (name == "ecat_run")
        {
            McpResult r = m_session->run(member_string(args, "commands"),
                                         member_uint(args, "timeout_ms", DEFAULT_TIMEOUT_MS));
            content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        if (name == "ecat_screenshot")
        {
            std::vector<unsigned char> png;
            McpResult r = m_session->screenshot(member_uint(args, "wait_ms", 0),
                                                DEFAULT_TIMEOUT_MS, png);
            if (r.ok)
                content.push_back(image_content(base64_encode(png), "image/png"));
            else
                content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        if (name == "ecat_status")
        {
            McpResult r = m_session->status();
            content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        if (name == "ecat_devices")
        {
            McpResult r = m_session->devices();
            content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        if (name == "ecat_describe")
        {
            McpResult r = m_session->describe(member_string(args, "device"));
            content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        if (name == "ecat_cancel")
        {
            McpResult r = m_session->cancel();
            content.push_back(text_content(r.text));
            is_error = !r.ok;
        }
        else
        {
            content.push_back(text_content("There is no tool named \"" + name + "\"."));
            is_error = true;
        }

        result["content"] = picojson::value(content);
        //A failed tool is a normal result with a flag, not a protocol error:
        //the model is meant to read it and try something else
        result["isError"] = picojson::value(is_error);
    }
    else
    {
        have_result   = false;
        error_code    = -32601;
        error_message = "Method not found: " + method;
    }

    if (notification) return;

    response["id"] = id;
    if (error_code != 0)
    {
        picojson::object e;
        e["code"]    = picojson::value(static_cast<double>(error_code));
        e["message"] = str(error_message);
        response["error"] = obj(e);
    }
    else
    if (have_result)
        response["result"] = obj(result);
    else
        response["result"] = obj(picojson::object());

    send(obj(response).serialize());
}

} // namespace mcp
