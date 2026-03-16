// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8253 (КР580ВИ53) programmable timer

#pragma once

#include "emulator/core.h"

class I8253:public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_output;
    Interface i_gate;

    uint8_t Modes[3];           //Режим работы счетчиков
    uint8_t IsBCD[3];           //Тип загруженных данных
    uint8_t Orders[3];          //Порядок загрузки данных
    uint8_t Indexes[3];         //Индесы загружаемых в счетчики байтов
    uint8_t Loaded[3];          //Загружены ли счетчики
    uint8_t Counting[3];        //Флаг останова для считывания счетчиков
    uint8_t StartData[6];       //Начальные данные
    uint8_t Counters[6];        //Текущие значения счетчиков
    uint8_t ReadData[6];        //Данные для считывания
    uint8_t Gates[3];           //Состояния входов GATE
    uint8_t NeedRestart[3];     //Старт счетчиков по следующему тактовому импульсу

    void init();
    void StartCount(unsigned int A);
    void SetOut(unsigned int A, unsigned int Mode);
    void Count(unsigned int A, unsigned int Increment);
    void Reload(unsigned int A);
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

public:
    I8253(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void clock(unsigned int counter) override;
};

ComputerDevice * create_i8253(InterfaceManager *im, EmulatorConfigDevice *cd);