// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common definitions for the Intel 8080 (КР580ВМ80) CPU core

#pragma once

#include <cstdint>

#pragma pack(1)
struct i8080context
{
    union {
        struct{
            uint8_t  C,B,E,D,L,H,
                m,
                A, F;
            uint16_t SP, PC;
        } regs;
        struct {
            uint16_t BC, DE, HL;
        } reg_pairs;
        uint8_t reg_array_8[8];
        uint16_t reg_array_16[3];
    } registers;
    bool halted;
    //TODO: i8080 maybe it should be bool
    unsigned int int_enable;
    unsigned int debug_mode;
};

#pragma pack()

namespace I8080
{
    const uint8_t F_BASE_8080 = 0x02; //This bit is always set

    const uint8_t F_CARRY      =  0x01;
    const uint8_t F_PARITY     =  0x04;
    const uint8_t F_HALF_CARRY =  0x10;
    const uint8_t F_ZERO       =  0x40;
    const uint8_t F_SIGN       =  0x80;
    const uint8_t F_ALL        =  (F_BASE_8080 + F_CARRY + F_PARITY + F_HALF_CARRY + F_ZERO + F_SIGN);
}
