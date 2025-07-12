// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat 840K floppy disk controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/i8255.h"

#define AGAT_840_TRACK_COUNT 80

class Agat_FDC840 : public FDC
{
protected:
    Interface i_select;
    Interface i_side;
    Interface i_motor_on;

    int current_track[2];
    int selected_drive;
    int drives_count;
    FDD * drives[2];
    bool motor_on;
    bool write_mode;
    int step_dir;
    int side;
    bool sector_sync;
    bool write_sync;
    bool data_ready;

    I8255 dd14;
    I8255 dd15;

    void update_status();
    void update_state();

    void read_next_byte();
    void write_next_byte();

#ifdef LOG_FDD
    RAM * ram0;
    bool start_log = false;
#endif

public:
    Agat_FDC840(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd) override;

    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
    virtual void reset(bool cold) override;
    virtual void clock(unsigned int counter) override;
};

ComputerDevice * create_agat_fdc840(InterfaceManager *im, EmulatorConfigDevice *cd);
