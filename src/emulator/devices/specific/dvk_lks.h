// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Line clock with the LKS register (177546), МС1201.04

#pragma once

#include "emulator/core.h"

// Сетевой таймер платы МС1201.04 с регистром LKS (как KW11-L DEC):
//   разряд 6 - разрешение прерывания (запись и чтение),
//   разряд 7 - тик был (ставится каждым импульсом, снимается записью 0).
// Импульс при разрешённом прерывании уходит на вход EVNT процессора
// (~irq, вектор 100, уровень 6). После сброса прерывание запрещено, а
// разряд 7 взведён - так у реплики платы (mc1201-04.v).
//
// Частоту у настоящей платы даёт линия ПРТ магистрали (от пульта, тумблер
// «Прерывания»): с frequency = 0 свой генератор молчит, а тик - нарастающий
// фронт на ~prt.
class DVKLKS: public AddressableDevice
{
private:
    Interface i_irq;            // импульс запроса, положительный
    Interface i_init;           // INIT магистрали
    Interface i_prt;            // ПРТ магистрали, при frequency = 0

    bool m_ie = false;
    bool m_monitor = true;
    unsigned int m_period = 1;
    unsigned int m_ticks = 0;
    bool m_pulse = false;
    unsigned int m_count = 0;
    bool m_internal = true;     // свой генератор (frequency не 0)

    void tick();

public:
    DVKLKS(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_dvk_lks(InterfaceManager *im, EmulatorConfigDevice *cd);
