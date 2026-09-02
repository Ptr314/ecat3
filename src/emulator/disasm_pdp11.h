// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: PDP-11 (К1801ВМ1) disassembler, header

#pragma once

#include "emulator/disasm.h"

// The PDP-11 encodes its operands as mode/register bit fields inside the
// instruction word, so the byte pattern table used for the 8 bit CPUs cannot
// describe it. This decoder walks the fields instead and prints everything in
// octal, the way PDP-11 documentation and assemblers do.
class DisAsmPDP11: public DisAsm
{
private:
    bool has_eis;

    std::string format_operand(unsigned int spec, CommandBytes bytes,
                               unsigned int PC, unsigned int & index);

public:
    DisAsmPDP11(bool has_eis = false);

    // No table is needed, the file name is accepted and ignored
    virtual emulator::Result load_file(const std::string &file_name) override;
    virtual unsigned int disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string * output) override;
};
