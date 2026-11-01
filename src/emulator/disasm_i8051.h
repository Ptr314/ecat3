// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8051 disassembler, header

#pragma once

#include "emulator/disasm.h"

// Свой декодер, а не таблица: адрес LJMP идёт старшим байтом вперёд, а биты
// и регистры SFR читаются по именам
class DisAsmI8051: public DisAsm
{
public:
    DisAsmI8051();
    emulator::Result load_file(const std::string &file_name) override;
    unsigned int decode(CommandBytes bytes, unsigned int PC, unsigned int max_len, DisasmLine * out) override;
};
