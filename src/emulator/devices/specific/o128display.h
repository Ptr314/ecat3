// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Orion-128 display controller device

#pragma once

#include "emulator/core.h"

class O128Display: public GenericDisplay
{
private:
    unsigned int mode;
    unsigned int frame;
    unsigned int base_address;

    Port * port_mode;
    Port * port_frame;
    RAM * page_main;
    RAM * page_color;

    void render_byte(unsigned int address);

protected:
    virtual void render_all(bool force_render) override;

public:
    O128Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void clock(unsigned int counter) override;
    virtual void load_config(SystemData *sd) override;

    virtual void memory_callback(unsigned int callback_id, unsigned int address) override;

    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
};

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd);
