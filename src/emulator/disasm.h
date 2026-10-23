// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Universal disassembler class, header

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/result.h"

struct InstructionByte {
    bool is_instr;
    uint8_t value;
};

//What an instruction does to the flow of the program, for the debugger: the
//arrows of the listing, the step over a call, the step out of a routine
enum DisasmFlow {
    FLOW_NONE = 0,      // goes on to the next instruction
    FLOW_JUMP,          // jumps (conditional says whether always)
    FLOW_CALL,          // calls something that returns here: a subroutine, a trap, an interrupt
    FLOW_RET            // returns from one
};

struct Instruction {
    InstructionByte bytes[16];
    unsigned int length;
    std::string text;
    //The flow marker of the table (J, JC, C, CC, R, RC, JI, CI, an @address
    //for a fixed target)
    DisasmFlow flow = FLOW_NONE;
    bool conditional = false;
    bool indirect = false;
    bool fixed_target = false;
    unsigned int target = 0;
};

//One decoded instruction
struct DisasmLine {
    unsigned int length = 0;
    std::string text;
    //False for what is not an instruction at all: an opcode the processor does
    //not have (.WORD, db, ?). The backward disassembly does not trust those
    bool valid = false;
    DisasmFlow flow = FLOW_NONE;
    bool conditional = false;
    //The address the jump or the call goes to, when the instruction itself
    //says it (not through a register or a memory cell)
    bool has_target = false;
    unsigned int target = 0;
};

typedef uint8_t (*CommandBytes)[15];

class DisAsm
{
private:
    //The table: empty for a decoder of its own (PDP-11, 8086, 15ИЭ)
    std::vector<Instruction> ins;

public:
    unsigned int max_command_length;

    DisAsm();
    virtual ~DisAsm(){};

    // Table driven by default; CPUs whose instructions are encoded in bit
    // fields rather than whole bytes replace these with their own decoder.
    virtual emulator::Result load_file(const std::string &file_name);
    virtual unsigned int decode(CommandBytes bytes, unsigned int PC, unsigned int max_len, DisasmLine * out);

    //The text only
    unsigned int disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string * output);

    //Instructions start at multiples of it: 2 on the PDP-11, whose code is words
    virtual unsigned int unit_bytes() const { return 1; }
};

std::string bytes_dump(CommandBytes bytes, unsigned int length);
