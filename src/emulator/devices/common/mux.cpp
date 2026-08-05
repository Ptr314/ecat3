// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Multiplexer IC device

#include "mux.h"

#define CHANGED_IN          1
#define CHANGED_S           4


Multiplexer::Multiplexer(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , i_a(this, im, 8, "a", MODE_R, CHANGED_IN)
    , i_b(this, im, 8, "b", MODE_R, CHANGED_IN)
    , i_out(this, im, 8, "out", MODE_W)
    , i_s(this, im, 1, "s", MODE_R, CHANGED_S)
{}

void Multiplexer::interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value)
{
    if ((i_s.value & 1) == 0) i_out.change(i_a.value);
    else i_out.change(i_b.value);
}

ComputerDevice * create_mux(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Multiplexer(im, cd);
}