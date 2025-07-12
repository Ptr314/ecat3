// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: CRC16 functions, header

#pragma once

#include <cstdint>

void CRC16_update(uint16_t * CRC, uint8_t * buffer, unsigned int len);
uint16_t CRC16(uint8_t * buffer, unsigned int len);
