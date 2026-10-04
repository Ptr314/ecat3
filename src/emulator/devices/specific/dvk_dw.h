// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК DW hard disk controller (КЗД 3.057.316)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/hdd_image.h"
#include "emulator/devices/common/virq_line.h"

// Контроллер винчестера ДВК «DW» (КЗД 3.057.316): накопитель MFM, сектор 512
// байт, 16 секторов на дорожке, 4 головки. Регистры - как в MAME
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
// Прерывание - вектор 300, по появлению «операция закончена» или «слово
// готово» при разрешении; разрешение при уже поднятом разряде запроса не
// даёт. Сброс (INIT машины или разряд 10 SI) - тоже операция: накопитель
// уходит на дорожку 0 с «занят», а в конце поднимает «закончено». Номер сектора в регистре - с единицы: так его
// пишут DW.SYS (сектор = (блок + 1) & 15) и мониторы 279 и 134 (загрузка
// читает сектор 1 - блок 0). Образ - блоки подряд, блок = (цилиндр * 4 +
// головка) * 16 + сектор - 1, как DW_System54.DSK эмулятора Патронова.
class DVKDW: public AddressableDevice
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_init;

    HddImage m_image;
    unsigned int m_cylinders = 0;
    unsigned int m_heads = 4;
    unsigned int m_sectors = 16;
    unsigned int m_vector = 0300;

    unsigned int m_err = 0;
    unsigned int m_sector = 0;
    unsigned int m_cyl = 0;
    unsigned int m_head = 0;
    unsigned int m_csr = 0;
    unsigned int m_si = 0;
    unsigned int m_track = 0;       // где стоят головки
    unsigned int m_command = 0;

    uint16_t m_buf[256] = {};
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

    // Отложенное событие, в микросекундах
    unsigned int m_event = 0;
    uint64_t m_timeout = 0;
    uint64_t m_acc = 0;
    unsigned int m_seek_us = 0;
    unsigned int m_step_us = 0;
    unsigned int m_word_us = 0;
    unsigned int m_init_us = 0;

    unsigned int m_reads = 0;
    unsigned int m_writes = 0;

    // Отладка: кольцо последних обращений к регистрам - чтение или запись,
    // регистр, значение; поле trace
    struct TraceEntry { uint16_t kind; uint16_t reg; uint16_t value; };
    TraceEntry m_trace[1024] = {};
    unsigned int m_trace_next = 0;
    void trace(unsigned int kind, unsigned int reg, unsigned int value);

    void init_controller();
    void update_irq();
    void raise_drqa();
    void clear_drqa();
    void raise_drqb();
    void clear_drqb();
    void command(unsigned int cmd);
    void schedule(unsigned int event, unsigned int us);
    unsigned int seek_us(unsigned int cylinder) const;
    bool sector_offset(uint64_t &offset) const;
    void fail(unsigned int err);
    void read_done();
    void write_done();
    unsigned int read_reg(unsigned int reg, bool peek);
    void write_reg(unsigned int reg, unsigned int value);
    emulator::Result attach(const std::string &file_name);

public:
    std::string files;

    DVKDW(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned get_direct(unsigned address) override;

    ConfigFields get_config_fields() override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    bool state_owns_file(const std::string &parameter) const override
        { return parameter == "image"; }
};

ComputerDevice * create_dvk_dw(InterfaceManager *im, EmulatorConfigDevice *cd);
