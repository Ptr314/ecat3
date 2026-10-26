// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: СМК-512 memory board of the БК (AltPro), header

#pragma once

#include "emulator/core.h"

// Плата СМК-512 (АльтПро), как ее делает ПЛИС-реплика: 512 КБ ОЗУ и ПЗУ BIOS
// 4 КБ в окне 100000-177777 БК0010-01 и БК0011М. Контроллер дисковода и
// винчестера на той же плате - отдельные устройства (bk-fdc, uknc-hdd).
//
// Память - страницы по 32 КБ из восьми сегментов по 4 КБ. Режим и страницу
// задает запись в 177130 (тот же адрес, что у регистра дисковода): сначала
// MOV #6, затем MOV режим+страница - значение защелкивается записью, идущей за
// младшей тетрадой 0110. Режим - разряды 4-6 (восемь режимов, см. таблицу в
// .cpp), страница - разряды 10, 2, 3 и 0. Разряд 2 любой записи в 177130
// запрещает чтение 177130 и 177132, пока следующая запись его не сбросит.
//
// Устройство стоит в диспетчере одним диапазоном 100000-177777 с routed = 1 и
// по каждому адресу само решает (route()), отвечает ли плата, остается ли
// адрес за памятью БК или никто не отвечает: плата не только перекрывает
// память машины, но и отключает ее - монитор БК0010, ПЗУ БОС и верхнее ОЗУ
// БК0011М. У реплики ее память на чтение перебивает ПЗУ и ОЗУ машины на тех же
// адресах, с регистрами ввода-вывода складывается по ИЛИ, а запись видят все.
//
// Параметры:
//   ram     - устройство ram на 512 КБ, память платы
//   rom     - устройство rom на 4 КБ, BIOS
//   machine - bk10 или bk11: какую память машины отключают сигналы платы
class SMK512: public AddressableDevice
{
private:
    Memory * m_ram = nullptr;
    Memory * m_rom = nullptr;
    bool m_bk11 = false;

    // Память платы читается и пишется прямо в буферах smk-ram и smk-rom: у
    // них нет обработчиков обращений, а плата сама следит за границами
    uint8_t * m_ram_buf = nullptr;
    const uint8_t * m_rom_buf = nullptr;

    // Смена режима меняет ответы route(), и диспетчер забывает разобранные
    // страницы (routing_changed())
    MemoryMapper * m_mapper = nullptr;

    unsigned int m_value = 0160;    // защелкнутые режим и страница
    unsigned int m_base = 0;        // первый сегмент 4 КБ страницы, из m_value
    bool m_strobe = false;          // предыдущая запись в 177130 была стробом 0110
    bool m_rd_off = false;          // чтение 177130/177132 запрещено разрядом 2

    unsigned int mode_index() const { return (m_value >> 4) & 7; }
    unsigned int page_base() const { return m_base; }
    static unsigned int base_of(unsigned int value);
    void set_register(unsigned int value);
    int write_source(unsigned int offset) const { return layout_write_source(m_value, offset); }
    int source(unsigned int offset) const { return layout_source(m_value, offset); }
    bool base_disabled(unsigned int segment) const { return machine_disabled(m_value, segment); }
    void write_177130(unsigned int value);
    unsigned int read_byte(unsigned int offset, bool direct);
    void write_byte(unsigned int offset, unsigned int value);

public:
    // Что стоит по смещению в окне 100000-177777: номер сегмента 4 КБ
    // страницы (0-7), ПЗУ платы или ничего
    static const int SRC_NONE = -1;
    static const int SRC_ROM = 8;

    // Раскладка окна для любого значения регистра (режим + страница), а не
    // только защелкнутого: по ним отладочное окно показывает другие режимы
    unsigned int latched() const { return m_value; }
    bool read_off() const { return m_rd_off; }
    static unsigned int page_of(unsigned int value) { return base_of(value) >> 3; }
    static int layout_source(unsigned int value, unsigned int offset);
    static int layout_write_source(unsigned int value, unsigned int offset);
    bool machine_disabled(unsigned int value, unsigned int segment) const;
    // Что из памяти самой машины отвечает по смещению в окне, если плата его
    // не занимает: nullptr, когда ее отключает сигнал платы или там ничего нет
    AddressableDevice * machine_device(unsigned int value, unsigned int offset);
    Memory * board_ram() const { return m_ram; }
    Memory * board_rom() const { return m_rom; }

    SMK512(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int route(unsigned int address, unsigned int mode) override;
    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    bool debug_writable(unsigned int address) const override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_smk512(InterfaceManager *im, EmulatorConfigDevice *cd);
