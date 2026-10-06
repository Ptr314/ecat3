// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MFM functions, header

#pragma once

#include <map>
#include <string>
#include <vector>
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

// Пометки дорожки по позиции байта: у Агата 840 - коды ячеек AIM, у
// дорожек IBM MFM - места синхробайтов A1 с пропуском синхроимпульса. По
// одной карте на физическую дорожку (дорожка * сторон + сторона)
typedef std::map<int, int> AgatAIMCodes[200];

uint8_t * generate_mfm_agat_140(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);
uint8_t * generate_mfm_agat_840(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & aim_codes);
uint8_t * load_aim_image(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & aim_codes);

void save_mfm_file(const std::string &file_name, int sides, int tracks, int track_size, HXC_MFM_TRACK_INFO track_indexes[], uint8_t * data);

// ДВК, контроллер MX (FM, 5,25", 300 об/мин). Контроллер секторов не знает:
// он читает и пишет дорожку словами, по слову за 128 мкс, так что за оборот
// (200 мс) под головкой проходит 1562 слова. Дорожка привода - эти слова,
// младший байт первым. Плоский образ (11 секторов по 256 байт на сторону,
// стороны дорожки подряд) переводится в формат RT-11: 8 нулевых слов,
// синхрослово 000363, номер дорожки, 11 секторов по 128 слов, за каждым -
// сумма его слов, и три слова 0101400 + дорожка * 2 + сторона. Остаток
// оборота - нули.
#define DVK_MX_TRACK_WORDS      1562
#define DVK_MX_SYNC_WORD        0000363
#define DVK_MX_IMAGE_SECTORS    11
#define DVK_MX_IMAGE_SECTOR     256

uint8_t * generate_tracks_dvk_mx(const uint8_t * flat, size_t flat_size, int sides, int tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[]);
void encode_track_dvk_mx(const uint8_t * flat_track, int track, int side, uint8_t * out);
// Сектора дорожки по синхрослову обратно в плоский образ. Дорожка без
// синхрослова (стёртая) или короткая оставляет его сектора нулями
void decode_tracks_dvk_mx(const uint8_t * buffer, int sides, int tracks, const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat);

// Дорожка IBM MFM, как её видит К1801ВП1-128 (КНГМД БК, контроллер УК-НЦ,
// плата КМД ДВК): поток декодированных байтов, 6250 за оборот (250 кбит/с,
// 300 об/мин). Перед каждым полем - промежуток 4E, 12 нулей и три
// синхробайта A1 с пропуском синхроимпульса: первый из них помечается в
// карте пометок кодом AIM_CMD_DESYNC (контроллер ловит пометку словом, и
// метка всегда на чётной позиции). Заголовок FE, дорожка, сторона, сектор,
// код размера, CRC; данные FB (F8 - с меткой удаления), сектор, CRC.
// CRC-16-CCITT, начальное FFFF с первого A1
#define IBM_MFM_TRACK_BYTES     6250

// Размер кода сектора: 128 << код
int ibm_mfm_size_code(int sector_size);
void encode_track_ibm_mfm(const uint8_t * flat_track, int sectors, int sector_size, int track, int side, uint16_t deleted, uint8_t * out, std::map<int, int> & marks);
// flat - сектора подряд; sides_order - порядок дорожек образа (все дорожки
// стороны 0, затем стороны 1) или по цилиндрам. deleted - по слову на
// физическую дорожку (бит на сектор), может быть nullptr
uint8_t * generate_tracks_ibm_mfm(const uint8_t * flat, size_t flat_size, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                                  const uint16_t * deleted, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & marks);
// Сектора одной дорожки в порядке, в каком они лежат; false - дорожка не
// разбирается (не размечена или испорчена)
bool decode_track_ibm_mfm(const uint8_t * data, size_t len, int sectors, int sector_size, uint8_t * out, int & count, uint16_t & deleted);
void decode_tracks_ibm_mfm(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                           const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat);
// Пометки синхро по самим байтам: в .mfm их нет, а A1 A1 A1 перед FE, FB
// или F8 после нулей - это и есть адресная метка
void find_marks_ibm_mfm(const uint8_t * data, size_t len, std::map<int, int> & marks);

// Дорожка IBM 3740 FM (8", 360 об/мин, 250 кбит/с: 5208 байт за оборот) -
// RX01, контроллер DX ДВК. Промежуток FF, 6 нулей, индексная метка FC, FF;
// у каждого сектора 6 нулей, ID FE (дорожка, сторона, сектор, код размера,
// CRC), 11 FF, 6 нулей, данные FB (F8 - с меткой удаления), сектор, CRC,
// 27 FF. Метки FC, FE, FB, F8 пишутся с особым синхроимпульсом: это они и
// помечены в карте пометок. CRC-16-CCITT, начальное FFFF с байта метки
#define IBM_FM_TRACK_BYTES      5208

void encode_track_ibm_fm(const uint8_t * flat_track, int sectors, int sector_size, int track, int side, uint16_t deleted, uint8_t * out, std::map<int, int> & marks);
uint8_t * generate_tracks_ibm_fm(const uint8_t * flat, size_t flat_size, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                                 const uint16_t * deleted, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & marks);
// Сектора дорожки на места по номерам из заголовков (1..sectors); deleted -
// бит на сектор с меткой удаления. false - дорожка не разбирается
bool decode_track_ibm_fm(const uint8_t * data, size_t len, int sectors, int sector_size, uint8_t * out, int & count, uint16_t & deleted);
void decode_tracks_ibm_fm(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                          const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat);
void find_marks_ibm_fm(const uint8_t * data, size_t len, std::map<int, int> & marks);
// Метка данных сектора с этим номером и дорожкой в заголовке: смещение байта
// метки или -1; deleted - метка F8. size - размер сектора по заголовку
int find_sector_ibm_fm(const uint8_t * data, size_t len, int track, int sector, bool & deleted, int & size);
// Данные сектора на место (метка FB/F8 и CRC пересчитываются)
void put_sector_ibm_fm(uint8_t * data, size_t len, int mark, const uint8_t * src, int size, bool deleted);