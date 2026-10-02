// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 disassembler, header

#pragma once

#include "emulator/disasm.h"

// An 8086 instruction is an opcode with a ModR/M byte whose fields pick the
// registers and one of 24 address forms, each with its own displacement. The
// byte pattern table cannot describe that, so this decoder walks the fields.
// Intel syntax, numbers in hex with the $ prefix the rest of the emulator uses.
// PC is the linear address of the instruction; a jump target is shown as the
// linear address it reaches inside the same code segment. That segment is
// the processor's CS, read through cs (the debugger lives no longer than the
// processor it shows); without it, or for code outside CS, the target is
// taken as linear
class DisAsmX86: public DisAsm
{
public:
    explicit DisAsmX86(const uint16_t * cs = nullptr);

    virtual emulator::Result load_file(const std::string &file_name) override;
    virtual unsigned int disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string * output) override;

private:
    const uint16_t * m_cs;
};
