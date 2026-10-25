// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: FDD device, WOZ images (the Apple II disk as a stream of bits)

#include <algorithm>
#include <cstring>

#include "fdd.h"
#include "dsk_tools/core.h"
#include "libs/dsk_tools/src/utils.h"

// https://applesaucefdc.com/woz/reference2/
//
// A WOZ file is a header and chunks: INFO, TMAP - the track for each of 160
// quarter-track positions ($FF: none), TRKS - the bits. WOZ1 keeps a track in
// a fixed record of 6656 bytes, WOZ2 in 512-byte blocks listed at the start of
// TRKS. Bits go most significant first; a '1' is a flux change

static const size_t WOZ_QUARTERS = 160;
static const size_t WOZ1_TRACK = 6656;
static const size_t NIB_TRACK = 6656;
static const size_t BLANK_BITS = 51200;     // a track written where there was none

static uint32_t rd32(const uint8_t * p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t * p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(std::vector<uint8_t> &v, size_t at, uint32_t x)
    { v[at] = x & 0xFF; v[at+1] = (x >> 8) & 0xFF; v[at+2] = (x >> 16) & 0xFF; v[at+3] = (x >> 24) & 0xFF; }
static void wr16(std::vector<uint8_t> &v, size_t at, uint16_t x)
    { v[at] = x & 0xFF; v[at+1] = (x >> 8) & 0xFF; }

static uint32_t woz_crc32(const uint8_t * p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static void unpack_bits(const uint8_t * p, size_t count, std::vector<uint8_t> &bits)
{
    bits.resize(count);
    for (size_t i = 0; i < count; i++) bits[i] = (p[i >> 3] >> (7 - (i & 7))) & 1;
}

// The nibbles a controller that reads bytes finds on the track: a byte ends
// when its top bit comes into the register
static void bits_to_nibbles(const std::vector<uint8_t> &bits, std::vector<uint8_t> &out)
{
    out.clear();
    uint8_t reg = 0;
    for (size_t i = 0; i < bits.size() && out.size() < NIB_TRACK; i++) {
        reg = (uint8_t)((reg << 1) | bits[i]);
        if (reg & 0x80) { out.push_back(reg); reg = 0; }
    }
    while (out.size() < NIB_TRACK) out.push_back(0xFF);
}

// Bytes as they go round under the head: a run of $FF of a gap is the
// self-sync, ten bits a byte (two zeros after it)
void FDD::nibbles_to_bits(const std::vector<uint8_t> &bytes, std::vector<uint8_t> &bits)
{
    static const size_t SYNC_RUN = 4;
    bits.clear();
    const size_t n = bytes.size();
    for (size_t i = 0; i < n; i++) {
        const uint8_t b = bytes[i];
        for (int k = 7; k >= 0; k--) bits.push_back((b >> k) & 1);
        if (b == 0xFF) {
            size_t run = 1;
            for (size_t j = 1; j < SYNC_RUN && bytes[(i + j) % n] == 0xFF; j++) run++;
            for (size_t j = 1; j < SYNC_RUN && bytes[(i + n - j) % n] == 0xFF; j++) run++;
            if (run >= SYNC_RUN) { bits.push_back(0); bits.push_back(0); }
        }
    }
}

bool FDD::parse_woz(const std::vector<uint8_t> &f)
{
    if (f.size() < 12 || memcmp(f.data() + 4, "\xFF\x0A\x0D\x0A", 4) != 0) return false;
    int version;
    if (memcmp(f.data(), "WOZ1", 4) == 0) version = 1;
    else if (memcmp(f.data(), "WOZ2", 4) == 0) version = 2;
    else return false;

    const uint8_t * info = nullptr;
    size_t info_size = 0;
    const uint8_t * tmap = nullptr;
    const uint8_t * trks = nullptr;
    size_t trks_size = 0;
    for (size_t at = 12; at + 8 <= f.size(); ) {
        const uint32_t size = rd32(&f[at + 4]);
        if ((uint64_t)at + 8 + size > f.size()) break;
        const uint8_t * data = &f[at + 8];
        if (memcmp(&f[at], "INFO", 4) == 0 && size >= 3) { info = data; info_size = size; }
        else if (memcmp(&f[at], "TMAP", 4) == 0 && size >= WOZ_QUARTERS) tmap = data;
        else if (memcmp(&f[at], "TRKS", 4) == 0) { trks = data; trks_size = size; }
        at += 8 + size;
    }
    // Only a 5.25" disk: a 3.5" one turns at speeds the drive here does not have
    if (info == nullptr || tmap == nullptr || trks == nullptr || info[1] != 1) return false;
    // WOZ2 gives the bit cell the disk was written with (some were written
    // fast on purpose), in eighths of a microsecond
    unsigned timing = 32;
    if (version == 2 && info_size >= 40 && info[0] >= 2 && info[39] >= 16 && info[39] <= 64) timing = info[39];

    std::vector<std::vector<uint8_t> > tracks_bits;
    if (version == 1) {
        const size_t count = std::min(trks_size / WOZ1_TRACK, WOZ_QUARTERS);
        for (size_t i = 0; i < count; i++) {
            const uint8_t * t = trks + i * WOZ1_TRACK;
            const uint16_t bit_count = rd16(t + 6648);
            std::vector<uint8_t> bits;
            if (bit_count > 0 && bit_count <= 6646 * 8) unpack_bits(t, bit_count, bits);
            tracks_bits.push_back(bits);
        }
    } else {
        if (trks_size < WOZ_QUARTERS * 8) return false;
        for (size_t i = 0; i < WOZ_QUARTERS; i++) {
            const uint8_t * e = trks + i * 8;
            const uint64_t start = (uint64_t)rd16(e) * 512;
            const uint64_t blocks = rd16(e + 2);
            const uint32_t bit_count = rd32(e + 4);
            std::vector<uint8_t> bits;
            if (start > 0 && bit_count > 0 && bit_count <= blocks * 512 * 8 && start + blocks * 512 <= f.size())
                unpack_bits(&f[(size_t)start], bit_count, bits);
            tracks_bits.push_back(bits);
        }
    }

    // A quarter that names no track with bits is blank
    for (size_t q = 0; q < WOZ_QUARTERS; q++) {
        const uint8_t t = tmap[q];
        m_woz_map[q] = (t < tracks_bits.size() && !tracks_bits[t].empty()) ? t : 0xFF;
    }
    m_woz_tracks.swap(tracks_bits);
    m_woz_timing = timing;
    m_woz = true;
    if (info[2] != 0) write_protect = true;
    woz_bytes();
    return true;
}

// The whole tracks as nibbles, laid out as a .nib image is
void FDD::woz_bytes()
{
    int count = 35;
    for (int t = 35; t < 40; t++) if (m_woz_map[t * 4] != 0xFF) count = t + 1;
    delete [] buffer;
    sides = 1;
    tracks = count;
    physical_track_len = (int)NIB_TRACK;
    disk_size = count * (int)NIB_TRACK;
    buffer = new uint8_t[disk_size];
    std::vector<uint8_t> bytes;
    for (int t = 0; t < count; t++) {
        const int index = woz_track(t * 4);
        if (index >= 0) bits_to_nibbles(m_woz_tracks[index], bytes);
        else bytes.assign(NIB_TRACK, 0xFF);
        memcpy(buffer + t * NIB_TRACK, bytes.data(), NIB_TRACK);
        track_indexes[t].track_number = t;
        track_indexes[t].side_number = 0;
        track_indexes[t].mfmtracksize = (uint32_t)NIB_TRACK;
        track_indexes[t].mfmtrackoffset = (uint32_t)(t * NIB_TRACK);
    }
    track_mode = FDD_MODE_WHOLE_TRACK;
    position = 0;
    loaded = true;
}

int FDD::woz_track(int quarter) const
{
    if (!m_woz || quarter < 0 || quarter >= (int)WOZ_QUARTERS) return -1;
    const uint8_t t = m_woz_map[quarter];
    return (t == 0xFF || t >= m_woz_tracks.size()) ? -1 : t;
}

bool FDD::read_bits(int index, std::vector<uint8_t> &bits) const
{
    if (!m_woz || index < 0 || index >= (int)m_woz_tracks.size()) {
        bits.assign(BLANK_BITS, 0);
        return false;
    }
    bits = m_woz_tracks[index];
    return true;
}

int FDD::write_bits(int quarter, int index, const std::vector<uint8_t> &bits)
{
    if (!m_woz || bits.empty()) return index;
    if (index < 0 || index >= (int)m_woz_tracks.size()) {
        if (quarter < 0 || quarter >= (int)WOZ_QUARTERS || m_woz_tracks.size() >= WOZ_QUARTERS) return -1;
        // A new track is wide: it is found a quarter track either side too,
        // as a drive writing it would leave it
        index = (int)m_woz_tracks.size();
        m_woz_tracks.push_back(bits);
        for (int q = quarter - 1; q <= quarter + 1; q++)
            if (q >= 0 && q < (int)WOZ_QUARTERS && (q == quarter || m_woz_map[q] == 0xFF)) m_woz_map[q] = (uint8_t)index;
    } else
        m_woz_tracks[index] = bits;
    // The nibbles of the whole tracks that show it
    std::vector<uint8_t> bytes;
    for (int t = 0; t < tracks; t++)
        if (woz_track(t * 4) == index) {
            bits_to_nibbles(m_woz_tracks[index], bytes);
            memcpy(buffer + t * NIB_TRACK, bytes.data(), NIB_TRACK);
        }
    return index;
}

static void write_woz(const uint8_t * map, const std::vector<std::vector<uint8_t> > &tracks, bool wp,
                      unsigned timing, std::vector<uint8_t> &out)
{
    // Header, INFO (60), TMAP (160), TRKS: 160 entries, then the bits from
    // block 3 on
    std::vector<size_t> start(tracks.size()), blocks(tracks.size());
    size_t block = 3, largest = 0;
    for (size_t i = 0; i < tracks.size(); i++) {
        start[i] = block;
        blocks[i] = (tracks[i].size() + 4095) / 4096;
        if (blocks[i] > largest) largest = blocks[i];
        block += blocks[i];
    }
    out.assign(block * 512, 0);
    memcpy(&out[0], "WOZ2\xFF\x0A\x0D\x0A", 8);

    memcpy(&out[12], "INFO", 4);
    wr32(out, 16, 60);
    out[20] = 2;                            // version
    out[21] = 1;                            // 5.25"
    out[22] = wp ? 1 : 0;
    memset(&out[25], ' ', 32);
    memcpy(&out[25], "eCat3", 5);
    out[57] = 1;                            // sides
    out[59] = (uint8_t)timing;              // the bit cell, 1/8 us
    wr16(out, 64, (uint16_t)largest);

    memcpy(&out[80], "TMAP", 4);
    wr32(out, 84, (uint32_t)WOZ_QUARTERS);
    memcpy(&out[88], map, WOZ_QUARTERS);

    memcpy(&out[248], "TRKS", 4);
    wr32(out, 252, (uint32_t)(out.size() - 256));
    for (size_t i = 0; i < tracks.size(); i++) {
        const size_t e = 256 + i * 8;
        wr16(out, e, (uint16_t)start[i]);
        wr16(out, e + 2, (uint16_t)blocks[i]);
        wr32(out, e + 4, (uint32_t)tracks[i].size());
        uint8_t * p = &out[start[i] * 512];
        for (size_t b = 0; b < tracks[i].size(); b++)
            if (tracks[i][b]) p[b >> 3] |= (uint8_t)(0x80 >> (b & 7));
    }
    wr32(out, 8, woz_crc32(&out[12], out.size() - 12));
}

void FDD::serialize_woz(std::vector<uint8_t> &out)
{
    write_woz(m_woz_map, m_woz_tracks, write_protect, m_woz_timing, out);
}

emulator::Result FDD::load_woz(const std::string &file_name)
{
    const std::string data = dsk_tools::utf8_read_file(file_name);
    std::vector<uint8_t> f(data.begin(), data.end());
    // The drive keeps the disk it has unless the new one is accepted: the
    // parse below changes nothing before it knows the file is good
    if (f.size() > 64u * 1024 * 1024 || !parse_woz(f))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "File is in unknown format")) + "} " + file_name);
    m_generation++;
    return emulator::Result::ok();
}

emulator::Result FDD::save_woz(const std::string &file_name)
{
    std::vector<uint8_t> out;
    if (m_woz)
        serialize_woz(out);
    else {
        // A disk of bytes goes as the bits the Disk II reads from it, a track
        // on a whole track and a quarter either side, as a drive writes it
        if (!whole_track() || sides != 1 || tracks > 40)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Saving images for this type of drive is not supported yet.")) + "}");
        uint8_t map[WOZ_QUARTERS];
        memset(map, 0xFF, sizeof(map));
        std::vector<std::vector<uint8_t> > bits(tracks);
        std::vector<uint8_t> bytes;
        for (int t = 0; t < tracks; t++) {
            const HXC_MFM_TRACK_INFO &ti = track_indexes[t];
            if ((uint64_t)ti.mfmtrackoffset + ti.mfmtracksize > (uint64_t)disk_size) continue;
            bytes.assign(buffer + ti.mfmtrackoffset, buffer + ti.mfmtrackoffset + ti.mfmtracksize);
            nibbles_to_bits(bytes, bits[t]);
            for (int q = t * 4 - 1; q <= t * 4 + 1; q++) if (q >= 0) map[q] = (uint8_t)t;
        }
        write_woz(map, bits, write_protect, 32, out);
    }
    dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
    if (!file.is_open())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
    file.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
    return emulator::Result::ok();
}
