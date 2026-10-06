// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MFM functions, source

#include <cstring>
#include <stdexcept>
#include "libs/mfm_tools.h"
#include "dsk_tools/core.h"

const uint8_t gcr62_encode_table[64] =
    {
        0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,
        0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
        0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,
        0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
        0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,
        0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
        0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6,
        0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};

// static const uint8_t agat_sector_translate[]={
//     0x00,0x0D,0x0B,0x09,0x07,0x05,0x03,0x01,0x0E,0x0C,0x0A,0x08,0x06,0x04,0x02,0x0F
// };

static const int agat_140_raw2logic[16] = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
};


// Idea: https://tulip-house.ddo.jp/digital/SDISK2/english.html (dsk2nic.cpp)
static const unsigned char FlipBit1[4] = { 0, 2,  1,  3  };
static const unsigned char FlipBit2[4] = { 0, 8,  4,  12 };
static const unsigned char FlipBit3[4] = { 0, 32, 16, 48 };

#define GAP0    48
#define GAP1    6
#define GAP2    27

uint8_t * load_image(const std::string &file_name, int image_size)
{
    long long file_size = dsk_tools::utf8_file_size(file_name);
    // A short tail past the sectors is ignored (the demo disk of the Agat
    // sound card ends in four bytes). Less than a sector, so that an image of
    // another format - an 840K disk opened in a 140K drive - is still refused
    if (file_size >= image_size && file_size - image_size < 256)
    {
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (file.is_open()){
            uint8_t * image = new uint8_t[image_size];
            file.read(reinterpret_cast<char*>(image), image_size);
            file.close();
            return image;
        } else {
            throw std::runtime_error("Error opening file: " + file_name);
        }
    } else {
        throw std::runtime_error("Incorrect disk image size for: " + file_name);
    }
    return nullptr;
}

static void code44(const uint8_t buffer[], int len, uint8_t * out)
{
    for (int i=0; i<len; i++) {
        out[i*2]   = (buffer[i] >> 1) | 0xaa;
        out[i*2+1] =  buffer[i]       | 0xaa;
    }
}

void encode_gcr62(const uint8_t data_in[], uint8_t * data_out)
{

    // First 86 bytes are combined 2 lower bits of input data
    for (int i = 0; i < 86; i++) {
        data_out[i] = FlipBit1[data_in[i]&3] | FlipBit2[data_in[i+86]&3] | FlipBit3[data_in[(i+172) & 0xFF]&3];
                                                                                                    // ^^ 2 extra bytes are wrapped to the beginning
    }

    // Next 256 bytes are upper 6 bits
    for (int i = 0; i < 256; i++) {
        data_out[i+86] = data_in[i] >> 2;
    }

    // Then, encoding 6 bits to 8 bits using a table and calculating a crc
    uint8_t crc = 0;
    for (int i = 0; i < 342; i++) {
        uint8_t v = data_out[i];
        data_out[i] = gcr62_encode_table[v ^ crc];
        crc = v;
    }

    // And finally adding a crc byte
    data_out[342] = gcr62_encode_table[crc];
}

static const uint8_t prologue_address[] = {0xD5, 0xAA, 0x96};
static const uint8_t prologue_data[]    = {0xD5, 0xAA, 0xAD};
static const uint8_t epilogue[]         = {0xDE, 0xAA, 0xEB};

uint8_t * generate_mfm_agat_140(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[])
{
    int track_len = 6400;
    sides = 1;
    tracks = 35;
    int sectors = 16;
    int sector_size = 256;
    disk_size = tracks * track_len;
    int image_size = tracks * sectors * sector_size;
    uint8_t * image = load_image(file_name, image_size);

    uint8_t * buffer = new uint8_t[disk_size];
    uint8_t * out = buffer;

    char gap_bytes[256];
    memset(&gap_bytes, 0xFF, sizeof(gap_bytes));

    uint8_t encoded_sector[344];
    uint8_t encoded_address[8];

    for (uint8_t track = 0; track < tracks; track++){
        // GAP 0
        memcpy(out, &gap_bytes, GAP0); out += GAP0;
        for (uint8_t sector = 0; sector < sectors; sector++) {
            // Prologue
            memcpy(out, prologue_address, 3); out += 3;
            // Address
            uint8_t volume = 0xFE;
            uint8_t sector_t = agat_140_raw2logic[sector];
            uint8_t address_field[4] = {volume, track, sector, static_cast<uint8_t>(volume ^ track ^ sector)};
            code44(address_field, 4, encoded_address);
            memcpy(out, encoded_address, 8); out += 8;
            // Epilogue
            memcpy(out, epilogue, 3); out += 3;
            // GAP 1
            memcpy(out, &gap_bytes, GAP1); out += GAP1;
            // Data field
            // Prologue
            memcpy(out, prologue_data, 3); out += 3;
            uint8_t * data = &image[track * sectors * sector_size + sector_t * sector_size];
            encode_gcr62(data, encoded_sector);
            memcpy(out, &encoded_sector, 343); out += 343;
            // Epilogue
            memcpy(out, epilogue, 3); out += 3;
            // GAP 2
            memcpy(out, &gap_bytes, GAP2); out += GAP2;
        }
        // GAP 3
        int gap3 = track_len - (GAP0 + sectors*(
                                                  3 +                                       // Address prologue
                                                  8 +                                       // Address
                                                  3 +                                       // Address epilogue
                                                  GAP1 +
                                                  3 +                                       // Data prologue
                                                  343 +                                     // Data
                                                  3 +                                       // Data epilogue
                                                  GAP2
                                        )
                                );

        memcpy(out, &gap_bytes, gap3); out += gap3;
    }

    delete [] image;

    for (int i=0; i < tracks; i++) {
        track_indexes[i].track_number = i;
        track_indexes[i].side_number = 0;
        track_indexes[i].mfmtracksize = track_len;
        track_indexes[i].mfmtrackoffset = i * track_len;
    }

    return buffer;
}

void save_mfm_file(const std::string &file_name, int sides, int tracks, int track_size, HXC_MFM_TRACK_INFO track_indexes[], uint8_t * data)
{
    HXC_MFM_HEADER      hxc_mfm_header;
    HXC_MFM_TRACK_INFO  hxc_mfm_track_info;

    dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
    if (file.is_open()){
        //header
        strcpy((char*)&hxc_mfm_header.headername, "HXCMFM");
        hxc_mfm_header.number_of_track = tracks;
        hxc_mfm_header.number_of_side = sides;
        int track_offset_mult = (hxc_mfm_header.number_of_side==2)?2:1;
        hxc_mfm_header.floppyRPM = 300;
        hxc_mfm_header.floppyBitRate = 250;
        hxc_mfm_header.floppyiftype = 0;
        hxc_mfm_header.mfmtracklistoffset = sizeof(HXC_MFM_HEADER);

        file.write((char*)(&hxc_mfm_header), sizeof(HXC_MFM_HEADER));

        //track list
        for (uint8_t track = 0; track < tracks; track++){
            for (uint8_t head = 0; head < sides; head++){
                hxc_mfm_track_info.track_number = track;
                hxc_mfm_track_info.side_number = head;
                hxc_mfm_track_info.mfmtracksize = track_size;
                hxc_mfm_track_info.mfmtrackoffset = 0x800 + (track*track_offset_mult + head)*track_size;
                file.write((char*)(&hxc_mfm_track_info), sizeof(HXC_MFM_TRACK_INFO));
            }
        }

        uint8_t fill = 0;
        int fill_size = 0x800 - sizeof(HXC_MFM_HEADER) - sizeof(HXC_MFM_TRACK_INFO) * tracks * sides;
        for (int i=0; i < fill_size; i++){
            file.write((char*)(&fill), 1);
        }

        file.write((char*)data, sides*tracks*track_size);

        file.close();
    }

}

uint8_t agat_840_calc_cs(const uint8_t data[], const int len)
{
    uint32_t cs=0;
    uint32_t carry=0;

    for (int i=0; i<len; i++) {
        cs += data[i] + carry;
        carry = cs >> 8;
        cs &= 0xFF;
    }

    return static_cast<uint8_t>(cs);
}

uint8_t * generate_mfm_agat_840(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & aim_codes)
{
    sides = 2;
    tracks = 80;
    int sectors = 21;
    int sector_size = 256;
    int track_len = AGAT_840_TRACK_LEN;
    //The drive's own formatter spaces the sectors evenly over the whole track,
    //and so does this: packing them into the first 5922 bytes would leave the
    //rest without a single sync mark, and a read starting in that stretch runs
    //out of retries before it finds one
    int sector_step = track_len / sectors;
    disk_size = tracks * track_len * sides;
    int image_size = sides * tracks * sectors * sector_size;
    uint8_t * image = load_image(file_name, image_size);

    uint8_t * buffer = new uint8_t[disk_size];
    //Everything the sectors do not cover is gap, and the gap byte is $AA
    memset(buffer, 0xAA, disk_size);

    int track_index = 0;

    for (uint8_t track = 0; track < tracks; track++)
        for (uint8_t head = 0; head < sides; head++) {
            int agat_track = track*2 + head;
            aim_codes[agat_track].clear();
            for (uint8_t sector = 0; sector < sectors; sector++) {
                int track_pos = sector * sector_step;
                uint8_t * out = buffer + agat_track*track_len + track_pos;
                // Address field
                aim_codes[agat_track][track_pos + AGAT_840_SYNC_OFFSET] = AIM_CMD_DESYNC;
                memcpy(out, &agat_840_address_field, AGAT_840_ADDRESS_SIZE);
                out[AGAT_840_ADDRESS_TRACK] = agat_track;
                out[AGAT_840_ADDRESS_SECTOR] = sector;
                out += AGAT_840_ADDRESS_SIZE;
                // Data field
                aim_codes[agat_track][track_pos + AGAT_840_ADDRESS_SIZE + AGAT_840_SYNC_OFFSET] = AIM_CMD_DESYNC;
                memcpy(out, &agat_840_data_field, AGAT_840_DATA_SIZE); out += AGAT_840_DATA_SIZE;
                // Data
                uint8_t * data = &image[(agat_track * sectors + sector) * sector_size];
                memcpy(out, data, sector_size); out += sector_size;
                // Checksum
                *out++ = agat_840_calc_cs(data, sector_size);
                // Footer
                *out++ = 0x5A;
            }

            track_indexes[track_index].track_number = track;
            track_indexes[track_index].side_number = head;
            track_indexes[track_index].mfmtracksize = track_len;
            track_indexes[track_index].mfmtrackoffset = agat_track*track_len;
            track_index++;
        }

    delete [] image;

    return buffer;
}

uint8_t * load_aim_image(const std::string &file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & aim_codes)
{
    sides = 2;
    tracks = 80;
    int track_len = AGAT_840_AIM_TRACK_LEN;
    disk_size = tracks * track_len * sides;
    uint16_t * image = reinterpret_cast<uint16_t*>(load_image(file_name, disk_size*2));
    uint8_t * out = new uint8_t[disk_size];

    for (uint8_t track = 0; track < tracks; track++) {
        for (uint8_t head = 0; head < sides; head++) {
            int agat_track = track*2 + head;
            aim_codes[agat_track].clear();
            for (int track_pos=0; track_pos<track_len; track_pos++) {
                int image_pos = agat_track*track_len + track_pos;
                uint16_t w = image[image_pos];
                uint8_t hi = static_cast<uint8_t>(w >> 8);
                uint8_t lo = static_cast<uint8_t>(w & 0xFF);
                if (hi !=0) {
                    aim_codes[agat_track][track_pos] = hi;
                }
                out[image_pos] = lo;
            }
            track_indexes[agat_track].track_number = track;
            track_indexes[agat_track].side_number = head;
            track_indexes[agat_track].mfmtracksize = track_len;
            track_indexes[agat_track].mfmtrackoffset = (track*2 + head)*track_len;
        }
    }
    delete [] image;
    return out;
}

//------------------------- ДВК, контроллер MX ------------------------------//

void encode_track_dvk_mx(const uint8_t * flat_track, int track, int side, uint8_t * out)
{
    memset(out, 0, DVK_MX_TRACK_WORDS * 2);
    unsigned int p = 8;     // нулевые слова от индекса до синхрослова
    auto put = [&](uint16_t w) {
        out[p * 2] = (uint8_t)(w & 0xFF);
        out[p * 2 + 1] = (uint8_t)(w >> 8);
        p++;
    };
    put(DVK_MX_SYNC_WORD);
    put((uint16_t)track);
    for (int s = 0; s < DVK_MX_IMAGE_SECTORS; s++) {
        uint16_t sum = 0;
        const uint8_t * d = flat_track + s * DVK_MX_IMAGE_SECTOR;
        for (int i = 0; i < DVK_MX_IMAGE_SECTOR; i += 2) {
            const uint16_t w = (uint16_t)(d[i] | (d[i + 1] << 8));
            put(w);
            sum = (uint16_t)(sum + w);
        }
        put(sum);
    }
    for (int i = 0; i < 3; i++) put((uint16_t)(0101400 | (track * 2 + side)));
}

uint8_t * generate_tracks_dvk_mx(const uint8_t * flat, size_t flat_size, int sides, int tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[])
{
    const int track_bytes = DVK_MX_TRACK_WORDS * 2;
    const size_t flat_track = DVK_MX_IMAGE_SECTORS * DVK_MX_IMAGE_SECTOR;
    disk_size = sides * tracks * track_bytes;
    uint8_t * buffer = new uint8_t[disk_size];
    std::vector<uint8_t> blank(flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const size_t from = (size_t)i * flat_track;
            // Образ короче геометрии - недостающие дорожки пустые
            const uint8_t * src = (from + flat_track <= flat_size) ? flat + from : blank.data();
            if (from < flat_size && from + flat_track > flat_size) {
                memcpy(blank.data(), flat + from, flat_size - from);
                src = blank.data();
            }
            encode_track_dvk_mx(src, t, s, buffer + (size_t)i * track_bytes);
            if (src == blank.data()) memset(blank.data(), 0, flat_track);
            track_indexes[i].track_number = (uint16_t)t;
            track_indexes[i].side_number = (uint8_t)s;
            track_indexes[i].mfmtracksize = track_bytes;
            track_indexes[i].mfmtrackoffset = (uint32_t)(i * track_bytes);
        }
    return buffer;
}

void decode_tracks_dvk_mx(const uint8_t * buffer, int sides, int tracks, const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat)
{
    const size_t flat_track = DVK_MX_IMAGE_SECTORS * DVK_MX_IMAGE_SECTOR;
    flat.assign((size_t)sides * tracks * flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const uint8_t * tr = buffer + track_indexes[i].mfmtrackoffset;
            const size_t words = track_indexes[i].mfmtracksize / 2;
            size_t p = 0;
            while (p < words && (tr[p * 2] | (tr[p * 2 + 1] << 8)) != DVK_MX_SYNC_WORD) p++;
            p += 2;     // синхрослово и номер дорожки
            if (p + DVK_MX_IMAGE_SECTORS * (DVK_MX_IMAGE_SECTOR / 2 + 1) > words) continue;
            uint8_t * out = flat.data() + (size_t)i * flat_track;
            for (int sec = 0; sec < DVK_MX_IMAGE_SECTORS; sec++) {
                memcpy(out + sec * DVK_MX_IMAGE_SECTOR, tr + p * 2, DVK_MX_IMAGE_SECTOR);
                p += DVK_MX_IMAGE_SECTOR / 2 + 1;   // слова сектора и сумма
            }
        }
}

//------------------------- IBM MFM (К1801ВП1-128) --------------------------//

#define IBM_GAP_FIRST   42      // GAP4a + GAP1 перед первым сектором
#define IBM_GAP_SECTOR  36      // GAP3 между секторами
#define IBM_GAP_HEADER  22      // GAP2 между заголовком и данными
#define IBM_SYNC_BYTES  12
#define IBM_GAP_BYTE    0x4E
#define IBM_MARK_BYTE   0xA1
#define IBM_MARK_HEADER 0xFE
#define IBM_MARK_DATA   0xFB
#define IBM_MARK_DELETED 0xF8

static uint16_t ibm_crc(uint16_t crc, const uint8_t * p, size_t n)
{
    for (size_t k = 0; k < n; k++) {
        crc ^= (uint16_t)(p[k] << 8);
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

int ibm_mfm_size_code(int sector_size)
{
    int code = 0;
    while ((128 << code) < sector_size && code < 6) code++;
    return code;
}

void encode_track_ibm_mfm(const uint8_t * flat_track, int sectors, int sector_size, int track, int side, uint16_t deleted, uint8_t * out, std::map<int, int> & marks)
{
    memset(out, IBM_GAP_BYTE, IBM_MFM_TRACK_BYTES);
    marks.clear();
    unsigned int p = 0;
    unsigned int gap = IBM_GAP_FIRST;
    for (int sect = 0; sect < sectors; sect++) {
        p += gap;
        memset(out + p, 0, IBM_SYNC_BYTES); p += IBM_SYNC_BYTES;

        marks[(int)p] = AIM_CMD_DESYNC;
        unsigned int start = p;
        out[p++] = IBM_MARK_BYTE; out[p++] = IBM_MARK_BYTE; out[p++] = IBM_MARK_BYTE;
        out[p++] = IBM_MARK_HEADER;
        out[p++] = (uint8_t)track;
        out[p++] = (uint8_t)side;
        out[p++] = (uint8_t)(sect + 1);
        out[p++] = (uint8_t)ibm_mfm_size_code(sector_size);
        uint16_t crc = ibm_crc(0xFFFF, out + start, p - start);
        out[p++] = (uint8_t)(crc >> 8);
        out[p++] = (uint8_t)(crc & 0xFF);

        p += IBM_GAP_HEADER;
        memset(out + p, 0, IBM_SYNC_BYTES); p += IBM_SYNC_BYTES;

        marks[(int)p] = AIM_CMD_DESYNC;
        start = p;
        out[p++] = IBM_MARK_BYTE; out[p++] = IBM_MARK_BYTE; out[p++] = IBM_MARK_BYTE;
        out[p++] = (deleted & (1u << sect)) ? IBM_MARK_DELETED : IBM_MARK_DATA;
        if (flat_track != nullptr) memcpy(out + p, flat_track + (size_t)sect * sector_size, sector_size);
        else memset(out + p, 0, sector_size);
        p += sector_size;
        crc = ibm_crc(0xFFFF, out + start, p - start);
        out[p++] = (uint8_t)(crc >> 8);
        out[p++] = (uint8_t)(crc & 0xFF);

        gap = IBM_GAP_SECTOR;
    }
}

uint8_t * generate_tracks_ibm_mfm(const uint8_t * flat, size_t flat_size, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                                  const uint16_t * deleted, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & marks)
{
    const size_t flat_track = (size_t)sectors * sector_size;
    disk_size = sides * tracks * IBM_MFM_TRACK_BYTES;
    uint8_t * buffer = new uint8_t[disk_size];
    std::vector<uint8_t> tmp(flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const size_t from = (size_t)(sides_order ? (s * tracks + t) : i) * flat_track;
            // Образ короче геометрии - недостающие дорожки размечены, но пусты
            memset(tmp.data(), 0, flat_track);
            if (from < flat_size)
                memcpy(tmp.data(), flat + from, (from + flat_track <= flat_size) ? flat_track : flat_size - from);
            encode_track_ibm_mfm(tmp.data(), sectors, sector_size, t, s, deleted ? deleted[i] : 0,
                                 buffer + (size_t)i * IBM_MFM_TRACK_BYTES, marks[i]);
            track_indexes[i].track_number = (uint16_t)t;
            track_indexes[i].side_number = (uint8_t)s;
            track_indexes[i].mfmtracksize = IBM_MFM_TRACK_BYTES;
            track_indexes[i].mfmtrackoffset = (uint32_t)(i * IBM_MFM_TRACK_BYTES);
        }
    return buffer;
}

// Метки находятся по самим байтам; пометки синхро тут не нужны
bool decode_track_ibm_mfm(const uint8_t * data, size_t len, int sectors, int sector_size, uint8_t * out, int & count, uint16_t & deleted)
{
    const size_t total = (size_t)sectors * sector_size;
    size_t p = 0, o = 0;
    count = 0;
    deleted = 0;
    for (;;) {
        while (p < len && data[p] == IBM_GAP_BYTE) p++;
        if (p >= len) break;                                    // конец дорожки
        while (p < len && data[p] == 0) p++;
        if (p >= len) return false;
        for (int i = 0; i < 3 && p < len && data[p] == IBM_MARK_BYTE; i++) p++;
        if (p >= len || data[p++] != IBM_MARK_HEADER) return false;
        if (p + 6 > len) return false;
        const uint8_t size_code = data[p + 3];
        p += 4 + 2;                                             // поля заголовка и CRC
        if (size_code > 6) return false;
        const size_t size = (size_t)128 << size_code;

        while (p < len && data[p] == IBM_GAP_BYTE) p++;
        while (p < len && data[p] == 0) p++;
        for (int i = 0; i < 3 && p < len && data[p] == IBM_MARK_BYTE; i++) p++;
        if (p >= len) return false;
        const uint8_t mark = data[p++];
        if (mark != IBM_MARK_DATA && mark != IBM_MARK_DELETED) return false;
        if (mark == IBM_MARK_DELETED && o < total) deleted |= (uint16_t)(1u << (o / sector_size));
        if (p + size + 2 > len) return false;
        for (size_t i = 0; i < size; i++) {
            if (o >= total) break;
            out[o++] = data[p++];
        }
        p += 2;                                                 // CRC данных
    }
    count = (int)(o / sector_size);
    return true;
}

void decode_tracks_ibm_mfm(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                           const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat)
{
    const size_t flat_track = (size_t)sectors * sector_size;
    flat.assign((size_t)sides * tracks * flat_track, 0);
    std::vector<uint8_t> tmp(flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            int count = 0;
            uint16_t deleted = 0;
            memset(tmp.data(), 0, flat_track);
            // Дорожка, которая не разбирается, остаётся в образе пустой
            if (!decode_track_ibm_mfm(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize,
                                      sectors, sector_size, tmp.data(), count, deleted)) continue;
            const size_t to = (size_t)(sides_order ? (s * tracks + t) : i) * flat_track;
            memcpy(flat.data() + to, tmp.data(), flat_track);
        }
}

void find_marks_ibm_mfm(const uint8_t * data, size_t len, std::map<int, int> & marks)
{
    marks.clear();
    for (size_t p = 1; p + 3 < len; p++)
        if (data[p - 1] == 0 && data[p] == IBM_MARK_BYTE && data[p + 1] == IBM_MARK_BYTE && data[p + 2] == IBM_MARK_BYTE
            && (data[p + 3] == IBM_MARK_HEADER || data[p + 3] == IBM_MARK_DATA || data[p + 3] == IBM_MARK_DELETED))
            marks[(int)p] = AIM_CMD_DESYNC;
}

//------------------------- IBM 3740 FM (RX01) ------------------------------//

#define FM_GAP4A        40
#define FM_GAP1         26
#define FM_GAP2         11
#define FM_GAP3         27
#define FM_SYNC         6
#define FM_GAP_BYTE     0xFF
#define FM_MARK_INDEX   0xFC
#define FM_MARK_ID      0xFE
#define FM_MARK_DATA    0xFB
#define FM_MARK_DELETED 0xF8

void encode_track_ibm_fm(const uint8_t * flat_track, int sectors, int sector_size, int track, int side, uint16_t deleted, uint8_t * out, std::map<int, int> & marks)
{
    memset(out, FM_GAP_BYTE, IBM_FM_TRACK_BYTES);
    marks.clear();
    unsigned int p = FM_GAP4A;
    memset(out + p, 0, FM_SYNC); p += FM_SYNC;
    marks[(int)p] = AIM_CMD_DESYNC;
    out[p++] = FM_MARK_INDEX;
    p += FM_GAP1;
    for (int sect = 0; sect < sectors; sect++) {
        memset(out + p, 0, FM_SYNC); p += FM_SYNC;
        marks[(int)p] = AIM_CMD_DESYNC;
        unsigned int start = p;
        out[p++] = FM_MARK_ID;
        out[p++] = (uint8_t)track;
        out[p++] = (uint8_t)side;
        out[p++] = (uint8_t)(sect + 1);
        out[p++] = (uint8_t)ibm_mfm_size_code(sector_size);
        uint16_t crc = ibm_crc(0xFFFF, out + start, p - start);
        out[p++] = (uint8_t)(crc >> 8);
        out[p++] = (uint8_t)(crc & 0xFF);
        p += FM_GAP2;
        memset(out + p, 0, FM_SYNC); p += FM_SYNC;
        marks[(int)p] = AIM_CMD_DESYNC;
        start = p;
        out[p++] = (deleted & (1u << sect)) ? FM_MARK_DELETED : FM_MARK_DATA;
        if (flat_track != nullptr) memcpy(out + p, flat_track + (size_t)sect * sector_size, sector_size);
        else memset(out + p, 0, sector_size);
        p += sector_size;
        crc = ibm_crc(0xFFFF, out + start, p - start);
        out[p++] = (uint8_t)(crc >> 8);
        out[p++] = (uint8_t)(crc & 0xFF);
        p += FM_GAP3;
    }
}

uint8_t * generate_tracks_ibm_fm(const uint8_t * flat, size_t flat_size, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                                 const uint16_t * deleted, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & marks)
{
    const size_t flat_track = (size_t)sectors * sector_size;
    disk_size = sides * tracks * IBM_FM_TRACK_BYTES;
    uint8_t * buffer = new uint8_t[disk_size];
    std::vector<uint8_t> tmp(flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const size_t from = (size_t)(sides_order ? (s * tracks + t) : i) * flat_track;
            memset(tmp.data(), 0, flat_track);
            if (from < flat_size)
                memcpy(tmp.data(), flat + from, (from + flat_track <= flat_size) ? flat_track : flat_size - from);
            encode_track_ibm_fm(tmp.data(), sectors, sector_size, t, s, deleted ? deleted[i] : 0,
                                buffer + (size_t)i * IBM_FM_TRACK_BYTES, marks[i]);
            track_indexes[i].track_number = (uint16_t)t;
            track_indexes[i].side_number = (uint8_t)s;
            track_indexes[i].mfmtracksize = IBM_FM_TRACK_BYTES;
            track_indexes[i].mfmtrackoffset = (uint32_t)(i * IBM_FM_TRACK_BYTES);
        }
    return buffer;
}

// Проход по полям дорожки: для каждой метки - её смещение и байт. Метка -
// байт FC/FE/FB/F8 после нулей; за ID идут 6 байт, за данными - сектор по
// коду размера последнего ID и CRC
template <typename F>
static void fm_walk(const uint8_t * data, size_t len, F field)
{
    size_t p = 0;
    int size = 128;
    while (p < len) {
        if (data[p] != 0) { p++; continue; }
        while (p < len && data[p] == 0) p++;
        if (p >= len) break;
        const uint8_t m = data[p];
        if (m == FM_MARK_INDEX) { field((int)p, m, 0); p++; continue; }
        if (m == FM_MARK_ID) {
            if (p + 7 > len) break;
            const uint8_t code = data[p + 4];
            size = (code <= 6) ? (128 << code) : 128;
            field((int)p, m, 0);
            p += 7;
            continue;
        }
        if (m == FM_MARK_DATA || m == FM_MARK_DELETED) {
            field((int)p, m, size);
            p += 1 + (size_t)size + 2;
            continue;
        }
        p++;
    }
}

bool decode_track_ibm_fm(const uint8_t * data, size_t len, int sectors, int sector_size, uint8_t * out, int & count, uint16_t & deleted)
{
    count = 0;
    deleted = 0;
    int id_sector = -1;
    fm_walk(data, len, [&](int p, uint8_t m, int size) {
        if (m == FM_MARK_ID) { id_sector = data[p + 3]; return; }
        if ((m == FM_MARK_DATA || m == FM_MARK_DELETED) && id_sector >= 1 && id_sector <= sectors
            && size == sector_size && (size_t)p + 1 + size <= len) {
            memcpy(out + (size_t)(id_sector - 1) * sector_size, data + p + 1, size);
            if (m == FM_MARK_DELETED) deleted |= (uint16_t)(1u << (id_sector - 1));
            count++;
        }
        id_sector = -1;
    });
    return count > 0;
}

void decode_tracks_ibm_fm(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                          const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat)
{
    const size_t flat_track = (size_t)sectors * sector_size;
    flat.assign((size_t)sides * tracks * flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const size_t to = (size_t)(sides_order ? (s * tracks + t) : i) * flat_track;
            int count = 0;
            uint16_t deleted = 0;
            decode_track_ibm_fm(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize,
                                sectors, sector_size, flat.data() + to, count, deleted);
        }
}

void find_marks_ibm_fm(const uint8_t * data, size_t len, std::map<int, int> & marks)
{
    marks.clear();
    fm_walk(data, len, [&](int p, uint8_t, int) { marks[p] = AIM_CMD_DESYNC; });
}

int find_sector_ibm_fm(const uint8_t * data, size_t len, int track, int sector, bool & deleted, int & size)
{
    int found = -1;
    bool want = false;
    fm_walk(data, len, [&](int p, uint8_t m, int sz) {
        if (found >= 0) return;
        if (m == FM_MARK_ID) { want = data[p + 1] == track && data[p + 3] == sector; return; }
        if ((m == FM_MARK_DATA || m == FM_MARK_DELETED) && want && (size_t)p + 1 + sz + 2 <= len) {
            found = p;
            deleted = (m == FM_MARK_DELETED);
            size = sz;
        }
        want = false;
    });
    return found;
}

void put_sector_ibm_fm(uint8_t * data, size_t len, int mark, const uint8_t * src, int size, bool deleted)
{
    if (mark < 0 || (size_t)mark + 1 + size + 2 > len) return;
    data[mark] = deleted ? FM_MARK_DELETED : FM_MARK_DATA;
    memcpy(data + mark + 1, src, size);
    const uint16_t crc = ibm_crc(0xFFFF, data + mark, 1 + (size_t)size);
    data[mark + 1 + size] = (uint8_t)(crc >> 8);
    data[mark + 2 + size] = (uint8_t)(crc & 0xFF);
}
