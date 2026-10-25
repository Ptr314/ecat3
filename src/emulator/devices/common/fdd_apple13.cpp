// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: FDD device, 13-sector Apple ][ disks (DOS 3.2, 5&3 encoding)

#include <cstring>

#include "fdd.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"
#include "libs/dsk_tools/src/utils.h"

// The disks of the Disk II before DOS 3.3: 13 sectors of 256 bytes a track,
// each byte split into five bits and three, written as one of 32 nibbles
// with no two zero bits in a row (CiderPress, diskimg/Nibble.cpp, and
// "Beneath Apple DOS"). A .d13 image holds the sectors by number; on the
// track they lie in the order DOS 3.2 formats them.
//
//   address field   D5 AA B5, volume, track, sector, checksum (4&4), DE AA EB
//   data field      D5 AA AD, 154 + 256 nibbles, checksum, DE AA EB

static const int D13_TRACKS = 35;
static const int D13_SECTORS = 13;
static const size_t TRACK_BYTES = 6656;     // a track of a .nib image

static const uint8_t NIBBLES_53[32] = {
    0xAB, 0xAD, 0xAE, 0xAF, 0xB5, 0xB6, 0xB7, 0xBA,
    0xBB, 0xBD, 0xBE, 0xBF, 0xD6, 0xD7, 0xDA, 0xDB,
    0xDD, 0xDE, 0xDF, 0xEA, 0xEB, 0xED, 0xEE, 0xEF,
    0xF5, 0xF6, 0xF7, 0xFA, 0xFB, 0xFD, 0xFE, 0xFF
};

// The physical order of the sectors on a track formatted by DOS 3.2
static const int ORDER_32[D13_SECTORS] = {0, 10, 7, 4, 1, 11, 8, 5, 2, 12, 9, 6, 3};

static void put44(std::vector<uint8_t> &t, uint8_t v)
{
    t.push_back((uint8_t)((v >> 1) | 0xAA));
    t.push_back((uint8_t)(v | 0xAA));
}

static void encode_53(const uint8_t *sector, std::vector<uint8_t> &t)
{
    const int CHUNK = 51;
    uint8_t top[256];
    uint8_t threes[154];
    int chunk = CHUNK - 1;
    for (int i = 0; i < 255; i += 5) {
        const uint8_t b1 = sector[i], b2 = sector[i + 1], b3 = sector[i + 2], b4 = sector[i + 3], b5 = sector[i + 4];
        top[chunk] = b1 >> 3;
        top[chunk + CHUNK] = b2 >> 3;
        top[chunk + CHUNK * 2] = b3 >> 3;
        top[chunk + CHUNK * 3] = b4 >> 3;
        top[chunk + CHUNK * 4] = b5 >> 3;
        threes[chunk] = (uint8_t)((b1 & 7) << 2 | (b4 & 4) >> 1 | (b5 & 4) >> 2);
        threes[chunk + CHUNK] = (uint8_t)((b2 & 7) << 2 | (b4 & 2) | (b5 & 2) >> 1);
        threes[chunk + CHUNK * 2] = (uint8_t)((b3 & 7) << 2 | (b4 & 1) << 1 | (b5 & 1));
        chunk--;
    }
    top[255] = sector[255] >> 3;
    threes[153] = sector[255] & 7;
    uint8_t sum = 0;
    for (int i = 153; i >= 0; i--) { t.push_back(NIBBLES_53[threes[i] ^ sum]); sum = threes[i]; }
    for (int i = 0; i < 256; i++) { t.push_back(NIBBLES_53[top[i] ^ sum]); sum = top[i]; }
    t.push_back(NIBBLES_53[sum]);
}

emulator::Result FDD::load_d13(const std::string &file_name)
{
    const size_t size = (size_t)D13_TRACKS * D13_SECTORS * 256;
    const std::string data = dsk_tools::utf8_read_file(file_name);
    if (data.size() != size)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect disk image size for")) + "} " + file_name);
    const uint8_t *img = reinterpret_cast<const uint8_t *>(data.data());
    const uint8_t volume = 254;

    uint8_t *out = new uint8_t[(size_t)D13_TRACKS * TRACK_BYTES];
    std::vector<uint8_t> t;
    for (int track = 0; track < D13_TRACKS; track++) {
        t.clear();
        t.insert(t.end(), 48, 0xFF);
        for (int k = 0; k < D13_SECTORS; k++) {
            const int s = ORDER_32[k];
            t.push_back(0xD5); t.push_back(0xAA); t.push_back(0xB5);
            put44(t, volume); put44(t, (uint8_t)track); put44(t, (uint8_t)s);
            put44(t, (uint8_t)(volume ^ track ^ s));
            t.push_back(0xDE); t.push_back(0xAA); t.push_back(0xEB);
            t.insert(t.end(), 6, 0xFF);
            t.push_back(0xD5); t.push_back(0xAA); t.push_back(0xAD);
            encode_53(img + ((size_t)track * D13_SECTORS + s) * 256, t);
            t.push_back(0xDE); t.push_back(0xAA); t.push_back(0xEB);
            t.insert(t.end(), 27, 0xFF);
        }
        t.resize(TRACK_BYTES, 0xFF);
        memcpy(out + (size_t)track * TRACK_BYTES, t.data(), TRACK_BYTES);
    }

    delete [] buffer;
    buffer = out;
    sides = 1;
    tracks = D13_TRACKS;
    physical_track_len = (int)TRACK_BYTES;
    disk_size = D13_TRACKS * (int)TRACK_BYTES;
    for (int i = 0; i < D13_TRACKS; i++) {
        track_indexes[i].track_number = i;
        track_indexes[i].side_number = 0;
        track_indexes[i].mfmtracksize = (uint32_t)TRACK_BYTES;
        track_indexes[i].mfmtrackoffset = (uint32_t)(i * TRACK_BYTES);
    }
    track_mode = FDD_MODE_WHOLE_TRACK;
    position = 0;
    loaded = true;
    m_generation++;
    return emulator::Result::ok();
}

// The other way: the sectors read off the nibble tracks, the way DOS 3.2
// reads them. A track that has none is an unformatted one and gives zeros; a
// track that has some, but not all thirteen, or a disk with none at all is
// not saved - that would lose data without a word
static bool decode_53(const uint8_t *nib, uint8_t *sector)
{
    static uint8_t inv[256];
    static bool ready = false;
    if (!ready) {
        memset(inv, 0xFF, sizeof(inv));
        for (int i = 0; i < 32; i++) inv[NIBBLES_53[i]] = (uint8_t)i;
        ready = true;
    }
    uint8_t threes[154], base[256];
    uint8_t sum = 0;
    int k = 0;
    for (int i = 153; i >= 0; i--) {
        const uint8_t v = inv[nib[k++]];
        if (v == 0xFF) return false;
        sum ^= v;
        threes[i] = sum;
    }
    for (int i = 0; i < 256; i++) {
        const uint8_t v = inv[nib[k++]];
        if (v == 0xFF) return false;
        sum ^= v;
        base[i] = (uint8_t)(sum << 3);
    }
    const uint8_t v = inv[nib[k]];
    if (v == 0xFF || (sum ^ v) != 0) return false;
    const int CHUNK = 51;
    uint8_t *p = sector;
    for (int i = CHUNK - 1; i >= 0; i--) {
        const uint8_t t1 = threes[i], t2 = threes[CHUNK + i], t3 = threes[CHUNK * 2 + i];
        const uint8_t t4 = (uint8_t)((t1 & 2) << 1 | (t2 & 2) | (t3 & 2) >> 1);
        const uint8_t t5 = (uint8_t)((t1 & 1) << 2 | (t2 & 1) << 1 | (t3 & 1));
        *p++ = (uint8_t)(base[i] | ((t1 >> 2) & 7));
        *p++ = (uint8_t)(base[CHUNK + i] | ((t2 >> 2) & 7));
        *p++ = (uint8_t)(base[CHUNK * 2 + i] | ((t3 >> 2) & 7));
        *p++ = (uint8_t)(base[CHUNK * 3 + i] | (t4 & 7));
        *p++ = (uint8_t)(base[CHUNK * 4 + i] | (t5 & 7));
    }
    *p = (uint8_t)(base[255] | (threes[153] & 7));
    return true;
}

emulator::Result FDD::save_d13(const std::string &file_name)
{
    const emulator::Result bad = emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error exporting disk.")) + "} " + file_name);
    if (!whole_track() || sides != 1) return bad;
    std::vector<uint8_t> out((size_t)D13_TRACKS * D13_SECTORS * 256, 0);
    std::vector<uint8_t> t;
    unsigned found = 0;
    for (int track = 0; track < D13_TRACKS && track < tracks; track++) {
        if (!read_track(track, 0, t) || t.empty()) continue;
        // Round the track twice: a field may straddle its end
        const size_t n = t.size();
        t.insert(t.end(), t.begin(), t.end());
        bool have[D13_SECTORS] = {false};
        unsigned count = 0;
        for (size_t i = 0; i + 14 < 2 * n && i < n + 8; i++) {
            if (t[i] != 0xD5 || t[i + 1] != 0xAA || t[i + 2] != 0xB5) continue;
            const uint8_t trk = (uint8_t)(((t[i + 5] << 1) | 1) & t[i + 6]);
            const uint8_t sec = (uint8_t)(((t[i + 7] << 1) | 1) & t[i + 8]);
            if (trk != track || sec >= D13_SECTORS || have[sec]) continue;
            for (size_t j = i + 11; j < i + 64 && j + 3 + 411 < 2 * n; j++)
                if (t[j] == 0xD5 && t[j + 1] == 0xAA && t[j + 2] == 0xAD) {
                    if (decode_53(&t[j + 3], &out[((size_t)track * D13_SECTORS + sec) * 256])) {
                        have[sec] = true;
                        count++;
                    }
                    break;
                }
        }
        if (count != 0 && count != (unsigned)D13_SECTORS) return bad;
        found += count;
    }
    if (found == 0) return bad;
    dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
    if (!file.is_open())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
    file.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
    return emulator::Result::ok();
}
