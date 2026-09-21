// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Minimal ZIP archive writer, source

#include "zip_writer.h"

#include <cstdlib>

#include "lodepng/lodepng.h"
#include "zip_reader.h"

namespace {

const uint32_t SIG_CENTRAL = 0x02014b50;
const uint32_t SIG_LOCAL   = 0x04034b50;
const uint32_t SIG_EOCD    = 0x06054b50;

void wr16(std::string &out, uint16_t v)
{
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}

void wr32(std::string &out, uint32_t v)
{
    wr16(out, static_cast<uint16_t>(v & 0xFFFF));
    wr16(out, static_cast<uint16_t>((v >> 16) & 0xFFFF));
}

} // namespace

void ZipWriter::add(const std::string &name, const uint8_t * data, size_t size, bool compress)
{
    Entry e;
    e.name = name;
    e.size = static_cast<uint32_t>(size);
    e.crc = size ? lodepng_crc32(data, size) : 0;

    if (compress && size > 0)
    {
        //Raw deflate, which is what method 8 stores. lodepng_zlib_compress()
        //would wrap it in a 2 byte header and a 4 byte Adler sum and produce
        //an entry no unpacker can read
        unsigned char * buf = nullptr;
        size_t buf_size = 0;
        const unsigned err = lodepng_deflate(&buf, &buf_size, data, size,
                                             &lodepng_default_compress_settings);
        if (err == 0 && buf_size < size)
        {
            e.method = 8;
            e.data.assign(buf, buf + buf_size);
        }
        free(buf);
    }

    //Stored, either because it was asked for or because deflating it did not
    //help - which is the common case for a small or already packed member
    if (e.method == 0 && size > 0) e.data.assign(data, data + size);

    m_entries.push_back(e);
}

void ZipWriter::add(const std::string &name, const std::string &text, bool compress)
{
    add(name, reinterpret_cast<const uint8_t *>(text.data()), text.size(), compress);
}

bool ZipWriter::build(std::string &out)
{
    out.clear();
    m_error.clear();

    //Both are ZIP64 territory, and the reader refuses ZIP64 outright
    if (m_entries.size() > 0xFFFE) return fail("too many entries for a ZIP archive");

    std::vector<uint32_t> offsets(m_entries.size(), 0);

    for (size_t i = 0; i < m_entries.size(); i++)
    {
        const Entry &e = m_entries[i];
        if (!ZipReader::is_safe_name(e.name)) return fail("unsafe name for a ZIP entry: " + e.name);
        if (e.name.size() > 0xFFFF) return fail("name is too long for a ZIP entry: " + e.name);
        //No ZIP64, so refuse rather than silently truncate a size. The
        //central directory has to stay addressable too, so the whole archive
        //is bounded, not just the entry
        if (static_cast<uint64_t>(e.data.size()) > 0xFFFFFFFFull
            || static_cast<uint64_t>(out.size()) + e.data.size() + e.name.size() + 30 > 0xFFFFFFFFull)
            return fail("entry is too large for a ZIP archive: " + e.name);

        offsets[i] = static_cast<uint32_t>(out.size());
        wr32(out, SIG_LOCAL);
        wr16(out, 20);                  //Version needed
        //Flags 0: no data descriptor. The whole archive is built in memory,
        //so every size is known before its header is written - which is the
        //reason to build it in memory at all
        wr16(out, 0);
        wr16(out, e.method);
        wr16(out, m_time);
        wr16(out, m_date);
        wr32(out, e.crc);
        wr32(out, static_cast<uint32_t>(e.data.size()));
        wr32(out, e.size);
        wr16(out, static_cast<uint16_t>(e.name.size()));
        wr16(out, 0);                   //Extra length
        out += e.name;
        out.append(reinterpret_cast<const char *>(e.data.data()), e.data.size());
    }

    const uint32_t cd_offset = static_cast<uint32_t>(out.size());
    for (size_t i = 0; i < m_entries.size(); i++)
    {
        const Entry &e = m_entries[i];
        wr32(out, SIG_CENTRAL);
        //Made by MS-DOS (OS byte 0), which is what makes the forward slashes
        //of the names mean what they say
        wr16(out, 0x0014);
        wr16(out, 20);                  //Version needed
        wr16(out, 0);                   //Flags
        wr16(out, e.method);
        wr16(out, m_time);
        wr16(out, m_date);
        wr32(out, e.crc);
        wr32(out, static_cast<uint32_t>(e.data.size()));
        wr32(out, e.size);
        wr16(out, static_cast<uint16_t>(e.name.size()));
        wr16(out, 0);                   //Extra length
        wr16(out, 0);                   //Comment length
        wr16(out, 0);                   //Disk number
        wr16(out, 0);                   //Internal attributes
        wr32(out, 0);                   //External attributes
        wr32(out, offsets[i]);          //Where the local header is
        out += e.name;
    }

    const uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_offset;
    wr32(out, SIG_EOCD);
    wr16(out, 0);                       //This disk
    wr16(out, 0);                       //Disk the central directory starts on
    wr16(out, static_cast<uint16_t>(m_entries.size()));
    wr16(out, static_cast<uint16_t>(m_entries.size()));
    wr32(out, cd_size);
    wr32(out, cd_offset);
    wr16(out, 0);                       //Comment length
    return true;
}
