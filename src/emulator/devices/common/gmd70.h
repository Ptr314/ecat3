// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: GMD70 (Электроника-ГМД70) FDC device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

class GMD70 : public FDC
{
private:
    Interface i_select;
    Interface i_motor;

    FDD * m_drives[2]{};
    bool m_busy = false;
    unsigned m_drives_count = 0;
    unsigned m_selected_drive = 0;
    bool m_ints_en = false;
    unsigned m_command = 0;
    bool m_reserved = false;
    bool m_trq = false;             // Transfer request
    int m_error = -1;
    bool m_done = false;
    uint8_t m_data = 0;
    uint8_t m_buffer[128];
    unsigned m_counter = 0;
    unsigned m_command_counter = 0;
    uint8_t m_command_buffer[2];
    void reset_fdc();
    void do_command();
    void motor_on();
    void motor_off();
public:
    GMD70(InterfaceManager *im, EmulatorConfigDevice *cd);
    bool get_busy() override;
    unsigned get_selected_drive() override;
    unsigned  get_value(unsigned  address) override;
    void set_value(unsigned  address, unsigned  value, bool force=false) override;
    void load_config(SystemData *sd) override;
    void reset(bool cold) override;
};

ComputerDevice * create_GMD70(InterfaceManager *im, EmulatorConfigDevice *cd);