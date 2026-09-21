// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Minimal ZIP archive writer, header
//
// The mirror of zip_reader.h: builds an archive in memory from entries added
// one by one. Methods 0 (stored) and 8 (deflate, through lodepng_deflate,
// which every build already links - the emulator encodes PNG screenshots with
// the same encoder), no ZIP64, no encryption.
//
// Deterministic on purpose: the time stamp is fixed unless set, lodepng's
// deflate is a pure function of its input, and entries keep the order they
// were added in. Two archives built from the same bytes are byte-identical,
// which is what lets a saved state be a regression reference.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ZipWriter
{
public:
    //Adds one entry. compress false stores it as it is; with compress true
    //the deflated form is used only when it actually comes out smaller
    void add(const std::string &name, const uint8_t * data, size_t size, bool compress = true);
    void add(const std::string &name, const std::string &text, bool compress = true);

    //Builds the whole archive. False, with error() set, on a name that is not
    //safe (ZipReader::is_safe_name) or an entry that does not fit a 32-bit
    //size - this writer never produces what its reader would reject
    bool build(std::string &out);

    const std::string & error() const { return m_error; }

    //MS-DOS stamp every entry gets, 1980-01-01 00:00 by default
    void set_time(uint16_t dos_date, uint16_t dos_time) { m_date = dos_date; m_time = dos_time; }

    size_t count() const { return m_entries.size(); }

private:
    struct Entry {
        std::string          name;
        std::vector<uint8_t> data;      //Already in its stored form
        uint16_t             method = 0;
        uint32_t             crc = 0;
        uint32_t             size = 0;  //Uncompressed
    };

    std::vector<Entry> m_entries;
    std::string        m_error;
    uint16_t           m_date = 0x0021; //1980-01-01
    uint16_t           m_time = 0;

    bool fail(const std::string &message) { m_error = message; return false; }
};
