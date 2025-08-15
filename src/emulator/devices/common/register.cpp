// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Register IC device

#include "register.h"
#include "emulator/utils.h"

#define REGISTER_FLIPFLOP_POS  0
#define REGISTER_FLIPFLOP_NEG  1
#define REGISTER_LATCH_POS     2
#define REGISTER_LATCH_NEG     3
#define REGISTER_BUFFER        4

#define CHANGED_IN          1
#define CHANGED_C           2
#define CHANGED_R           3
#define CHANGED_S           4

Register::Register(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , register_value(0)
    , store_type(REGISTER_FLIPFLOP_POS)
    , i_in(this, im, 16, "in", MODE_R, CHANGED_IN)
    , i_out(this, im, 16, "out", MODE_W)
    , i_c(this, im, 1, "c", MODE_R, CHANGED_C)
    , i_r(this, im, 1, "r", MODE_R, CHANGED_R)
    , i_s(this, im, 1, "s", MODE_R, CHANGED_S)
{

    i_out.change(register_value);
}

void Register::reset(bool cold)
{
    register_value = 0;
    i_out.change(register_value);
}

void Register::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);
    //TODO: Register - add other types
    QString type_string = cd->get_parameter("type", false).value.toLower();

    if (type_string.isEmpty() || type_string == "flip-flop-pos")
        store_type = REGISTER_FLIPFLOP_POS;
    else if (type_string == "flip-flop-neg")
        store_type = REGISTER_FLIPFLOP_NEG;
    else if (type_string == "latch-pos")
        store_type = REGISTER_LATCH_POS;
    else if (type_string == "latch-neg")
        store_type = REGISTER_LATCH_NEG;
    else if (type_string == "buffer")
        store_type = REGISTER_BUFFER;
    else
        QMessageBox::critical(0, Register::tr("Error"), Register::tr("Unknown register type %1").arg(type_string));
}

void Register::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == CHANGED_C)
    {
        switch (store_type) {
        case REGISTER_FLIPFLOP_POS:
        case REGISTER_LATCH_POS:
            if (i_c.pos_edge()){
                register_value = i_in.value;
                i_out.change(register_value);
            }
            break;
        case REGISTER_FLIPFLOP_NEG:
        case REGISTER_LATCH_NEG:
            if (i_c.neg_edge()){
                register_value = i_in.value;
                i_out.change(register_value);
            }
            break;
        }
    } else
    if (callback_id == CHANGED_IN) {
        if (
            (store_type == REGISTER_BUFFER)
            || (store_type == REGISTER_LATCH_POS && i_c.value == 0 )
            || (store_type == REGISTER_LATCH_NEG && i_c.value == 1 )
            )
        {
            register_value = i_in.value;
            i_out.change(register_value);
        }
    } else
    if (callback_id == CHANGED_R) {
        if (i_r.neg_edge()){
            register_value = 0;
            i_out.change(register_value);
        }
    } else
    if (callback_id == CHANGED_S) {
        if (i_r.neg_edge()){
            register_value = _FFFF;
            i_out.change(register_value);
        }
    }
}

ComputerDevice * create_register(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Register(im, cd);
}
