// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Wave generator, header

#pragma once

#include "emulator/core.h"

class Generator:public ComputerDevice
{
private:
    Interface i_out;
    Interface i_enable;

    bool positive;
    unsigned int total_counts;
    unsigned int pulse_counts;
    unsigned int pulse_stored;
    bool in_pulse;
    bool enabled;

public:
    Generator(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd) override;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    virtual void system_clock(unsigned int counter) override;
};

ComputerDevice * create_generator(InterfaceManager *im, EmulatorConfigDevice *cd);
