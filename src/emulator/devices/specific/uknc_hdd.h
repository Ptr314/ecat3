// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ IDE hard disk controller, header

#pragma once

#include <array>
#include <cstdio>
#include <map>
#include <string>

#include "emulator/core.h"
#include "emulator/thread_compat.h"

// Контроллер винчестера УК-НЦ: плата ИДЕ в кассете ПЗУ периферийного
// процессора. Кассета отдаёт ему верхнюю половину своего окна - адреса
// 110000-117777, - а нижняя половина, 100000-107777, остаётся за ПЗУ платы,
// которое и умеет с этим контроллером работать.
//
// Регистр выбирается разрядами 1-3 адреса, причём в обратном коде, как их
// принимает магистраль: 110000 - состояние и команда, 110016 - данные.
//
//   110000  состояние (чтение) / команда (запись)
//   110002  номер головки и накопителя
//   110004  старший байт цилиндра
//   110006  младший байт цилиндра
//   110010  номер сектора
//   110012  число секторов
//   110014  ошибка
//   110016  данные
//
// Данные на магистрали 1801 инвертированы, и плата их не переворачивает:
// регистры машина читает в обратном коде, а сектор приходит так, как он
// лежит в накопителе. Поэтому дамп такого винчестера, снятый на обычной
// машине, оказывается инвертированным - образ с признаком inv эмулятор
// переворачивает обратно (как и UKNCBTL, признак определяется сам по
// первому сектору).
//
// Геометрия берётся из первого сектора образа: байт 0 - число секторов на
// дорожке, байт 1 - число головок; число цилиндров считается по размеру
// файла. Так размечают диск программы WD и ID, и так же поступает UKNCBTL.
//
// Адресуют накопитель двумя способами, и выбирает его сама программа разрядом
// 6 регистра 110002. Снятый - цилиндр, головка и сектор (сектора с единицы),
// взведённый - линейный номер в 28 разрядов, разложенный по тем же регистрам:
// 110010 - разряды 0-7, 110006 - 8-15, 110004 - 16-23, младшая тетрада 110002 -
// 24-27. Драйвер приезжает вместе с диском, а не лежит в ПЗУ платы, поэтому на
// одной и той же плате один образ работает по цилиндрам, а другой линейно.
class UKNCHDD: public AddressableDevice
{
private:
    std::FILE * m_file = nullptr;
    std::string m_file_name;
    bool m_attached = false;
    bool m_read_only = true;        // образ открыт только на чтение
    bool m_write_protect = false;   // защита, заданная конфигурацией или сценарием
    bool m_inverted = false;        // образ снят в обратном коде
    uint64_t m_image_size = 0;      // размер файла образа, байт

    // Запись в память: записанные сектора остаются здесь, а файл образа не
    // меняется - как у дисковода, который правит образ только в памяти.
    // Ключ - смещение сектора в образе. Выключение режима и смена образа
    // изменения забывают
    bool m_volatile = false;
    std::map<uint64_t, std::array<uint8_t, 512>> m_overlay;

    // Файл и m_overlay: меню окна меняет образ из потока GUI, пока машина
    // читает сектор в потоке эмуляции. Замок берётся раз на сектор
    compat_mutex m_image_mutex;
    void close_image();

    unsigned int m_cylinders = 0;
    unsigned int m_heads = 0;
    unsigned int m_sectors = 0;

    // Регистры контроллера. Здесь они в прямом коде, как у накопителя;
    // инверсию магистрали делает обмен с шиной
    unsigned int m_status = 0;
    unsigned int m_error = 0;
    unsigned int m_command = 0;
    unsigned int m_sectorcount = 0;
    unsigned int m_cursector = 0;
    unsigned int m_curcylinder = 0;
    unsigned int m_curhead = 0;
    unsigned int m_curheadreg = 0;

    uint8_t m_buffer[512];
    unsigned int m_bufferoffset = 0;

    // Отложенное окончание операции: накопитель отвечает не мгновенно, и ПЗУ
    // платы ждёт снятия разряда «занят»
    unsigned int m_event = 0;
    uint64_t m_timeout = 0;         // сколько микросекунд осталось до события
    uint64_t m_acc = 0;             // накопленные такты домена

    unsigned int m_sector_us = 0;   // время передачи сектора, мкс
    unsigned int m_seek_us = 0;     // время поиска дорожки, мкс

    unsigned int m_sectors_read = 0;
    unsigned int m_sectors_written = 0;

    unsigned int read_port(unsigned int reg, bool peek = false);
    void set_port(unsigned int reg, unsigned int value);
    void handle_command(unsigned int command);
    void identify_drive();
    bool lba_mode() const;
    uint64_t sector_offset() const;
    bool read_sector();
    bool write_sector();
    void read_sector_done();
    void write_sector_done();
    void next_sector();
    void continue_read();
    void continue_write();
    void schedule(unsigned int event, unsigned int us);
    void invert_buffer();

public:
    std::string files;

    UKNCHDD(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~UKNCHDD();

    emulator::Result load_config(SystemData *sd) override;
    emulator::Result load_image(const std::string &file_name);
    void unload();
    bool is_attached() const { return m_attached; }
    bool is_protected() const { return m_read_only || m_write_protect; }
    const std::string & image_name() const { return m_file_name; }

    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    ConfigFields get_config_fields() override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_uknc_hdd(InterfaceManager *im, EmulatorConfigDevice *cd);
