// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НМД Электроники МС 0585 (плата 3.858.232, ТО5)

#pragma once

#include "emulator/devices/common/dw_controller.h"

// Контроллер винчестера: микропроцессор КМ1818ВМ01 с прошивкой, которой нет в
// дампах, поэтому плата сделана по регистрам ТО5. Окно 174000-174177, первая
// позиция магистрали:
//
//   174000  РИ      читается 000401; обращение снимает ЗОБ
//   174004  РОШПК   старший байт - ошибки, младший - запись (предкомпенсация)
//   174006  РАССЗ   сектор с нуля (0-4) и байт служебной зоны (8-15);
//                   обращение снимает ЗОА и ставит адрес буфера в начало
//   174010  РД      слово буфера; обращение снимает ЗОБ
//   174012  РАЦ     цилиндр, 10 разрядов
//   174014  РАП     поверхность, 3 разряда
//   174016  РКС2    младший байт - команда (020 на дорожку 0, 040 чтение, 060
//                   запись, 120 разметка), старший - состояние; обращение
//                   снимает ЗОА
//   174020  РСНУ    1 операция завершена, 10 сброс (запись), 200 запрос данных,
//                   400 тип накопителя, 100000 занят
//
// Регистр выбирается разрядами 1-3 адреса, РСНУ - разрядом 4. Пока идёт
// операция, отвечает только РСНУ: обращение к остальным даёт ловушку по вектору 4
// (п. 4.6.1). ЗОА идёт на линию А позиции (вектор 300), ЗОБ - на линию Б (304).
// Разрешения прерываний в контроллере нет: они маскируются в am9519.
//
// Образ - секторы подряд: ((цилиндр * головок + поверхность) * секторов + сектор)
// * 512, сектор с нуля, как в образах Xhomer. Физического чередования в образе нет,
// служебная зона не хранится. Накопитель на 4 головки не видит старший разряд
// выбора головки, а идентификаторы контроллер сравнивает с регистрами целиком:
// чтение поверхности 4 на такой дорожке 0 не находит адреса, а разметка
// поверхности 4 попадает на поверхность 0 и записывает в идентификаторы 4. Так
// установка ПРОС узнаёт число поверхностей, поэтому номер из идентификаторов
// хранится отдельно от образа, для дорожек, размеченных в этом сеансе.
class E85Knmd: public DWController
{
private:
    Interface i_irqa;
    Interface i_irqb;
    Interface i_init;

    MemoryMapper * m_mapper = nullptr;

    unsigned int m_status = 0;

    // Поверхность в идентификаторах дорожки: цилиндр * 8 + физическая головка
    enum { ID_TRACKS = 1024 * 8 };
    uint8_t m_id_head[ID_TRACKS];

    unsigned int m_pointer = 0;         // адрес буфера, словами
    unsigned int m_count = 0;           // слов передано в этой операции

    enum Op { OP_NONE = 0, OP_READ, OP_WRITE, OP_FORMAT };
    unsigned int m_op = OP_NONE;

    unsigned int m_format_us = 0;
    unsigned int m_formats = 0;
    unsigned int m_refused = 0;         // обращений во время операции

    void update_lines();
    void raise_done();
    void clear_done();
    void raise_data();
    void clear_data();
    void command(unsigned int cmd);
    bool drive_ready() const;
    void reset_ids();
    unsigned int physical_head() const;
    bool sector_offset(unsigned int sector, uint64_t &offset) const;
    void fail(unsigned int err);
    void finish();
    void read_done();
    void write_done();
    void format_done();

protected:
    void init_controller() override;
    void word_ready() override;
    void event_done(unsigned int event) override;
    bool geometry_ok() const override;
    unsigned int reg_of(unsigned int address) const override;
    unsigned int read_reg(unsigned int reg, bool peek) override;
    void write_reg(unsigned int reg, unsigned int value) override;
    bool odd_byte_write(unsigned int reg) override;
    bool refused(unsigned int address) override;
    bool trace_skips(unsigned int reg) const override;
    void image_opened() override;

public:
    E85Knmd(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_e85_knmd(InterfaceManager *im, EmulatorConfigDevice *cd);
