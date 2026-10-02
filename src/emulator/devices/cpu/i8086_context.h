// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 (К1810ВМ86/ВМ88) registers

#pragma once

#include <cstdint>

#define I8086_FAMILY_8088   0
#define I8086_FAMILY_8086   1

namespace I8086
{
//Word registers in the order of the reg field of an instruction
enum { AX = 0, CX, DX, BX, SP, BP, SI, DI };
//Segment registers in the order of the sreg field
enum { ES = 0, CS, SS, DS };

const uint16_t F_CF = 0x0001;
const uint16_t F_PF = 0x0004;
const uint16_t F_AF = 0x0010;
const uint16_t F_ZF = 0x0040;
const uint16_t F_SF = 0x0080;
const uint16_t F_TF = 0x0100;
const uint16_t F_IF = 0x0200;
const uint16_t F_DF = 0x0400;
const uint16_t F_OF = 0x0800;

//Bits of FLAGS that exist; the 8086 reads the others as 1 (12-15, 1) or 0 (3, 5)
const uint16_t F_MASK = 0x0FD5;
const uint16_t F_ONES = 0xF002;

//The prefetch queue of the 8086 is six bytes long; the 8088 uses four of them
const unsigned QUEUE_MAX = 6;
}

struct i8086context
{
    uint16_t r[8];      //AX CX DX BX SP BP SI DI
    uint16_t s[4];      //ES CS SS DS
    uint16_t ip;        //of the next byte the execution unit takes from the queue
    uint16_t flags;     //only the bits of F_MASK are kept
    bool     halted;
};
