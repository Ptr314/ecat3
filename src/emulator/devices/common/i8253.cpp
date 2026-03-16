// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8253 (КР580ВИ53) programmable timer

#include "i8253.h"
#include "emulator/utils.h"

//Номера строк в таблице управления режимами
//Каждая строка соответствует своему событию
#define MODE_SET_OUT            0		//Значение OUT после установки режима (0, 1)
#define COUNTER_LOAD_DEC_VALUE  1		//Значение декремента (0=>1, 1=>2)
#define COUNTER_LOAD_AUTO_START 2       //Автозапуск после загрузки счетчика
#define COUNTER_START_OUT       3		//Значение OUT после запуска счета (0, 1, инверсия)
#define COUNTER_END_OUT         4		//Значение OUT после конца счета (0, 1, не влияет)
#define COUNTER_END_RESTART     5       //Авто-перезапуск счета
#define GATE_0_OUT              6		//Значение OUT если GATE=0
#define GATE_0_STOP             7		//Останов счета по GATE=0
#define GATE_01_OUT             8		//Значение OUT по положительному фронту GATE
#define GATE_01_RESET           9       //Перезапуск счета по положительному фронту GATE

//Добавляем по два лишних значения, чтобы было по 8 байт
const uint8_t I8253_MODES[10][8] =
                                    {{0, 1, 1, 1, 1, 1, 1, 1}, //MODE_SET_OUT
                                     {0, 0, 0, 1, 0, 0, 0, 1}, //COUNTER_LOAD_DEC_VALUE
                                     {1, 0, 1, 1, 1, 0, 1, 1}, //COUNTER_LOAD_AUTO_START
                                     {0, 0, 1, 2, 1, 1, 1, 2}, //COUNTER_START_OUT
                                     {1, 1, 0, 3, 0, 0, 0, 3}, //COUNTER_END_OUT
                                     {0, 0, 1, 1, 0, 0, 1, 1}, //COUNTER_END_RESTART
                                     {3, 3, 1, 1, 3, 3, 1, 1}, //GATE_0_OUT
                                     {1, 0, 1, 1, 1, 0, 1, 1}, //GATE_0_STOP
                                     {3, 0, 3, 3, 3, 3, 3, 3}, //GATE_01_OUT
                                     {0, 1, 1, 1, 0, 1, 1, 1}};//GATE_01_RESET

I8253::I8253(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 2, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_output(this, im, 3, "output", MODE_W)
    , i_gate(this, im, 3, "gate", MODE_R, 1)
    , per_channel_clock(false)
{
    for (int i = 0; i < 3; i++) {
        ch_clock_multiplier[i] = 1;
        ch_clock_divider[i] = 1;
        ch_clock_stored[i] = 0;
    }
    init();
    memset(&Gates, 1, sizeof(Gates)); //Allow counting just if gates are not connected
}

void I8253::load_config(SystemData *sd)
{
    AddressableDevice::load_config(sd);

    const QString param_names[3] = {"clock0", "clock1", "clock2"};
    for (int ch = 0; ch < 3; ch++) {
        QString s = cd->get_parameter(param_names[ch], false).value;
        if (!s.isEmpty()) {
            int pos = s.indexOf("/");
            if (pos > 0) {
                ch_clock_multiplier[ch] = parse_numeric_value(s.left(pos));
                ch_clock_divider[ch] = parse_numeric_value(s.right(s.length() - pos - 1));
            } else {
                ch_clock_multiplier[ch] = parse_numeric_value(s);
                ch_clock_divider[ch] = 1;
            }
            per_channel_clock = true;
        }
    }
}

void I8253::reset(const bool cold)
{
    AddressableDevice::reset(cold);
    init();
}

void I8253::init()
{
    memset(&Modes,       0, sizeof(Modes));
    memset(&IsBCD,       0, sizeof(IsBCD));
    memset(&Orders,      0, sizeof(Orders));
    memset(&Indexes,     0, sizeof(Indexes));
    memset(&Counting,    0, sizeof(Counting));
    memset(&StartData,   0, sizeof(StartData));
    memset(&Counters,    0, sizeof(Counters));
    memset(&NeedRestart, 0, sizeof(NeedRestart));
    memset(&Loaded,      0, sizeof(Loaded));
    memset(&ch_clock_stored, 0, sizeof(ch_clock_stored));
}

void I8253::StartCount(unsigned int A)
{
    SetOut(A, COUNTER_START_OUT);
    //Загружаем регистры счетчиков
    Reload(A);
    //Разрешаем счет
    if (Gates[A]==1) Counting[A] = 1;
}

void I8253::SetOut(unsigned int A, unsigned int Mode)
{
    const unsigned int M = I8253_MODES[Mode][Modes[A]];
    const unsigned int V = i_output.value;
    const unsigned int Mask = 1 << A;
    switch (M) {
        case 0: i_output.change(V & ~Mask); break; //0
        case 1: i_output.change(V | Mask);  break; //1
        case 2: i_output.change(V ^ Mask);  break; //Инверсия
        default: break;
    }
}

void I8253::Count(const unsigned int A, const unsigned int Increment)
{
    //Проверяем, загружен ли счетчик
    if (Loaded[A] == 1)
    {
        if (NeedRestart[A] == 1)
        {
            StartCount(A);
            NeedRestart[A] = 0;
        };
        if (Counting[A] == 1)
        {
            unsigned int I2 = Increment;
            //Умножаем значение на 2 для двойного декремента
            if (I8253_MODES[COUNTER_LOAD_DEC_VALUE][Modes[A]] == 1)
                I2 = I2 << 1;
            const int Ctr = static_cast<int>(Counters[A*2]) + static_cast<int>(Counters[A*2+1])*256;
            const int V = Ctr - static_cast<int>(I2);
            if ((V > 0) || (Ctr == 0))
            {
                //Сохраняем значение для следующего цикла
                Counters[A*2] = static_cast<uint8_t>(V);
                Counters[A*2+1] = static_cast<uint8_t>(V >> 8);
            } else {
                //Достигнут конец
                //Устанавливаем выход
                SetOut(A, COUNTER_END_OUT);
                //Запрещаем счет
                Counting[A] = 0;
                //Рестарт счета, если надо
                if (I8253_MODES[COUNTER_END_RESTART][Modes[A]] == 1)
                    NeedRestart[A] = 1;
            };
        };
    };
}

void I8253::Reload(unsigned int A)
{
    Counters[A*2] = StartData[A*2];
    Counters[A*2+1] = StartData[A*2+1];
}

unsigned int I8253::get_value(unsigned int address)
{
    const unsigned a = address & 0x03;
    unsigned result;
    if (a==3)
        result = 0;
    else {
        result = ReadData[a*2 + Indexes[a]];
        Indexes[a] = Indexes[a] ^ 0x01;
    }
    return result;
}

void I8253::set_value(const unsigned address, const unsigned value, bool force)
{
    const unsigned a = address & 0x03;
    if (a==3) {
        //control word
        unsigned int C = (value >> 6) & 0x03;				 	//Номер канала
        if ((value & 0x30) != 0)
        {
            IsBCD[C] = value & 0x01;  				//BCD-режим
            Orders[C] = (value >> 4) & 0x03; //Число байтов
            Modes[C] = (value >> 1) & 0x07;  //Режим работы канала
            //Обнуляем счетчик загруженных данных
            Loaded[C] = 0;
            Indexes[C] = 0;
            if (Orders[C] == 2) Indexes[C]++; //Только старший
            //Запрещаем счет
            Counting[C] = 0;
            //Устанавливаем OUT
            unsigned int V = I8253_MODES[MODE_SET_OUT][Modes[C]];
            i_output.change( (i_output.value & ~(1 << C)) | (V << C));
        } else {
            //Фиксация счетчиков для чтения
            ReadData[C*2] = Counters[C*2];
            Indexes[C] = 0;
            //Следующие строки надо включить если окажется,
            //что данные не всегда читаются по два, и нужно учесть влияние режима
            //if (Orders[C] == 2) Indexes[C]++;
        }
    } else {
        //counters
        StartData[a*2+Indexes[a]] = static_cast<uint8_t>(value);
        Indexes[a] = Indexes[a] ^ 0x01;
        //Если надо загрузить только один байт,
        //или уже загружено два, запускаем процесс
        if ((Orders[a] != 3) || (Indexes[a]==0))
        {
            Loaded[a] = 1;
            if (I8253_MODES[COUNTER_LOAD_AUTO_START][Modes[a]] != 0)
                NeedRestart[a] = 1;
        };
    }
}

void I8253::clock(const unsigned counter)
{
    if (!per_channel_clock) {
        Count(0, counter);
        Count(1, counter);
        Count(2, counter);
    } else {
        for (int ch = 0; ch < 3; ch++) {
            if (ch_clock_multiplier[ch] == ch_clock_divider[ch]) {
                Count(ch, counter);
            } else {
                ch_clock_stored[ch] += counter * ch_clock_multiplier[ch];
                unsigned int ch_clock = ch_clock_stored[ch] / ch_clock_divider[ch];
                if (ch_clock > 0) {
                    Count(ch, ch_clock);
                    ch_clock_stored[ch] -= ch_clock * ch_clock_divider[ch];
                }
            }
        }
    }
}

void I8253::interface_callback(MAYBE_UNUSED unsigned callback_id, const unsigned new_value, const unsigned old_value)
{
    for (unsigned int A=0; A<3; A++)
    {
        const unsigned G0 = (old_value >> A) & 0x01;
        const unsigned G1 = (new_value >> A) & 0x01;
        if (G1!=G0)
        {
            if (G1==0)
            {
                SetOut(A, GATE_0_OUT);
                if (I8253_MODES[GATE_0_STOP][Modes[A]] == 1) Counting[A] = 0;
            } else {
                //G1=1
                Counting[A] = 1;
                SetOut(A, GATE_01_OUT);
                if (I8253_MODES[GATE_01_RESET][Modes[A]] == 1) Reload(A);
            };
            Gates[A] = G1;
        };
    };
}

ComputerDevice * create_i8253(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8253(im, cd);
}