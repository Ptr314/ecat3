// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Minimal read-only ZIP archive reader, source

#include "zip_reader.h"

#include <cstdlib>
#include <cstring>

#include "lodepng/lodepng.h"

namespace {

const uint32_t SIG_EOCD    = 0x06054b50;
const uint32_t SIG_CENTRAL = 0x02014b50;
const uint32_t SIG_LOCAL   = 0x04034b50;

uint16_t rd16(const std::string &d, size_t p)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(d[p]) | (static_cast<uint8_t>(d[p + 1]) << 8));
}

uint32_t rd32(const std::string &d, size_t p)
{
    return static_cast<uint32_t>(rd16(d, p)) | (static_cast<uint32_t>(rd16(d, p + 2)) << 16);
}

} // namespace

bool ZipReader::open(const std::string &data)
{
    m_data = data;
    m_entries.clear();
    m_error.clear();

    //The end of central directory record is the last thing in the file,
    //followed only by a comment of up to 64K
    const size_t n = m_data.size();
    if (n < 22) return fail("not a ZIP archive");
    size_t eocd = std::string::npos;
    const size_t lowest = n > 22 + 0xFFFF ? n - 22 - 0xFFFF : 0;
    for (size_t p = n - 22 + 1; p-- > lowest; )
        if (rd32(m_data, p) == SIG_EOCD) { eocd = p; break; }
    if (eocd == std::string::npos) return fail("not a ZIP archive");

    const uint16_t count = rd16(m_data, eocd + 10);
    const uint32_t cd_size = rd32(m_data, eocd + 12);
    const uint32_t cd_offset = rd32(m_data, eocd + 16);
    if (count == 0xFFFF || cd_offset == 0xFFFFFFFF) return fail("ZIP64 archives are not supported");
    if (static_cast<uint64_t>(cd_offset) + cd_size > eocd) return fail("damaged ZIP archive");

    size_t p = cd_offset;
    for (unsigned i = 0; i < count; i++)
    {
        if (p + 46 > eocd || rd32(m_data, p) != SIG_CENTRAL) return fail("damaged ZIP archive");
        Entry e;
        const uint16_t flags = rd16(m_data, p + 8);
        e.method = rd16(m_data, p + 10);
        e.crc = rd32(m_data, p + 16);
        e.compressed_size = rd32(m_data, p + 20);
        e.size = rd32(m_data, p + 24);
        const uint16_t name_len = rd16(m_data, p + 28);
        const uint16_t extra_len = rd16(m_data, p + 30);
        const uint16_t comment_len = rd16(m_data, p + 32);
        e.local_offset = rd32(m_data, p + 42);
        if (p + 46 + name_len > eocd) return fail("damaged ZIP archive");
        e.name = m_data.substr(p + 46, name_len);
        if (flags & 1) return fail("encrypted ZIP entries are not supported: " + e.name);
        //Some packers write Windows separators
        for (size_t k = 0; k < e.name.size(); k++) if (e.name[k] == '\\') e.name[k] = '/';
        m_entries.push_back(e);
        p += 46 + name_len + extra_len + comment_len;
    }
    return true;
}

int ZipReader::find(const std::string &name) const
{
    for (size_t i = 0; i < m_entries.size(); i++)
        if (m_entries[i].name == name) return static_cast<int>(i);
    return -1;
}

bool ZipReader::read(const std::string &name, std::vector<uint8_t> &out)
{
    const int i = find(name);
    if (i < 0) return fail("no such entry in the archive: " + name);
    return read(static_cast<size_t>(i), out);
}

bool ZipReader::read(size_t index, std::vector<uint8_t> &out)
{
    out.clear();
    if (index >= m_entries.size()) return fail("no such entry in the archive");
    const Entry &e = m_entries[index];
    if (e.is_dir()) return true;
    if (e.size > max_entry_size) return fail("entry is too large: " + e.name);

    //Bounds are checked by subtraction: offsets come from the archive, and a
    //sum of two of them wraps around a 32-bit size_t
    const size_t p = e.local_offset;
    if (p > m_data.size() || m_data.size() - p < 30 || rd32(m_data, p) != SIG_LOCAL)
        return fail("damaged ZIP archive: " + e.name);
    //The local header has its own name and extra lengths, which may differ
    //from the central ones
    const size_t start = p + 30 + rd16(m_data, p + 26) + rd16(m_data, p + 28);
    if (start > m_data.size() || e.compressed_size > m_data.size() - start)
        return fail("damaged ZIP archive: " + e.name);
    const unsigned char * in = reinterpret_cast<const unsigned char *>(m_data.data()) + start;

    if (e.method == 0)
    {
        if (e.compressed_size != e.size) return fail("damaged ZIP archive: " + e.name);
        out.assign(in, in + e.size);
    }
    else if (e.method == 8)
    {
        LodePNGDecompressSettings settings;
        lodepng_decompress_settings_init(&settings);
        settings.max_output_size = max_entry_size;
        unsigned char * buf = nullptr;
        size_t buf_size = 0;
        const unsigned err = lodepng_inflate(&buf, &buf_size, in, e.compressed_size, &settings);
        if (err == 0) out.assign(buf, buf + buf_size);
        free(buf);
        if (err != 0) return fail("damaged ZIP archive: " + e.name);
    }
    else
        return fail("unsupported ZIP compression method: " + e.name);

    if (out.size() != e.size || lodepng_crc32(out.data(), out.size()) != e.crc)
    {
        out.clear();
        return fail("CRC error in ZIP archive: " + e.name);
    }
    return true;
}

bool ZipReader::is_safe_name(const std::string &name)
{
    if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= name.size())
    {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        if (name.compare(start, end - start, "..") == 0 && end - start == 2) return false;
        start = end + 1;
    }
    return true;
}
