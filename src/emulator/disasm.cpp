// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Universal disassembler class, source

#include <fstream>
#include <iostream>
#include <sstream>

#include "dsk_tools/core.h"
#include "emulator/utils.h"
#include "disasm.h"

static std::string str_replace(const std::string &s, const std::string &from, const std::string &to)
{
    std::string result = s;
    size_t pos = result.find(from);
    if (pos != std::string::npos)
        result.replace(pos, from.length(), to);
    return result;
}

DisAsm::DisAsm()
    : max_command_length(0)
{}

//The flow marker of a table line: J, JC (conditional), C, CC, R, RC, JI and CI
//(through a register or a cell, no target), with @hex for a target the
//instruction implies (RST 38H). Any column after the length may hold it: the
//6502 tables have the addressing mode there
static bool parse_flow(const std::string &s, Instruction &in)
{
    std::string m = s;
    std::string at;
    const size_t p = m.find('@');
    if (p != std::string::npos) { at = m.substr(p + 1); m = m.substr(0, p); }
    if (m.empty() || m.size() > 2) return false;
    DisasmFlow flow;
    switch (m[0]) {
    case 'J': flow = FLOW_JUMP; break;
    case 'C': flow = FLOW_CALL; break;
    case 'R': flow = FLOW_RET; break;
    default: return false;
    }
    bool conditional = false, indirect = false;
    if (m.size() == 2) {
        if (m[1] == 'C') conditional = true;
        else if (m[1] == 'I') indirect = true;
        else return false;
    }
    if (!at.empty()) {
        try {
            in.target = parse_numeric_value("$" + at);
            in.fixed_target = true;
        } catch (std::exception &) {
            return false;
        }
    }
    in.flow = flow;
    in.conditional = conditional;
    in.indirect = indirect;
    return true;
}

emulator::Result DisAsm::load_file(const std::string &file_name)
{
    const std::string hex_digits = "0123456789ABCDEF";
    const std::string letters = "abcdefghijklmnopqrstuvwxyz";

    std::string content = dsk_tools::utf8_read_file(file_name);
    if (content.empty()) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DisAsm|" + std::string(QT_TRANSLATE_NOOP("DisAsm", "Error reading CPU instructions file")) + "} " + file_name);
    }

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line))
    {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::vector<std::string> parts = split_string(line, '\x09', true);
        if (parts.size() < 3) continue;

        std::vector<std::string> codes = split_string(parts[0], ' ', true);

        Instruction in;
        in.length = 0;

        //bytes[] holds 16, CommandBytes 15
        for (size_t i = 0; i < codes.size() && in.length < 15; i++)
        {
            const std::string &code = codes[i];
            if (code.length() == 2 && hex_digits.find(code[0]) != std::string::npos)
            {
                //HEX byte
                in.bytes[in.length].is_instr = true;
                in.bytes[in.length].value = parse_numeric_value("$" + code);
                in.length++;
            } else
            if (code.length() == 1 && letters.find(code[0]) != std::string::npos)
            {
                //data byte
                in.bytes[in.length].is_instr = false;
                in.bytes[in.length].value = static_cast<uint8_t>(code[0]);
                in.length++;
            }
        }

        if ( in.length != parse_numeric_value(parts[2]) )
        {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{DisAsm|" + std::string(QT_TRANSLATE_NOOP("DisAsm", "CPU instruction length is incorrect")) + "} " + parts[1]);
        }

        if (in.length > max_command_length) max_command_length = in.length;

        in.text = parts[1];
        for (size_t i = 3; i < parts.size(); i++)
            if (parse_flow(str_trim(parts[i]), in)) break;

        ins.push_back(in);
    }

    return emulator::Result::ok();
}

unsigned int DisAsm::disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string *output)
{
    DisasmLine l;
    const unsigned int n = decode(bytes, PC, max_len, &l);
    *output = l.text;
    return n;
}

unsigned int DisAsm::decode(CommandBytes bytes, unsigned int PC, unsigned int max_len, DisasmLine * out)
{
    *out = DisasmLine();

    for (size_t i = 0; i < ins.size(); i++)
    {
        const Instruction &in = ins[i];
        if (in.length > max_len) continue; //Skip too long instructions

        unsigned int j;
        for (j = 0; j < in.length; j++)
            if (in.bytes[j].is_instr && (in.bytes[j].value != (*bytes)[j])) break;
        if (j != in.length) continue;

        //The operands as the bytes give them: nn the little-endian word, n
        //and d a byte, e the displacement of a relative jump (counted from
        //the end of the instruction, wherever in it the byte is)
        unsigned int nn = 0, n = 0, d = 0, nn_count = 0;
        int8_t e = 0;
        bool has_e = false;
        for (j = 0; j < in.length; j++) {
            if (in.bytes[j].is_instr) continue;
            const uint8_t v = (*bytes)[j];
            switch (in.bytes[j].value) {
            case 'n': nn += (unsigned int)v << (nn_count++ * 8); n = v; break;
            case 'd': d = v; break;
            case 'e': e = static_cast<int8_t>(v); has_e = true; break;
            default: break;
            }
        }
        const unsigned int rel = (PC + e + in.length) & 0xFFFF;

        std::string s = in.text;
        s = str_replace(s, "$nn", hex_str(nn, 4));
        s = str_replace(s, "$n", hex_str(n, 2));
        s = str_replace(s, "$d", hex_str(d, 2));
        s = str_replace(s, "PC+$e", hex_str(rel, 4));

        out->length = in.length;
        out->text = s;
        //Codes the tables mark with a leading * (the undocumented ones of the
        //8080 and the 6502) are not trusted by the backward disassembly; the
        //Z80 table marks its own with a trailing * and they stay valid
        out->valid = !s.empty() && s[0] != '*' && s[0] != '?';
        out->flow = in.flow;
        out->conditional = in.conditional;
        if ((in.flow == FLOW_JUMP || in.flow == FLOW_CALL) && !in.indirect) {
            if (in.fixed_target) {
                out->has_target = true;
                out->target = in.target;
            } else if (has_e) {
                out->has_target = true;
                out->target = rel;
            } else if (nn_count == 2) {
                out->has_target = true;
                out->target = nn;
            }
        }
        return in.length;
    }
    return 0;
}

std::string bytes_dump(CommandBytes bytes, unsigned int length)
{
    std::string s;
    for (unsigned int i = 0; i < length; i++)
        s += hex_str((*bytes)[i], 2) + " ";
    return s;
}
