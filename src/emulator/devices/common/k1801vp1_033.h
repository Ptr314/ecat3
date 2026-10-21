// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВП1-033 as a byte parallel interface, printer output (LP11)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/virq_line.h"

// КР1801ВП1-033 в режиме контроллера байтового параллельного интерфейса
// (BPIC, github.com/1801BM1/k1801, 033/doc/bpic.md), вместе с КР1801ВП1-034,
// на которой регистр данных. Режим и адреса задают выводы RC2-RC0; здесь
// только RC = 000 - выход на принтер, как LP11 DEC: 177514/177516, вектор 200.
//
//   +0  РСП  разряд 15 ERR (вход ERR), 7 TR - требование передачи,
//            6 IET - разрешение прерывания (пишется), 5 DONE (вход DONE)
//   +2  РИ   запись - байт принтеру
//
// Запись РИ защёлкивает байт (nOUT в ВП1-034) и выдаёт строб принтеру (nSC-A,
// здесь ~strobe = 1). Строб снимается, когда принтер снимает свой запрос (nAC-A,
// здесь ~request = 0) - взял байт. TR - «принтер просит и строба нет»; на
// МС1201.04 это внешние вентили D18.4, D38 на выводах REQ и TR. Запрос
// прерывания - TR при IET, однократный, как у DL11.
//
// Входы ERR и DONE на МС1201.04 к принтеру не идут (ERR на земле), здесь их
// разряды нулевые. Режимы ввода (RC = 001, 010, 011) не моделируются.
class K1801VP1_033: public AddressableDevice
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_init;
    // Линия к принтеру
    Interface i_data;                   // защёлка ВП1-034
    // nSC-A: 1 - строб. Слушатель у него один - принтер: он снимает запрос
    // прямо из change(), и строб снимается вложенным change(0), так что
    // второй слушатель получил бы устаревшую 1
    Interface i_strobe;
    Interface i_request;                // nAC-A: 1 - принтер просит байт

    unsigned int m_csr = 0;             // здесь только IET
    unsigned int m_data = 0;
    bool m_strobe = false;
    bool m_tr = false;                  // последнее значение TR, для фронта
    bool m_pending = false;
    unsigned int m_vector = 0200;

    bool request() const;
    void set_strobe(bool on);
    void update_tr();
    void update_irq();
    void init_registers();

public:
    K1801VP1_033(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_k1801vp1_033(InterfaceManager *im, EmulatorConfigDevice *cd);
