// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scanning matrix-based keyboard device

#pragma once

#include "emulator/devices/common/keyboard.h"

struct ScanData {
    unsigned int key_code;
    unsigned int scan_line;
    unsigned int out_line;
    int shift_state;
};

class ScanKeyboard: public Keyboard
{
private:
    Interface i_scan;
    Interface i_output;
    Interface i_shift;
    Interface i_ctrl;
    Interface i_ruslat;
    Interface i_ruslat_led;

    unsigned int scan_lines;
    unsigned int out_lines;

    unsigned int keys_count;
    ScanData scan_data[200];
    unsigned int key_array[15];

    unsigned int code_ctrl;
    unsigned int code_shift;
    unsigned int code_ruslat;

    unsigned int stored_shift;

    void calculate_out();

protected:
    virtual void set_rus(bool new_rus) override;

public:
    ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    virtual void key_down(unsigned int key) override;
    virtual void key_up(unsigned int key) override;

    virtual void load_config(SystemData *sd) override;
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
