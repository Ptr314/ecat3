// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Helpers of the debugger's views

#pragma once

#include <QString>
#include <QStringList>

#include "emulator/core.h"
#include "emulator/devices/cpu/i8086.h"
#include "emulator/devices/cpu/k1801vm1.h"
#include "dialogs/numfmt.h"

//The addresses a processor's code lives at, as its debugger sees them: the
//К1801 family 64 KB of its own addresses (the ВМ3 translates them through its
//MMU), anything else its mapper
inline unsigned int code_space(CPU * cpu)
{
    if (dynamic_cast<k1801vm1*>(cpu) != nullptr) return 0x10000;
    const unsigned int s = (cpu->mm != nullptr) ? cpu->mm->get_size() : 0;
    return (s > 0) ? s : 0x10000;
}

//A register by name, 0 when there is none
inline unsigned int register_value(CPU * cpu, const std::string &name)
{
    for (const CpuRegister &r : cpu->get_register_values())
        if (r.name == name) return r.value;
    return 0;
}

//An address of code as a listing writes it: CS:IP on the 8086 when it lies in
//the current code segment, the plain number otherwise
inline QString code_address(CPU * cpu, unsigned int address, int base, int digits)
{
    if (dynamic_cast<i8086*>(cpu) != nullptr) {
        const unsigned int cs = register_value(cpu, "CS");
        const unsigned int from = cs << 4;
        if (address >= from && address - from <= 0xFFFF)
            return fmt_num(cs, 16, 4) + ":" + fmt_num(address - from, 16, 4);
        return fmt_num(address, 16, 5) + "    ";
    }
    return fmt_num(address, base, digits);
}

//Space-separated words, empty ones dropped (Qt::SkipEmptyParts came with 5.14)
inline QStringList split_words(const QString &s)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return s.split(' ', Qt::SkipEmptyParts);
#else
    return s.split(' ', QString::SkipEmptyParts);
#endif
}
