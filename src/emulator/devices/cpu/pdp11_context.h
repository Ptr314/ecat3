// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common definitions for the PDP-11 compatible CPU core (К1801ВМ1)

#pragma once

#include <cstdint>

#define PDP11_FAMILY_1801VM1  0
#define PDP11_FAMILY_1801VM2  1

#pragma pack(1)

struct pdp11context
{
    uint16_t R[8];              // R6 = SP, R7 = PC
    uint16_t PSW;
    bool halted;                // WAIT executed, idling until an interrupt
    bool halt_mode;             // running in the halt (console) mode
    bool stop;                  // double bus error, dead until INIT
    int type;
};

#pragma pack()

// A decoded operand: either a register or a memory address
struct pdp11operand
{
    bool is_reg;
    unsigned int reg;
    uint16_t addr;
};

namespace PDP11
{
    const uint16_t F_C    = 1 << 0;
    const uint16_t F_V    = 1 << 1;
    const uint16_t F_Z    = 1 << 2;
    const uint16_t F_N    = 1 << 3;
    const uint16_t F_T    = 1 << 4;
    const uint16_t F_ALL  = F_C | F_V | F_Z | F_N;

    // The 1801 series uses a single interrupt mask bit instead of the
    // three-bit priority field of the big PDP-11 machines
    const uint16_t F_MASK = 0200;

    const unsigned int REG_SP = 6;
    const unsigned int REG_PC = 7;

    // Trap vectors
    const uint16_t V_BUS_ERROR  = 0004;     // bus timeout (non-existent memory); the 1801 ignores odd word addresses
    const uint16_t V_ILLEGAL    = 0004;     // illegal instruction: JMP or JSR to a register
    const uint16_t V_RESERVED   = 0010;     // reserved instruction
    const uint16_t V_BPT        = 0014;     // BPT and T-bit trace trap
    const uint16_t V_IOT        = 0020;
    const uint16_t V_POWER_FAIL = 0024;
    const uint16_t V_EMT        = 0030;
    const uint16_t V_TRAP       = 0034;

    // Fixed vectors of the 1801 series interrupt inputs
    const uint16_t V_IRQ2       = 0100;
    const uint16_t V_IRQ3       = 0270;
}
