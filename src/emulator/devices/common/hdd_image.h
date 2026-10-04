// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Hard disk image file shared by the hard disk controllers

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
// Образ, которого нет в файловой системе процесса: в браузере - файл, выбранный
// посетителем на своём компьютере; он читается по кускам и в память целиком
// не попадает. Писать в него нельзя, записи остаются в памяти
class HddSource
{
public:
    virtual ~HddSource() {}
    virtual uint64_t size() const = 0;
    virtual bool read(uint64_t offset, uint8_t * buffer, size_t length) = 0;
};

class HddImage
{
public:
    // Источник по имени файла, или пустой, если имя обычное. Ставит фронтенд
    // (веб-версия); в настольной сборке не задан
    typedef std::unique_ptr<HddSource> (*SourceFactory)(const std::string &file_name);
    static SourceFactory source_factory;

    static const unsigned int SECTOR_SIZE = 512;

    ~HddImage();

    // Открывает файл; при неудаче гнездо остаётся пустым, текст ошибки - в error
    bool open(const std::string &file_name, std::string &error);
    void close();

    bool attached() const { return m_file != nullptr || m_source; }
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

    // Образ целиком, с записанными секторами
    bool contents(std::vector<uint8_t> &image);

    // Образ вместе с записанными секторами - в снимок под ключом key. Снимок
    // кладёт копию файла в свой архив; при восстановлении устройство
    // открывает эту копию и пишет уже только в память (set_volatile)
    void save_state(StateWriter &w, const char * key, const std::string &device_name);

private:
    std::FILE * m_file = nullptr;
    std::unique_ptr<HddSource> m_source;
    std::string m_file_name;
    uint64_t m_size = 0;
    bool m_read_only = true;
    bool m_write_protect = false;
    bool m_volatile = false;
    std::map<uint64_t, std::array<uint8_t, SECTOR_SIZE>> m_overlay;
    compat_mutex m_mutex;

    void close_locked();
    bool contents_locked(std::vector<uint8_t> &image);
};

// Контроллер с образом винчестера: через это веб-страница показывает образ
// и сохраняет его, не зная, какой это контроллер
class HddImageOwner
{
public:
    virtual ~HddImageOwner() {}
    virtual HddImage & hdd_image() = 0;
    // Фильтр файлов образа, как в параметре files
    virtual const std::string & hdd_files() const = 0;
};
