// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Number notation of the debug windows

#pragma once

#include <QString>

#include "emulator/core.h"
#include "emulator/utils.h"

//A machine written in octal (radix = 8: the БК, the ДВК, the УК-НЦ) is shown in
//octal, every other one in hex as before - also a machine whose files are
//decimal, since its documents give addresses in hex. The machine's own radix,
//not get_default_radix(), which a script's RADIX may have changed
inline int view_base(ComputerDevice * d)
{
    return (d != nullptr && d->radix() == 8) ? 8 : 16;
}

inline QString fmt_num(unsigned int value, int base, int digits)
{
    return QString("%1").arg(value, digits, base, QChar('0')).toUpper();
}

//The digits a value needs in the base, but no fewer than a 16-bit address does
inline int addr_digits(unsigned int max_value, int base)
{
    int digits = 0;
    for (unsigned int v = max_value; v != 0; v /= (unsigned int)base) digits++;
    const int min_digits = (base == 8) ? 6 : 4;
    return (digits < min_digits) ? min_digits : digits;
}

//Digits of one cell of the given width in bits: a byte or a word
inline int cell_digits(int base, bool word)
{
    if (base == 8) return word ? 6 : 3;
    return word ? 4 : 2;
}

//A number typed into a debug window: in the window's base unless it carries a
//prefix of its own ($, &, #, _). Throws on a bad value, as parse_numeric_value
inline unsigned int parse_input(const QString &text, int base)
{
    return parse_numeric_value(text.trimmed().toStdString(), (unsigned int)base);
}
