// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common definitions for using with CPUs

#pragma once

#include <cstdint>

#define LO4(v)      (v & 0x0F)
#define HI4(v)      ((v >> 4) & 0x0F)

#define LO8(v)      (static_cast<uint8_t>(v & 0xFF))
#define HI8(v)      (static_cast<uint8_t>(v >> 8))

#pragma pack(1)

union PartsRecLE {
    struct{
        uint8_t L, H;
    } b;
    uint16_t w;
    uint32_t dw;
};

#pragma pack()
