// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scripting engine parser, source

#include <cstdint>
#include <sstream>

#include "dsk_tools/dsk_tools.h"

#include "emulator/script/script_parser.h"
#include "emulator/utils.h"

namespace {

    struct VerbDescription {
        unsigned int    verb;
        const char *    name;
        bool            addressed;      //Uses the device.member(params) syntax
    };

    const VerbDescription VERBS[] = {
        {SCRIPT_CMD_MACHINE, "machine", false},
        {SCRIPT_CMD_WAIT,    "wait",    false},
        {SCRIPT_CMD_KEY,     "key",     false},
        {SCRIPT_CMD_SCREEN,  "screen",  false},
        {SCRIPT_CMD_LOGDEFS, "logdefs", false},
        {SCRIPT_CMD_LOG,     "log",     true },
        {SCRIPT_CMD_COMMAND, "command", true },
        {SCRIPT_CMD_EXIT,    "exit",    false},
        {SCRIPT_CMD_RESET,   "reset",   false},
        {SCRIPT_CMD_TYPE,    "type",    false},
        {SCRIPT_CMD_LOAD,    "load",    false},
        {SCRIPT_CMD_PRINT,   "print",   false},
        {SCRIPT_CMD_LOGFILE, "logfile", false},
        {SCRIPT_CMD_WAITFOR, "waitfor", true },
        {SCRIPT_CMD_KEYDOWN, "keydown", false},
        {SCRIPT_CMD_KEYUP,   "keyup",   false}
    };

    const unsigned int VERBS_COUNT = sizeof(VERBS) / sizeof(VERBS[0]);

    const VerbDescription * find_verb(const std::string &name)
    {
        std::string n = str_tolower(name);
        for (unsigned int i = 0; i < VERBS_COUNT; i++)
            if (n == VERBS[i].name) return &VERBS[i];
        return nullptr;
    }

    emulator::Result parse_error(unsigned int line_no, const std::string &text)
    {
        return emulator::Result::error(emulator::ErrorCode::ScriptError,
            "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Line")) + "} " +
            std::to_string(line_no) + ": " + text);
    }

    // Cuts off a comment, honouring quoted strings so that a file name
    // containing '#' or '//' is not damaged
    std::string strip_comment(const std::string &s)
    {
        bool in_quotes = false;
        for (size_t i = 0; i < s.length(); i++)
        {
            char c = s[i];
            if (c == '\\' && in_quotes && i + 1 < s.length() && s[i+1] == '"') i++;
            else if (c == '"') in_quotes = !in_quotes;
            else if (!in_quotes) {
                if (c == '#') return s.substr(0, i);
                if (c == '/' && i + 1 < s.length() && s[i+1] == '/') return s.substr(0, i);
            }
        }
        return s;
    }

} // namespace

std::string script_verb_name(unsigned int verb)
{
    for (unsigned int i = 0; i < VERBS_COUNT; i++)
        if (VERBS[i].verb == verb) return str_toupper(VERBS[i].name);
    return "?";
}

emulator::Result parse_script_line(const std::string &line, unsigned int line_no, ScriptCommand &out)
{
    out = ScriptCommand();
    out.line = line_no;

    std::string s = str_trim(strip_comment(line));
    if (s.empty()) return emulator::Result::ok();

    //The verb is separated from the rest by whitespace
    size_t space = s.find_first_of(" \t");
    std::string verb_name = (space == std::string::npos)?s:s.substr(0, space);
    std::string rest = (space == std::string::npos)?"":str_trim(s.substr(space + 1));

    const VerbDescription * v = find_verb(verb_name);
    if (v == nullptr)
        return parse_error(line_no, "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Unknown command")) + "} '" + verb_name + "'");

    out.verb = v->verb;

    if (!v->addressed) {
        out.args = split_params(rest);
        return emulator::Result::ok();
    }

    //device.member(params) - the bracketed part is optional.
    //WAITFOR additionally carries a comparison after the brackets.
    std::string addressed = rest;
    std::string tail;

    size_t open = addressed.find('(');
    if (open != std::string::npos) {
        size_t close = addressed.rfind(')');
        if (close == std::string::npos || close < open)
            return parse_error(line_no, "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Unbalanced brackets")) + "}");
        out.params = str_trim(addressed.substr(open + 1, close - open - 1));
        tail = str_trim(addressed.substr(close + 1));
        addressed = str_trim(addressed.substr(0, open));
    } else if (v->verb == SCRIPT_CMD_WAITFOR) {
        //No brackets: the comparison starts at the first operator character
        size_t op = addressed.find_first_of("=!<>");
        if (op != std::string::npos) {
            tail = str_trim(addressed.substr(op));
            addressed = str_trim(addressed.substr(0, op));
        }
    }

    size_t dot = addressed.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= addressed.length())
        return parse_error(line_no, "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Expected device.name")) + ", " +
            std::string(QT_TRANSLATE_NOOP("Script", "found")) + " '" + addressed + "'");

    out.device = str_trim(addressed.substr(0, dot));
    out.member = str_trim(addressed.substr(dot + 1));

    if (v->verb == SCRIPT_CMD_WAITFOR)
    {
        //tail is "<op> <value> [, timeout]"
        if (tail.empty())
            return parse_error(line_no, "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "WAITFOR expects a comparison")) + "}");

        unsigned int op;
        size_t skip;
        if      (tail.compare(0, 2, "==") == 0) { op = SCRIPT_OP_EQ; skip = 2; }
        else if (tail.compare(0, 2, "!=") == 0) { op = SCRIPT_OP_NE; skip = 2; }
        else if (tail.compare(0, 2, "<>") == 0) { op = SCRIPT_OP_NE; skip = 2; }
        else if (tail.compare(0, 2, "<=") == 0) { op = SCRIPT_OP_LE; skip = 2; }
        else if (tail.compare(0, 2, ">=") == 0) { op = SCRIPT_OP_GE; skip = 2; }
        else if (tail[0] == '=')                { op = SCRIPT_OP_EQ; skip = 1; }
        else if (tail[0] == '<')                { op = SCRIPT_OP_LT; skip = 1; }
        else if (tail[0] == '>')                { op = SCRIPT_OP_GT; skip = 1; }
        else
            return parse_error(line_no, "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Unknown comparison operator")) + "}");

        std::vector<std::string> p = split_params(str_trim(tail.substr(skip)));
        if (p.empty() || p[0].empty())
            return parse_error(line_no, "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "WAITFOR expects a value")) + "}");

        out.args.push_back(std::to_string(op));
        out.args.push_back(p[0]);                                   //Expected value
        out.args.push_back((p.size() > 1)?p[1]:std::string(""));    //Timeout, ms
    }

    return emulator::Result::ok();
}

emulator::Result parse_script_file(const std::string &file_name,
                                   std::vector<ScriptCommand> &out,
                                   std::vector<std::string> &errors)
{
    out.clear();
    errors.clear();

    if (!dsk_tools::file_exists(file_name))
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Script file is not found")) + "} " + file_name);

    std::string content = dsk_tools::utf8_read_file(file_name);

    std::istringstream stream(content);
    std::string line;
    unsigned int line_no = 0;

    while (std::getline(stream, line))
    {
        line_no++;
        ScriptCommand c;
        emulator::Result res = parse_script_line(line, line_no, c);
        if (!res)
            errors.push_back(res.message);
        else if (c.verb != SCRIPT_CMD_NONE)
            out.push_back(c);
    }

    return emulator::Result::ok();
}

//------------------------------- Writing ----------------------------------//

std::string escape_script_text(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.length(); i++)
    {
        if (s[i] == '"' || s[i] == '\\') r += '\\';
        r += s[i];
    }
    return r;
}

std::string format_script_arg(const std::string &s)
{
    bool safe = !s.empty();
    for (size_t i = 0; i < s.length() && safe; i++)
    {
        char c = s[i];
        safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_' || c == '+' || c == '.' || c == '-';
    }
    return safe?s:("\"" + s + "\"");
}

std::string format_script_command(const ScriptCommand &c)
{
    if (c.verb == SCRIPT_CMD_NONE) return "";

    std::string r = script_verb_name(c.verb);

    if (c.verb == SCRIPT_CMD_LOG || c.verb == SCRIPT_CMD_COMMAND || c.verb == SCRIPT_CMD_WAITFOR)
    {
        r += " " + c.device + "." + c.member;
        if (!c.params.empty()) r += "(" + c.params + ")";

        if (c.verb == SCRIPT_CMD_WAITFOR && c.args.size() >= 2)
        {
            static const char * OPS[] = {"==", "!=", "<", "<=", ">", ">="};
            unsigned int op = SCRIPT_OP_EQ;
            try { op = parse_numeric_value(c.args[0]); } catch (...) {}
            if (op > SCRIPT_OP_GE) op = SCRIPT_OP_EQ;
            r += std::string(" ") + OPS[op] + " " + c.args[1];
            if (c.args.size() > 2 && !c.args[2].empty()) r += ", " + c.args[2];
        }
        return r;
    }

    for (size_t i = 0; i < c.args.size(); i++)
        r += (i == 0 ? " " : ",") + format_script_arg(c.args[i]);

    return r;
}

emulator::Result write_script_file(const std::string &file_name,
                                   const std::vector<ScriptCommand> &commands)
{
    dsk_tools::UTF8_ofstream f(file_name, std::ios::binary);
    if (!f.is_open())
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{Script|" + std::string(QT_TRANSLATE_NOOP("Script", "Unable to write the script file")) + "} " + file_name);

    //The MinGW flavour of UTF8_ofstream has write() only
    for (size_t i = 0; i < commands.size(); i++)
    {
        std::string line = format_script_command(commands[i]) + "\n";
        f.write(line.data(), static_cast<std::streamsize>(line.size()));
    }

    return emulator::Result::ok();
}

//------------------------------ Durations ---------------------------------//

namespace {

    unsigned int arg_number(const ScriptCommand &c, size_t index, unsigned int def)
    {
        if (index >= c.args.size() || c.args[index].empty()) return def;
        try { return parse_numeric_value(c.args[index]); } catch (...) { return def; }
    }

} // namespace

unsigned int script_command_duration_ms(const ScriptCommand &c)
{
    switch (c.verb)
    {
        case SCRIPT_CMD_WAIT:
            return arg_number(c, 0, 0);

        case SCRIPT_CMD_KEY:
        {
            if (c.args.size() < 3) return 0;
            unsigned int per_key = arg_number(c, 0, 0) + arg_number(c, 1, 0);
            unsigned int keys = 0;
            for (size_t i = 2; i < c.args.size(); i++)
                if (!c.args[i].empty()) keys++;
            return per_key * keys;
        }

        case SCRIPT_CMD_TYPE:
        {
            if (c.args.empty()) return 0;
            unsigned int per_key = arg_number(c, 1, SCRIPT_KEY_DELAY) + arg_number(c, 2, SCRIPT_KEY_HOLD);
            //An escape pair types a single key, see ScriptEngine::do_type()
            unsigned int keys = 0;
            const std::string &text = c.args[0];
            for (size_t i = 0; i < text.length(); i++) {
                if (text[i] == '\\' && i + 1 < text.length()) i++;
                keys++;
            }
            return per_key * keys;
        }

        default:
            return 0;
    }
}

uint64_t script_duration_ms(const std::vector<ScriptCommand> &commands, size_t from, size_t to)
{
    uint64_t total = 0;
    if (to > commands.size()) to = commands.size();
    for (size_t i = from; i < to; i++)
        total += script_command_duration_ms(commands[i]);
    return total;
}
