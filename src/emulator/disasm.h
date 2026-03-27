// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Universal disassembler class, header

#pragma once

#include <string>

#include "emulator/result.h"

struct InstructionByte {
    bool is_instr;
    uint8_t value;
};

struct Instruction {
    InstructionByte bytes[16];
    unsigned int length;
    std::string text;
};

typedef uint8_t (*CommandBytes)[15];

class DisAsm
{
private:
    Instruction ins[2048];
    unsigned int count;

public:
    unsigned int max_command_length;

    DisAsm();

    emulator::Result load_file(const std::string &file_name);
    unsigned int disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string * output);

};

std::string bytes_dump(CommandBytes bytes, unsigned int length);