// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НМД Электроники МС 0585 (плата 3.858.232, ТО5)

#pragma once

#include "emulator/devices/common/dw_controller.h"

// Контроллер винчестера Э-85: микропроцессор КМ1818ВМ01 с прошивкой, которой нет в
// дампах, поэтому плата сделана по регистрам ТО5 (вся её работа - DWController, общий с
// КЗД ДВК, у которого та же прошивка). Здесь шина CTI: окно 174000-174177, первая
// позиция магистрали; регистр выбирается разрядами 1-3 адреса, РСНУ - разрядом 4.
// Пока идёт операция, отвечает только РСНУ (п. 4.6.1, DWController). ЗОА идёт на линию
// А позиции (вектор 300), ЗОБ - на линию Б (304); разрешения прерываний в контроллере
// нет, они маскируются в am9519.
//
// Образ - секторы подряд: ((цилиндр * головок + поверхность) * секторов + сектор)
// * 512, сектор с нуля, как в образах Xhomer. Физического чередования в образе нет,
// служебная зона не хранится. Образ размера известного накопителя DEC Pro несёт свою
// геометрию, а чистый (короче накопителя) дорастает до заданной.
class E85Knmd: public DWController
{
private:
    Interface i_irqa;
    Interface i_irqb;

protected:
    bool geometry_ok() const override;
    bool known_geometry(uint64_t size, unsigned int &heads, unsigned int &cylinders, unsigned int &sectors) const override;
    unsigned int reg_of(unsigned int address) const override;
    void request_changed(unsigned int request, bool up) override;
    void bus_init() override;

public:
    E85Knmd(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
};

ComputerDevice * create_e85_knmd(InterfaceManager *im, EmulatorConfigDevice *cd);
