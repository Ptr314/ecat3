// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Hard disk image file shared by the hard disk controllers

#include <cstring>
#include <vector>

#include "hdd_image.h"
#include "emulator/state.h"
#include "dsk_tools/core.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути (см. uknc_hdd.cpp)
#include "libs/dsk_tools/src/utils.h"

#ifdef _WIN32
#include <cwchar>
#include <share.h>
#endif

HddImage::~HddImage()
{
    close();
}

// Имя файла в кодировке UTF-8: на Windows его надо перевести в UTF-16, иначе
// путь с кириллицей не откроется. И открывать без запрета совместного
// доступа: _wfopen на запись не пускает к файлу другие процессы, и
// параллельный прогон тестов на одном образе не открывал его даже на чтение
static std::FILE * open_file(const std::string &file_name, bool for_write)
{
#ifdef _WIN32
    std::wstring w = dsk_tools::utf8_to_wide(file_name);
    return _wfsopen(w.c_str(), for_write? L"r+b" : L"rb", _SH_DENYNO);
#else
    return std::fopen(file_name.c_str(), for_write? "r+b" : "rb");
#endif
}

bool HddImage::open(const std::string &file_name, std::string &error)
{
    close();

    bool read_only = false;
    std::FILE * f = open_file(file_name, true);
    if (f == nullptr) {
        read_only = true;
        f = open_file(file_name, false);
    }
    if (f == nullptr) {
        error = "not found";
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || (size % SECTOR_SIZE) != 0) {
        std::fclose(f);
        error = "size";
        return false;
    }

    compat_lock_guard lock(m_mutex);
    close_locked();
    m_file = f;
    m_file_name = file_name;
    m_size = (uint64_t)size;
    m_read_only = read_only;
    return true;
}

void HddImage::close_locked()
{
    if (m_file != nullptr) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_file_name.clear();
    m_size = 0;
    m_overlay.clear();
}

void HddImage::close()
{
    compat_lock_guard lock(m_mutex);
    close_locked();
}

void HddImage::set_volatile(bool on)
{
    compat_lock_guard lock(m_mutex);
    m_volatile = on;
    if (!on) m_overlay.clear();
}

bool HddImage::read(uint64_t offset, uint8_t * buffer)
{
    compat_lock_guard lock(m_mutex);
    if (m_file == nullptr || offset + SECTOR_SIZE > m_size || offset > 0x7FFFFFFFull) return false;
    const auto kept = m_overlay.find(offset);
    if (kept != m_overlay.end()) {
        memcpy(buffer, kept->second.data(), SECTOR_SIZE);
        return true;
    }
    if (std::fseek(m_file, (long)offset, SEEK_SET) != 0) return false;
    return std::fread(buffer, 1, SECTOR_SIZE, m_file) == SECTOR_SIZE;
}

bool HddImage::write(uint64_t offset, const uint8_t * buffer)
{
    compat_lock_guard lock(m_mutex);
    if (m_file == nullptr || m_write_protect || offset + SECTOR_SIZE > m_size) return false;
    if (m_volatile) {
        // Файл только на чтение этому не мешает: в него ничего не пишется
        memcpy(m_overlay[offset].data(), buffer, SECTOR_SIZE);
        return true;
    }
    if (m_read_only || offset > 0x7FFFFFFFull) return false;
    if (std::fseek(m_file, (long)offset, SEEK_SET) != 0) return false;
    if (std::fwrite(buffer, 1, SECTOR_SIZE, m_file) != SECTOR_SIZE) return false;
    std::fflush(m_file);
    return true;
}

void HddImage::save_state(StateWriter &w, const char * key, const std::string &device_name)
{
    //The image with the written sectors already merged in: byte for byte
    //what the guest sees, which the file on disk may never have had
    compat_lock_guard lock(m_mutex);
    if (m_file == nullptr || m_size == 0) return;
    std::vector<uint8_t> image(static_cast<size_t>(m_size), 0);
    std::fseek(m_file, 0, SEEK_SET);
    if (std::fread(image.data(), 1, image.size(), m_file) != image.size()) return;
    for (std::map<uint64_t, std::array<uint8_t, SECTOR_SIZE>>::const_iterator it = m_overlay.begin();
         it != m_overlay.end(); ++it)
        if (it->first + SECTOR_SIZE <= image.size())
            memcpy(image.data() + it->first, it->second.data(), SECTOR_SIZE);
    w.blob(key, dsk_tools::get_filename(m_file_name.empty() ? (device_name + ".img") : m_file_name),
           image.data(), image.size());
}
