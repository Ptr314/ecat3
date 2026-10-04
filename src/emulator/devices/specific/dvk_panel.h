// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК power panel: power-up sequence of АИП and АСП

#pragma once

#include "emulator/core.h"

// Пульт ДВК: включает блок питания и даёт магистрали разрешение пуска. После
// включения он ждёт, пока блок питания выйдет на режим (POWER GOOD), через
// 13 мс снимает АИП (авария источника питания, DCLO), ещё через 10 мс - АСП
// (авария сетевого напряжения, ACLO). Кнопка «Сброс» опускает АИП. Так
// устроена реплика пульта на Arduino (проект DVK_Control автора).
//
// Линии - уровни магистрали «нет аварии»: 1 - напряжение в норме, 0 - авария;
// с входами процессора ~dclo и ~aclo они соединяются напрямую.
//
// Два тумблера - переключатели машины на панели инструментов. «Пульт» держит
// линию ОСТ (активный ноль, на ~halt процессора): процессор уходит в
// пультовый режим и, пока тумблер включён, из него не выходит. «Прерывания»
// подают на линию ПРТ меандр 50 Гц, пока машина включена; по ней работает
// сетевой таймер (вход ~irq2 у ВМ1 и ВМ2, регистр LKS у МС1201.04).
//
// Пауза до POWER GOOD - это и время, за которое успевает приготовиться
// терминал: КСМ - отдельный блок, и без неё первое приглашение монитора
// приходило к нему раньше, чем его прошивка начинала принимать символы.
class DVKPanel: public ComputerDevice
{
private:
    enum Phase { PH_WAIT_PG, PH_WAIT_AIP, PH_WAIT_ASP, PH_ON, PH_RESET };

    Interface i_aip;            // АИП, 1 - нет аварии
    Interface i_asp;            // АСП, 1 - нет аварии
    Interface i_ost;            // ОСТ, 0 - тумблер «Пульт» включён
    Interface i_prt;            // ПРТ, меандр 50 Гц

    bool m_halt = false;        // тумблер «Пульт»
    bool m_prt = false;         // тумблер «Прерывания»
    unsigned int m_prt_half = 1;    // полпериода ПРТ в тактах
    unsigned int m_prt_ticks = 0;
    // Картинки тумблеров на панели инструментов (файлы машины)
    std::string m_halt_icon;
    std::string m_prt_icon;

    unsigned int m_pg_ticks = 1;
    unsigned int m_aip_ticks = 1;
    unsigned int m_asp_ticks = 1;
    unsigned int m_reset_ticks = 1;

    unsigned int m_phase = PH_ON;
    unsigned int m_ticks = 0;   // тактов в текущей фазе

    void set_phase(unsigned int phase);
    void update_prt();

public:
    DVKPanel(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_dvk_panel(InterfaceManager *im, EmulatorConfigDevice *cd);
