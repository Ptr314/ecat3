// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: А16М floppy controller memory of the БК (AltPro), header

#pragma once

#include "emulator/core.h"

// Память дискового контроллера А16М (АльтПро): 16 КБ ОЗУ четырьмя сегментами
// по 4 КБ и ПЗУ драйвера 4 КБ в окне 100000-177777 БК0010-01 и БК0011М. Сам
// контроллер дисковода - отдельное устройство bk-fdc на тех же 177130-177133.
//
// Режим задает запись в 177130, как у СМК: MOV #6, затем MOV код режима, затем
// MOV #0. Строб у оригинала - разряды 1 и 2 (тест gid проверяет это записью
// #7); режим - разряды 4-6, восемь режимов (таблица в .cpp). Разряд 2 любой
// записи в 177130 запрещает чтение 177130 и 177132, разряд 3 на БК0010-01
// подключает ПЗУ БЕЙСИКа (линии BAS1/BAS2), пока следующая запись его не
// сбросит. БЕЙСИК выбирает диспетчер по выходу basic: в конфигурации он
// заведен на разряд его ~config.
//
// Устройство стоит в диспетчере одним диапазоном 100000-177777 с routed = 1 и
// по каждому адресу само решает (route()), отвечает ли плата, остается ли
// адрес за памятью БК или никто не отвечает: плата не только перекрывает
// память машины, но и отключает ее - монитор БК0010, ПЗУ БОС и верхнее ОЗУ
// БК0011М. На чтение память платы перебивает ОЗУ БК0011М, а с ПЗУ БОС
// складывается по ИЛИ; запись в ОЗУ платы получает и ОЗУ машины под ней.
//
// Параметры:
//   ram     - устройство ram на 16 КБ, память платы
//   rom     - устройство rom на 4 КБ, ПЗУ драйвера
//   machine - bk10 или bk11: какую память машины отключают сигналы платы
class A16M: public AddressableDevice
{
private:
    Interface i_basic;              // 1 - ПЗУ БЕЙСИКа БК0010-01 подключено (разряд 3)

    Memory * m_ram = nullptr;
    Memory * m_rom = nullptr;
    bool m_bk11 = false;

    // Память платы читается и пишется прямо в буферах: у ram и rom нет
    // обработчиков обращений, а плата сама следит за границами
    uint8_t * m_ram_buf = nullptr;
    const uint8_t * m_rom_buf = nullptr;

    // Смена режима меняет ответы route(), и диспетчер забывает разобранные
    // страницы (routing_changed())
    MemoryMapper * m_mapper = nullptr;

    unsigned int m_value = 0160;    // защелкнутый код режима
    bool m_strobe = false;          // предыдущая запись в 177130 была стробом
    bool m_rd_off = false;          // чтение 177130/177132 запрещено разрядом 2
    bool m_basic = false;           // разряд 3 последней записи: БЕЙСИК БК0010-01

    unsigned int mode_index() const { return (m_value >> 4) & 7; }
    void set_register(unsigned int value);
    void write_177130(unsigned int value);
    int read_source(unsigned int offset) const;
    int write_source(unsigned int offset) const;
    bool base_disabled(unsigned int segment) const;
    unsigned int read_byte(unsigned int offset, bool direct);
    void write_byte(unsigned int offset, unsigned int value);

public:
    A16M(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int route(unsigned int address, unsigned int mode) override;
    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_a16m(InterfaceManager *im, EmulatorConfigDevice *cd);
