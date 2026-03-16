// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8259 (КР580ВН59) programmable interrupt controller

#pragma once

#include "emulator/core.h"

class I8259:public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_ir;             // Interrupt request inputs IR0-IR7
    Interface i_int;            // Interrupt output to CPU
    Interface i_inta;           // Interrupt acknowledge from CPU

    uint8_t ICW[4];             // Initialization Command Words
    uint8_t OCW[3];             // Operation Command Words
    uint8_t IMR;                // Interrupt Mask Register
    uint8_t IRR;                // Interrupt Request Register
    uint8_t ISR;                // In-Service Register
    uint8_t icw_step;           // Current ICW initialization step
    bool    initialized;        // Initialization complete flag
    bool    read_isr;           // Read ISR (true) or IRR (false) on A0=0

    void init();
    void evaluate_interrupt();
    int  get_highest_priority(uint8_t reg);
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

public:
    I8259(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
};

ComputerDevice * create_i8259(InterfaceManager *im, EmulatorConfigDevice *cd);