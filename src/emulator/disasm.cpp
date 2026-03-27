// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Universal disassembler class, source

#include <fstream>
#include <iostream>

#include "dsk_tools/dsk_tools.h"
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
    : count(0),
    max_command_length(0)
{}

emulator::Result DisAsm::load_file(const std::string &file_name)
{
    const std::string hex_digits = "0123456789ABCDEF";
    const std::string letters = "abcdefghijklmnopqrstuvwxyz";

    std::cerr << "Loading " << file_name << std::endl;

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

        ins[count].length = 0;

        for (size_t i = 0; i < codes.size(); i++)
        {
            const std::string &code = codes[i];
            if (code.length() == 2 && hex_digits.find(code[0]) != std::string::npos)
            {
                //HEX byte
                ins[count].bytes[ins[count].length].is_instr = true;
                ins[count].bytes[ins[count].length].value = parse_numeric_value("$" + code);
                ins[count].length++;
            } else
            if (code.length() == 1 && letters.find(code[0]) != std::string::npos)
            {
                //data byte
                ins[count].bytes[ins[count].length].is_instr = false;
                ins[count].bytes[ins[count].length].value = static_cast<uint8_t>(code[0]);
                ins[count].length++;
            }
        }

        if ( ins[count].length != parse_numeric_value(parts[2]) )
        {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{DisAsm|" + std::string(QT_TRANSLATE_NOOP("DisAsm", "CPU instruction length is incorrect")) + "} " + parts[1]);
        }

        if (ins[count].length > max_command_length) max_command_length = ins[count].length;

        ins[count].text = parts[1];

        count++;
    }

    return emulator::Result::ok();
}

unsigned int DisAsm::disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string *output)
{
    unsigned int result = 0;

    for (unsigned int i = 0; i < count; i++)
    {
        if (ins[i].length > max_len) continue; //Skip too long instructions
        else {
            unsigned int j;
            for (j = 0; j < ins[i].length; j++)
                if (ins[i].bytes[j].is_instr && (ins[i].bytes[j].value != (*bytes)[j])) break;

            if (j == ins[i].length)
            {
                std::string s = ins[i].text;
                result = ins[i].length;

                size_t p = ins[i].text.find("$nn");
                if (p != std::string::npos)
                {
                    unsigned int c = 0;
                    unsigned int v = 0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == 'n')
                            v += (*bytes)[j] << (c++*8);
                    s = str_replace(s, "$nn", hex_str(v, 4));
                };

                p = ins[i].text.find("$n");
                if (p != std::string::npos)
                {
                    unsigned int v = 0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == 'n')
                            v = (*bytes)[j];
                    s = str_replace(s, "$n", hex_str(v, 2));
                };

                p = ins[i].text.find("$d");
                if (p != std::string::npos)
                {
                    unsigned int v=0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == 'd')
                            v = (*bytes)[j];
                    s = str_replace(s, "$d", hex_str(v, 2));
                };

                p = ins[i].text.find("PC+$e");
                if (p != std::string::npos)
                {
                    int8_t v=0;
                    for (j = 0; j < ins[i].length; j++)
                        if (!ins[i].bytes[j].is_instr && ins[i].bytes[j].value == 'e')
                            v = static_cast<int8_t>((*bytes)[j]);
                    uint16_t v1=(PC + v + ins[i].length) & 0xFFFF;
                    s = str_replace(s, "PC+$e", hex_str(v1, 4));
                }

                *output = s;
                return result;
            }
        }
    }
    return result;
}

std::string bytes_dump(CommandBytes bytes, unsigned int length)
{
    std::string s;
    for (unsigned int i = 0; i < length; i++)
        s += hex_str((*bytes)[i], 2) + " ";
    return s;
}