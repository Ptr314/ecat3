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

//------------------------- Дорожки целиком ---------------------------------//

void marks_from_special(const std::vector<uint8_t> & special, bool mfm, std::map<int, int> & marks)
{
    marks.clear();
    for (size_t p = 0; p < special.size(); p++)
        if (special[p] && !(mfm && p > 0 && special[p - 1]))
            marks[(int)p] = AIM_CMD_DESYNC;
}

void special_from_marks(const std::map<int, int> & marks, const uint8_t * data, size_t len, bool mfm, std::vector<uint8_t> & special)
{
    special.assign(len, 0);
    for (const auto &e : marks) {
        if (!aim_is_desync(e.second) || e.first < 0) continue;
        size_t p = (size_t)e.first;
        if (mfm)
            for (int n = 0; n < 3 && p < len && data[p] == 0xA1; n++) special[p++] = 1;
        else if (p < len)
            special[p] = 1;
    }
}

// Диск целиком: дорожки подряд, сторона за стороной; образ короче
// геометрии - недостающие дорожки размечены, но пусты
template <typename F>
static uint8_t * generate_tracks(const uint8_t * flat, size_t flat_size, int sides, int tracks, size_t flat_track, bool sides_order,
                                 int track_bytes, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], F format)
{
    disk_size = sides * tracks * track_bytes;
    uint8_t * buffer = new uint8_t[disk_size];
    std::vector<uint8_t> tmp(flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const size_t from = (size_t)(sides_order ? (s * tracks + t) : i) * flat_track;
            std::fill(tmp.begin(), tmp.end(), 0);
            if (from < flat_size)
                memcpy(tmp.data(), flat + from, (from + flat_track <= flat_size) ? flat_track : flat_size - from);
            format(t, s, i, tmp.data(), buffer + (size_t)i * track_bytes);
            track_indexes[i].track_number = (uint16_t)t;
            track_indexes[i].side_number = (uint8_t)s;
            track_indexes[i].mfmtracksize = track_bytes;
            track_indexes[i].mfmtrackoffset = (uint32_t)(i * track_bytes);
        }
    return buffer;
}

uint8_t * generate_tracks_dvk_mx(const uint8_t * flat, size_t flat_size, int sides, int tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[])
{
    return generate_tracks(flat, flat_size, sides, tracks, (size_t)DVK_MX_SECTORS * DVK_MX_SECTOR_SIZE, false,
                           DVK_MX_TRACK_WORDS * 2, disk_size, track_indexes,
                           [](int t, int s, int, const uint8_t * src, uint8_t * out) {
                               dsk_tools::BYTES data;
                               dsk_tools::dvk_mx_format_track(data, t, s, src);
                               memcpy(out, data.data(), data.size());
                           });
}

void decode_tracks_dvk_mx(const uint8_t * buffer, int sides, int tracks, const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat)
{
    const size_t flat_track = (size_t)DVK_MX_SECTORS * DVK_MX_SECTOR_SIZE;
    flat.assign((size_t)sides * tracks * flat_track, 0);
    std::vector<uint8_t> tmp(flat_track, 0);
    for (int i = 0; i < sides * tracks; i++) {
        // Дорожка без синхрослова (стёртая) оставляет сектора нулями
        if (dsk_tools::dvk_mx_read_track(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize, tmp.data()))
            memcpy(flat.data() + (size_t)i * flat_track, tmp.data(), flat_track);
    }
}

uint8_t * generate_tracks_ibm_mfm(const uint8_t * flat, size_t flat_size, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                                  const uint16_t * deleted, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & marks)
{
    return generate_tracks(flat, flat_size, sides, tracks, (size_t)sectors * sector_size, sides_order,
                           IBM_MFM_TRACK_BYTES, disk_size, track_indexes,
                           [&](int t, int s, int i, const uint8_t * src, uint8_t * out) {
                               dsk_tools::BYTES data, special;
                               dsk_tools::ibm_mfm_format_track(data, special, t, s, sectors, sector_size, src, deleted ? deleted[i] : 0);
                               memcpy(out, data.data(), data.size());
                               marks_from_special(special, true, marks[i]);
                           });
}

uint8_t * generate_tracks_ibm_fm(const uint8_t * flat, size_t flat_size, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                                 const uint16_t * deleted, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[], AgatAIMCodes & marks)
{
    return generate_tracks(flat, flat_size, sides, tracks, (size_t)sectors * sector_size, sides_order,
                           IBM_FM_TRACK_BYTES, disk_size, track_indexes,
                           [&](int t, int s, int i, const uint8_t * src, uint8_t * out) {
                               dsk_tools::BYTES data, special;
                               dsk_tools::ibm_fm_format_track(data, special, t, s, sectors, sector_size, src, deleted ? deleted[i] : 0);
                               memcpy(out, data.data(), data.size());
                               marks_from_special(special, false, marks[i]);
                           });
}

// Сектора дорожек на места по номерам из заголовков; дорожка, которая не
// разбирается, остаётся в образе пустой
template <typename F>
static void decode_tracks(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                          const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat, F read)
{
    const size_t flat_track = (size_t)sectors * sector_size;
    flat.assign((size_t)sides * tracks * flat_track, 0);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const int i = t * sides + s;
            const size_t to = (size_t)(sides_order ? (s * tracks + t) : i) * flat_track;
            uint16_t deleted = 0;
            int found = 0;
            read(buffer + track_indexes[i].mfmtrackoffset, (size_t)track_indexes[i].mfmtracksize, flat.data() + to, deleted, found);
        }
}

void decode_tracks_ibm_mfm(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                           const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat)
{
    decode_tracks(buffer, sides, tracks, sectors, sector_size, sides_order, track_indexes, flat,
                  [&](const uint8_t * d, size_t len, uint8_t * out, uint16_t & del, int & found) {
                      dsk_tools::ibm_mfm_read_track(d, len, sectors, sector_size, out, del, found);
                  });
}

void decode_tracks_ibm_fm(const uint8_t * buffer, int sides, int tracks, int sectors, int sector_size, bool sides_order,
                          const HXC_MFM_TRACK_INFO track_indexes[], std::vector<uint8_t> & flat)
{
    decode_tracks(buffer, sides, tracks, sectors, sector_size, sides_order, track_indexes, flat,
                  [&](const uint8_t * d, size_t len, uint8_t * out, uint16_t & del, int & found) {
                      dsk_tools::ibm_fm_read_track(d, len, sectors, sector_size, out, del, found);
                  });
}

void find_marks_ibm_mfm(const uint8_t * data, size_t len, std::map<int, int> & marks)
{
    dsk_tools::BYTES special;
    dsk_tools::ibm_mfm_find_marks(data, len, special);
    marks_from_special(special, true, marks);
}

void find_marks_ibm_fm(const uint8_t * data, size_t len, std::map<int, int> & marks)
{
    dsk_tools::BYTES special;
    dsk_tools::ibm_fm_find_marks(data, len, special);
    marks_from_special(special, false, marks);
}

//------------------------- Агат 840 в ячейках ------------------------------//

#define AGAT_840_SYNC_CELLS 0x4490      // $A4 без синхроимпульса разряда 0

void agat_840_to_cells(const uint8_t * data, size_t len, const std::map<int, int> & marks, std::vector<uint8_t> & cells)
{
    dsk_tools::mfm_encode(data, nullptr, len, cells);
    if (len == 0) return;
    for (const auto &e : marks) {
        if (!aim_is_desync(e.second) || e.first < 0 || (size_t)e.first >= len) continue;
        // Синхробайт - байт перед пометкой; у него пропадает синхроимпульс
        // последнего разряда, 15-я ячейка его слова, считая с первой
        const size_t sync = (e.first == 0) ? len - 1 : (size_t)e.first - 1;
        const size_t bit = sync * 16 + 14;
        cells[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
    }
}

void agat_840_from_cells(const std::vector<uint8_t> & cells, std::vector<uint8_t> & data, std::map<int, int> & marks)
{
    const size_t bits = cells.size() * 8;
    const size_t len = cells.size() / 2;
    data.assign(len, 0xAA);
    marks.clear();
    if (bits < 16) return;
    auto cell = [&](size_t b) { return (cells[b >> 3] >> (b & 7)) & 1; };

    // Синхробайты: 16 ячеек 4490h. В обычном MFM четырёх нулей подряд не
    // бывает, так что сдвинутое окно с ними не совпадёт
    std::vector<size_t> syncs;
    uint16_t w = 0;
    for (size_t b = 0; b < bits; b++) {
        w = (uint16_t)((w << 1) | cell(b));
        if (b >= 15 && w == AGAT_840_SYNC_CELLS) syncs.push_back(b - 15);
    }

    // Байты: сетка от каждого синхробайта до следующего; до первого - по
    // фазе первого, после последнего - по его фазе
    const size_t first_phase = syncs.empty() ? 0 : syncs[0] % 16;
    for (size_t k = 0; k <= syncs.size(); k++) {
        const size_t phase = (k == 0) ? first_phase : syncs[k - 1] % 16;
        const size_t from = (k == 0) ? 0 : syncs[k - 1] / 16;
        const size_t to = (k < syncs.size()) ? syncs[k] / 16 : len;
        for (size_t i = from; i < to && i < len; i++) {
            const size_t b0 = i * 16 + phase;
            if (b0 + 16 > bits) break;
            uint8_t d = 0;
            for (int j = 0; j < 8; j++) d = (uint8_t)((d << 1) | cell(b0 + 2 * j + 1));
            data[i] = d;
        }
    }
    for (size_t k = 0; k < syncs.size(); k++) {
        const size_t i = syncs[k] / 16;
        if (i < len) marks[(int)((i + 1) % len)] = AIM_CMD_DESYNC;
    }
}
