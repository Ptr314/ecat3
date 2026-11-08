// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК DW hard disk controller (КЗД 3.057.316)

#pragma once

#include "emulator/devices/common/dw_controller.h"
#include "emulator/devices/common/virq_line.h"

// Контроллер винчестера ДВК «DW» (КЗД 3.057.316). Сделан по плате Э-85, прошивка та
// же, поэтому регистры, команды и ход операций - общие (DWController, по ТО5 Э-85);
// здесь только то, чем отличается шина МПИ. Окно - 16 слов (174000-174037; второй
// контроллер ДВК-4 стоит на 174040), регистр - номер слова; РСНУ называется SI.
//
// Прерывания. Разряд 100 SI - разрешение, которого у платы Э-85 нет (там маскирует
// контроллер прерываний), а ЗОА и ЗОБ сведены через 2И-НЕ в одно прерывание с одним
// вектором (300, параметр vector). ЗОА и ЗОБ ставит прошивка, а снимает сама плата:
// ЗОБ - обращение к 174000 и 174010, ЗОА - к 174006 и 174016 (Alex_K по схеме КЗД).
// Подтверждение их не снимает, поэтому запрос - уровень: есть, пока разрешение
// стоит и поднят ЗОА или ЗОБ. Разрешение при уже поднятом разряде даёт прерывание сразу
// (на этом стоит проверка прерываний TESTDW: сброс, пауза, разрешение, ожидание), а
// обработчик, не тронувший регистров, вызывается снова. Начальная установка снимает
// разрешение.
//
// Пока контроллер занят, плата, как и у Э-85, отвечает только на SI (ловушка по
// вектору 4, DWController).
//
// Образ - как DW_System54.DSK эмулятора Патронова: блоки подряд, а на дорожке номер
// сектора на единицу больше блока, по модулю числа секторов (DW.SYS пишет сектор =
// (блок + 1) & 15, мониторы 279 и 134 грузят сектор 1 - блок 0). На диске номера с 0,
// как у Э-85; номера, которых на дорожке нет, не находятся. Геометрия - из
// конфигурации: heads (до 8, по умолчанию 4), sectors (до 16), cylinders.
class DVKDW: public DWController
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;

    unsigned int m_vector = 0300;
    bool m_ie = false;

    void update_irq();

protected:
    bool geometry_ok() const override;
    bool image_sector(unsigned int id, unsigned int &index) const override;
    unsigned int reg_of(unsigned int address) const override;
    void request_changed(unsigned int request, bool up) override;
    void bus_init() override;
    unsigned int status_bus_bits() const override;
    void status_written(unsigned int value) override;

public:
    DVKDW(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_dvk_dw(InterfaceManager *im, EmulatorConfigDevice *cd);
