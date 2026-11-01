// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НМД Электроники МС 0585 (плата 3.858.232, ТО5)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/hdd_image.h"

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
class E85Knmd: public AddressableDevice, public HddImageOwner
{
private:
    Interface i_irqa;
    Interface i_irqb;
    Interface i_init;

    MemoryMapper * m_mapper = nullptr;

    HddImage m_image;
    unsigned int m_cylinders = 0;
    unsigned int m_cfg_cylinders = 0;   // из конфигурации, 0 - по размеру образа
    unsigned int m_heads = 4;
    unsigned int m_sectors = 16;
    int m_small = -1;                   // тип накопителя: -1 - по числу цилиндров

    unsigned int m_err = 0;
    unsigned int m_sector = 0;
    unsigned int m_cyl = 0;
    unsigned int m_head = 0;
    unsigned int m_csr = 0;
    unsigned int m_status = 0;
    unsigned int m_track = 0;           // где стоят головки
    unsigned int m_command = 0;

    // Поверхность в идентификаторах дорожки: цилиндр * 8 + физическая головка
    enum { ID_TRACKS = 1024 * 8 };
    uint8_t m_id_head[ID_TRACKS];

    uint16_t m_buf[256] = {};
    unsigned int m_pointer = 0;         // адрес буфера, словами
    unsigned int m_count = 0;           // слов передано в этой операции

    enum Op { OP_NONE = 0, OP_READ, OP_WRITE, OP_FORMAT };
    unsigned int m_op = OP_NONE;

    // Отложенное событие, в микросекундах
    unsigned int m_event = 0;
    uint64_t m_timeout = 0;
    uint64_t m_acc = 0;
    // «Слово готово» - своё, отдельно от операции
    bool m_word_pending = false;
    uint64_t m_word_timeout = 0;
    uint64_t m_word_acc = 0;
    unsigned int m_seek_us = 0;
    unsigned int m_step_us = 0;
    unsigned int m_word_us = 0;
    unsigned int m_init_us = 0;
    unsigned int m_format_us = 0;

    unsigned int m_reads = 0;
    unsigned int m_writes = 0;
    unsigned int m_formats = 0;
    unsigned int m_refused = 0;         // обращений во время операции

    // Отладка: кольцо последних обращений к регистрам - чтение или запись,
    // регистр, значение; поле trace
    struct TraceEntry { uint16_t kind; uint16_t reg; uint16_t value; };
    TraceEntry m_trace[1024] = {};
    unsigned int m_trace_next = 0;
    void trace(unsigned int kind, unsigned int reg, unsigned int value);

    void init_controller();
    void update_lines();
    void raise_done();
    void clear_done();
    void raise_data();
    void clear_data();
    void command(unsigned int cmd);
    void schedule(unsigned int event, unsigned int us);
    void schedule_word();
    unsigned int seek_us(unsigned int cylinder) const;
    bool drive_ready() const;
    void reset_ids();
    unsigned int physical_head() const;
    bool sector_offset(unsigned int sector, uint64_t &offset) const;
    void fail(unsigned int err);
    void finish();
    void read_done();
    void write_done();
    void format_done();
    bool refused(unsigned int address);
    unsigned int read_reg(unsigned int reg, bool peek);
    void write_reg(unsigned int reg, unsigned int value);
    emulator::Result open_image(const std::string &file_name);
    emulator::Result attach(const std::string &file_name);

public:
    std::string files;
    HddImage & hdd_image() override { return m_image; }
    const std::string & hdd_files() const override { return files; }

    E85Knmd(InterfaceManager *im, EmulatorConfigDevice *cd);

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

ComputerDevice * create_e85_knmd(InterfaceManager *im, EmulatorConfigDevice *cd);
