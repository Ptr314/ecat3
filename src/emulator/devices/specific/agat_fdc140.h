// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat 140K floppy disk controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

class Agat_FDC140 : public FDC
{
protected:
    Interface i_select;
    Interface i_motor_on;
    int prev_phase;
    int current_phase;

    int current_track[2];
    int selected_drive;
    int drives_count;
    FDD * drives[2];
    bool motor_on;
    bool write_mode;
    bool speed_mode = true;

    uint8_t data;
    bool data_ready;

    uint8_t write_register;

    void phase_on(int n);
    void phase_off(int n);
    void select_drive(int n);

public:
    Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd) override;

    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
    virtual void clock(unsigned int counter) override;
};

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd);
