// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Hard disk image file shared by the hard disk controllers

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "emulator/thread_compat.h"

class StateWriter;
class StateReader;

// Образ винчестера - файл, в который машина пишет прямо, как в свой диск.
// Отдельно от контроллера, потому что у всех контроллеров он один и тот же:
// открыть на запись (а нет - только на чтение), прочитать и записать сектор
// по смещению, запретить запись, писать в память вместо файла, отдать образ
// со всеми записями в снимок состояния и взять его оттуда.
//
// Меню окна вставляет и вынимает образ из потока GUI, пока машина читает
// сектор в потоке эмуляции, поэтому каждый обмен берёт замок.
class HddImage
{
public:
    static const unsigned int SECTOR_SIZE = 512;

    ~HddImage();

    // Открывает файл; при неудаче гнездо остаётся пустым, текст ошибки - в error
    bool open(const std::string &file_name, std::string &error);
    void close();

    bool attached() const { return m_file != nullptr; }
    const std::string & file_name() const { return m_file_name; }
    uint64_t size() const { return m_size; }
    bool is_protected() const { return m_read_only || m_write_protect; }

    void set_write_protect(bool on) { m_write_protect = on; }
    bool write_protect() const { return m_write_protect; }
    // Записи остаются в памяти, файл не меняется; выключение их забывает
    void set_volatile(bool on);
    bool is_volatile() const { return m_volatile; }

    bool read(uint64_t offset, uint8_t * buffer);
    bool write(uint64_t offset, const uint8_t * buffer);

    // Образ вместе с записанными секторами - в снимок под ключом key. Снимок
    // кладёт копию файла в свой архив; при восстановлении устройство
    // открывает эту копию и пишет уже только в память (set_volatile)
    void save_state(StateWriter &w, const char * key, const std::string &device_name);

private:
    std::FILE * m_file = nullptr;
    std::string m_file_name;
    uint64_t m_size = 0;
    bool m_read_only = true;
    bool m_write_protect = false;
    bool m_volatile = false;
    std::map<uint64_t, std::array<uint8_t, SECTOR_SIZE>> m_overlay;
    compat_mutex m_mutex;

    void close_locked();
};
