// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК MY floppy controller board (КМД 3.057.136): the link between its own processor and the machine

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/virq_line.h"

// Контроллер MY (КМД 3.057.136) - плата со своим К1801ВМ1 (4 МГц), прошивкой
// 091 или 092 (ПЗУ 8 КБ с адреса 0), 4 КБ ОЗУ с 20000 и контроллером
// дисковода К1801ВП1-128 (bk-fdc) на 177130. Всё, что делает контроллер -
// чтение, запись, форматирование, чтение дорожки, - делает прошивка; это
// устройство - то, чем плата связана с машиной. Так плата сделана в MAME
// (dvk_kmd.cpp).
//
// Со стороны машины (диапазон без базы, 172140-172143):
//   172140 - MYCSR: 0 - пуск, 1-4 - команда, 5 - готово, 6 - разрешение
//            прерывания, 7 - запрос данных (TR), 8-13 - разряды адреса
//            16-21, 14 - сброс, 15 - ошибка
//   172142 - MYDR: адрес блока параметров или код ошибки; запись снимает TR
// Пуск запоминает команду, снимает «готово» и ждёт прошивку. Появление
// «готово» при разрешённом прерывании - запрос по вектору 170.
//
// Со стороны процессора платы (диапазоны с базой):
//   kmd[0100] - 177100/177102: те же регистры, прошивка ставит «готово»,
//               ошибку и TR и пишет MYDR
//   kmd[0110] - 177716: регистр режима платы, читается 010001
//   kmd[0400000] - 40000-77777: окно в память машины, 16 КБ. Разряды адреса
//               14-21 - младший байт MYDR: прошивка пишет туда разряды 14-15
//               адреса буфера и 16-21 из MYCSR (в MAME окно 16-разрядное и
//               берёт только разряды 0-1). На плате их держат две ВП1-095,
//               на шину разряды 16-21 выходят через перемычки S1-S6:
//               address_bits = _22 у машины с 22-разрядной шиной, _16 иначе
// INIT машины (~init) поднимает на плате прерывание IRQ2 (~local_init):
// прошивка по нему возвращается в исходное состояние.
class DVKKMD: public AddressableDevice
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_init;               // INIT машины
    Interface i_local_init;         // импульс на IRQ2 процессора платы

    MemoryMapper * m_host = nullptr;
    unsigned m_vector = 0170;
    uint32_t m_address_mask = 0177777;

    unsigned m_cr = 0;
    unsigned m_go = 0;
    unsigned m_dr = 0;
    bool m_pending = false;
    bool m_pulse = false;

    unsigned m_commands = 0;
    unsigned m_dma_reads = 0;
    unsigned m_dma_writes = 0;

    void update_irq();
    void set_done(bool done);
    uint32_t host_address(unsigned offset) const;

public:
    DVKKMD(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned get_direct(unsigned address) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_dvk_kmd(InterfaceManager *im, EmulatorConfigDevice *cd);
