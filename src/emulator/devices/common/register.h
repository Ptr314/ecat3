// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Register IC device

#pragma once

#include "emulator/core.h"

class Register:public ComputerDevice
{
private:
    Interface i_in;
    Interface i_out;
    Interface i_c;
    Interface i_r;
    Interface i_s;

    unsigned register_value;
    unsigned store_type;
    unsigned default_value;

public:
    Register(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void reset(bool cold) override;
    virtual void load_config(SystemData *sd) override;
    virtual void interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value) override;
    virtual unsigned get_value();
};

ComputerDevice * create_register(InterfaceManager *im, EmulatorConfigDevice *cd);
