// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК DW hard disk controller (КЗД 3.057.316)

#pragma once

#include "emulator/devices/common/dw_controller.h"
#include "emulator/devices/common/virq_line.h"

// Контроллер винчестера ДВК «DW» (КЗД 3.057.316): накопитель MFM, сектор 512
// байт. Геометрия - из конфигурации: heads (до 8, по умолчанию 4), sectors (до
// 16, по умолчанию 16), cylinders (до 1024; без него - по размеру образа).
// Адрес окна и вектор - тоже: второй контроллер ДВК-4 стоит на 174040 с
// вектором 310. Регистры - как в MAME
// (dvk_dwhle.cpp), обмен словами через регистр данных, без прямого доступа.
//
//   174000  ID      читается 000401
//   174004  ERR     старший байт - ошибки: 400 метка данных, 1000 дорожка 0,
//                   2000 отказ (неизвестная команда), 10000 нет адресной метки,
//                   20000 и 40000 - контрольный код; младший - запись
//   174006  SECTOR  номер сектора, с 1 (16-й пишется как 0)
//   174010  DATA    слово буфера сектора
//   174012  CYL     цилиндр, 10 разрядов
//   174014  HEAD    головка, 3 разряда
//   174016  CSR     младший байт - команда (20 - на дорожку 0, 40 - чтение,
//                   60 - запись, 120 - разметка дорожки); старший - 400
//                   ошибка, 4000 запрос данных, 10000 готов, 40000 накопитель
//                   готов
//   174020  SI      1 - операция закончена, 10 - сброс (запись), 100 -
//                   разрешение прерывания, 200 - слово данных готово, 400 -
//                   накопитель 5 МБ (153 цилиндра; без него DW.SYS считает
//                   диск 10 МБ, 306 цилиндров), 100000 - занят
//
// Регистр данных без команды чтения или записи - просто буфер контроллера на
// 256 слов, который пишется и читается по кругу (в MAME 256-е слово, записанное
// без команды, уходило на диск).
//
// Прерывание - вектор 300 (параметр vector), по появлению «операция закончена» или «слово
// готово» при разрешении; разрешение при уже поднятом разряде запроса не
// даёт. Сброс (INIT машины или разряд 10 SI) - тоже операция: накопитель
// уходит на дорожку 0 с «занят», а в конце поднимает «закончено». Номер сектора в регистре - с единицы: так его
// пишут DW.SYS (сектор = (блок + 1) & 15) и мониторы 279 и 134 (загрузка
// читает сектор 1 - блок 0). Образ - блоки подряд, блок = (цилиндр * головок
// + головка) * секторов + сектор - 1, как DW_System54.DSK эмулятора Патронова
// (4 головки, 16 секторов).
class DVKDW: public DWController
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_init;

    unsigned int m_vector = 0300;
    unsigned int m_si = 0;

    unsigned int m_count_read = 0;
    unsigned int m_count_write = 0;
    bool m_format = false;          // запись ведёт разметку дорожки
    // Чем занят буфер: без команды регистр данных только пишет и читает его
    // по кругу (проверка буфера TESTDW), с командой - передаёт сектор
    enum Op { OP_NONE = 0, OP_READ, OP_WRITE };
    unsigned int m_op = OP_NONE;

    // Запросы прерывания: «операция закончена» и «слово готово»
    bool m_drqa = false;
    bool m_drqb = false;

    void update_irq();
    void raise_drqa();
    void clear_drqa();
    void raise_drqb();
    void clear_drqb();
    void command(unsigned int cmd);
    bool sector_offset(uint64_t &offset) const;
    void fail(unsigned int err);
    void read_done();
    void write_done();

protected:
    void init_controller() override;
    void word_ready() override;
    void event_done(unsigned int event) override;
    bool geometry_ok() const override;
    unsigned int reg_of(unsigned int address) const override;
    unsigned int read_reg(unsigned int reg, bool peek) override;
    void write_reg(unsigned int reg, unsigned int value) override;
    bool odd_byte_write(unsigned int reg) override;

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
