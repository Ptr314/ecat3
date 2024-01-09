#ifndef I8253_H
#define I8253_H

#include "emulator/core.h"

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

class I8253:public AddressableDevice
{
private:
    Interface * i_address;
    Interface * i_data;
    Interface * i_output;
    Interface * i_gate;


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

protected:
    void init(){
        memset(&Modes,       0, sizeof(Modes));
        memset(&IsBCD,       0, sizeof(IsBCD));
        memset(&Orders,      0, sizeof(Orders));
        memset(&Indexes,     0, sizeof(Indexes));
        memset(&Counting,    0, sizeof(Counting));
        memset(&StartData,   0, sizeof(StartData));
        memset(&Counters,    0, sizeof(Counters));
        memset(&NeedRestart, 0, sizeof(NeedRestart));
        memset(&Loaded,      0, sizeof(Loaded));
    }

    void StartCount(unsigned int A)
    {
        SetOut(A, COUNTER_START_OUT);
        //Загружаем регистры счетчиков
        Reload(A);
        //Разрешаем счет
        if (Gates[A]==1) Counting[A] = 1;
    }

    void SetOut(unsigned int A, unsigned int Mode)
    {
        unsigned int M = I8253_MODES[Mode][Modes[A]];
        unsigned int V = i_output->value;
        unsigned int Mask = 1 << A;
        switch (M) {
        case 0: i_output->change(V & ~Mask); break; //0
        case 1: i_output->change(V | Mask);  break; //1
        case 2: i_output->change(V ^ Mask);  break; //Инверсия
        }
    }

    void Count(unsigned int A, unsigned int Increment)
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
                int Ctr = static_cast<int>(Counters[A*2]) + static_cast<int>(Counters[A*2+1])*256;
                int V = Ctr - static_cast<int>(I2);
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

    void Reload(unsigned int A)
    {
        Counters[A*2] = StartData[A*2];
        Counters[A*2+1] = StartData[A*2+1];
    }

public:


    I8253(InterfaceManager *im, EmulatorConfigDevice *cd):
        AddressableDevice(im, cd)
    {
        i_address = create_interface(2, "address", MODE_R);
        i_data =    create_interface(8, "data", MODE_R);
        i_output =  create_interface(3, "output", MODE_W);
        i_gate =    create_interface(3, "gate", MODE_R, 1);

        init();
        memset(&Gates, 1, sizeof(Gates)); //Allow counting just if gates are not connected
    }

    virtual void reset(bool cold) override
    {
        AddressableDevice::reset(cold);
        init();
    }

    virtual unsigned int get_value(unsigned int address) override
    {
        unsigned int a = address & 0x03;
        unsigned int result;
        if (a==3)
            result = 0;
        else {
            result = ReadData[a*2 + Indexes[a]];
            Indexes[a] = Indexes[a] ^ 0x01;
        }
        return result;
    }

    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override
    {
        unsigned int a = address & 0x03;
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
                i_output->change( (i_output->value & ~(1 << C)) | (V << C));
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

    virtual void clock(unsigned int counter) override
    {
        Count(0, counter);
        Count(1, counter);
        Count(2, counter);
    }

private:
    void interface_callback([[maybe_unused]] unsigned int callback_id, unsigned int new_value, unsigned int old_value) override
    {
        for (unsigned int A=0; A<3; A++)
        {
            unsigned int G0 = (old_value >> A) & 0x01;
            unsigned int G1 = (new_value >> A) & 0x01;
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
};

ComputerDevice * create_i8253(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8253(im, cd);
}


#endif // I8253_H
