// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MFM functions, header

#pragma once

#include <map>
#include <string>
#include "mfm_formats.h"

// A sector as the drive's own formatter lays it down (ПЗУ $E47D and $DECD):
// an address field, five gap bytes, then a data field. In each field the $FF
// is the byte the controller marks with a loss of bit sync, and a read locks
// on that mark and takes the byte under it as the one to throw away.
//
// The five bytes between the fields are not decoration: the guest reads the
// address field, decides whether this is the sector it wants and only then
// starts looking for the data mark. One byte, which is what this used to
// generate, is enough only while a byte takes twice as long as it should.
static const uint8_t agat_840_address_field[]={
    0xA4, 0xFF,     // the $FF carries the sync mark
    0x95, 0x6A,
    0xFE,           // volume
    0x00,           // track
    0x00,           // sector
    0x5A,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA
};

static const uint8_t agat_840_data_field[]={
    0xA4, 0xFF,     // the $FF carries the sync mark
    0x6A, 0x95
};

// Commands of an AIM cell, as they are stored in the high byte of it.
// A DESYNC comes in three flavours: $80 is the flag on its own, $01 the one
// the reference dumps use, $81 both at once - all three mark the same thing,
// a place where the controller loses bit sync and re-locks on the next mark.
// $03 and $13 (the index pulse) also carry bit 0 and are NOT a DESYNC, so the
// test has to name the values instead of masking them.
#define AIM_CMD_DESYNC      0x01
#define AIM_CMD_END         0x02
#define AIM_CMD_INDEX_ON    0x03
#define AIM_CMD_INDEX_OFF   0x13
#define AIM_CMD_DESYNC_ALT  0x80
#define AIM_CMD_DESYNC_BOTH 0x81

inline bool aim_is_desync(int code)
{
    return code == AIM_CMD_DESYNC || code == AIM_CMD_DESYNC_ALT || code == AIM_CMD_DESYNC_BOTH;
}

#define AGAT_840_ADDRESS_SIZE sizeof(agat_840_address_field)
#define AGAT_840_DATA_SIZE    sizeof(agat_840_data_field)
// Offset of the $FF inside either field: that is where the sync mark goes
#define AGAT_840_SYNC_OFFSET   1
// Byte cells on a physical track, and where the number comes from: the
// controller hands a byte over every AGAT_840_BYTE_CYCLES processor cycles and
// the drive turns AGAT_840_RPM times a minute, so 1021000/5/32 = 6381 of them
// go past the head of an Агат-9 in one turn. The 21 sectors take 5922 bytes of
// that; the rest is the gap the drive's own formatter needs to lay them out,
// and a track cut down to the sectors alone is one no ROM can format.
//
// AGAT_840_BYTE_CYCLES is not a free parameter either: the loop of Агат ДОС
// that measures the gap between two sectors ($E512) reads a byte every 32
// cycles, and with anything shorter it misses bytes, comes up short and an
// INIT never converges.
#define AGAT_840_TRACK_LEN     6381
#define AGAT_840_BYTE_CYCLES   32
#define AGAT_840_RPM           300
// An .aim dump keeps a whole turn and a little overlap, so its tracks are
// longer than the count above and are read at their own length
#define AGAT_840_AIM_TRACK_LEN 6464
#define AGAT_840_ADDRESS_TRACK  5
#define AGAT_840_ADDRESS_SECTOR 6

typedef std::map<int, int> AgatAIMCodes[160];

uint8_t * generate_mfm_agat_140(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);
uint8_t * generate_mfm_agat_840(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & aim_codes);
uint8_t * load_aim_image(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & aim_codes);

void save_mfm_file(const std::string &file_name, int sides, int tracks, int track_size, HXC_MFM_TRACK_INFO track_indexes[], uint8_t * data);