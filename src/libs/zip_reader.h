// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Minimal read-only ZIP archive reader, header
//
// Reads the central directory of an archive held in memory and extracts its
// entries. Only what a packed configuration needs: methods 0 (stored) and 8
// (deflate, through lodepng_inflate, which every build already links), no
// ZIP64, no encryption, no spanning.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ZipReader
{
public:
    struct Entry {
        std::string name;           //As stored: '/' separated, relative
        uint16_t    method = 0;
        uint32_t    crc = 0;
        uint32_t    compressed_size = 0;
        uint32_t    size = 0;
        uint32_t    local_offset = 0;
        bool is_dir() const { return !name.empty() && name.back() == '/'; }
    };

    //Takes the archive bytes. False, with error() set, if they are not a
    //readable archive
    bool open(const std::string &data);

    const std::vector<Entry> & entries() const { return m_entries; }
    //Index of the entry with this exact name, -1 if none
    int find(const std::string &name) const;

    bool read(size_t index, std::vector<uint8_t> &out);
    bool read(const std::string &name, std::vector<uint8_t> &out);

    //False for a name that would land outside the directory it is unpacked
    //into: absolute, with a drive letter, or with a ".." component
    static bool is_safe_name(const std::string &name);

    const std::string & error() const { return m_error; }

    //Upper bound of one unpacked entry, against archives that inflate into
    //gigabytes
    static const size_t max_entry_size = 64u * 1024u * 1024u;

private:
    std::string         m_data;
    std::vector<Entry>  m_entries;
    std::string         m_error;

    bool fail(const std::string &message) { m_error = message; return false; }
};
