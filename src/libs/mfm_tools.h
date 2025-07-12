// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MFM functions, header

#pragma once

#include <QString>
#include "mfm_formats.h"

static const uint8_t agat_840_prolog[]={
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xFF, 0xAA, 0xA4, 0x95, 0x6A,
    0xFE,   //volume
    00,     // track
    00,     // sector
    0x5A, 0xAA, 0xAA
};

static const uint8_t agat_840_header[]={
    0xAA, 0x6A, 0x95
};

#define AGAT_840_PROLOG_SIZE sizeof(agat_840_prolog)
#define AGAT_840_HEADER_SIZE sizeof(agat_840_header)
#define AGAT_840_PROLOG_TRACK  16
#define AGAT_840_PROLOG_SECTOR 17

uint8_t * generate_mfm_agat_140(QString file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);
uint8_t * generate_mfm_agat_840(QString file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);

void save_mfm_file(QString file_name, int sides, int tracks, int track_size, HXC_MFM_TRACK_INFO track_indexes[], uint8_t * data);
