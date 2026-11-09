// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The DW hard disk controller: the board of the Электроника МС 0585 and its ДВК copy

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/hdd_image.h"

// Контроллер винчестера семейства DW: плата Э-85 (3.858.232, e85-knmd) и сделанный по
// ней КЗД ДВК (3.057.316, dvk-dw). Прошивка у них одна, различие только во внешней
// шине, поэтому здесь вся работа контроллера, по ТО5 Э-85: регистры, команды, буфер на
// сектор, ошибки, поиск сектора, идентификаторы дорожек, образ и времена, и отказ на
// шине, пока контроллер занят: тогда отвечает только РСНУ, остальное - тайм-аут и
// ловушка по вектору 4 (ТО5 п. 4.6.1; у КЗД ДВК так же, Alex_K). У наследников - шина:
// какой адрес какой регистр, как запросы ЗОА и ЗОБ становятся прерываниями, и как
// секторы дорожки лежат в образе.
//
//   +0   РИ      читается 000401; обращение снимает ЗОБ
//   +4   РОШПК   старший байт - ошибки (чистятся сбросом и каждой командой; разряды -
//                в dw_controller.cpp), младший - только запись (предкомпенсация)
//   +6   РАССЗ   сектор (0-4) и байт служебной зоны (8-15); обращение снимает ЗОА и
//                ставит адрес буфера в начало
//   +10  РД      слово буфера; обращение снимает ЗОБ, следующее слово готово через
//                word_time. Без команды буфер пишется и читается по кругу
//   +12  РАЦ     цилиндр, 10 разрядов
//   +14  РАП     поверхность, 3 разряда
//   +16  РКС2    младший байт - команда (020 на дорожку 0, 040 чтение, 060 запись,
//                120 разметка; значат только разряды 6-4), старший - 400 ошибка, 4000
//                запрос данных 2, 10000 установка завершена, 40000 накопитель готов;
//                обращение снимает ЗОА и ставит адрес буфера в начало
//   +20  РСНУ    1 операция завершена (ЗОА), 10 сброс (запись), 200 запрос данных 1
//                (ЗОБ), 400 накопитель 5 МБ, 100000 занят
//
// Сброс (линия init или разряд 10 РСНУ) - операция init_time с «Занят»: автотест
// прошивки и переход на дорожку 0, в конце ЗОА; без накопителя он не кончается.
// Сектор, которого нет, ищется два оборота (format_time - оборот), и чтение кончается,
// как найденное: ЗОБ и запрос данных с разрядами ошибки, ЗОА - когда буфер прочтут;
// запись - ЗОА с ошибкой. Адрес буфера - он же счётчик слов: когда он возвращается к
// началу, прошивка повторяет конец последней команды (снова пишет записанный сектор).
// Накопитель видит столько разрядов РАП, сколько нужно на его головки, а
// идентификаторы контроллер сравнивает с регистрами целиком: поверхность из
// идентификаторов хранится отдельно от образа, для дорожек, размеченных в этом сеансе.
//
// Параметры: heads, sectors, cylinders (без него - по размеру образа), small (перемычка
// накопителя 5 МБ, без него - по числу цилиндров), seek_time, step_time, word_time,
// init_time, format_time, в микросекундах. Образ размера известного накопителя
// (known_geometry()) приносит свою геометрию вместо heads, sectors и cylinders.
class DWController: public AddressableDevice, public HddImageOwner
{
private:
    // Отладка: кольцо последних обращений к регистрам - чтение или запись,
    // регистр, значение; поле trace
    struct TraceEntry { uint16_t kind; uint16_t reg; uint16_t value; };
    TraceEntry m_trace[1024] = {};
    unsigned int m_trace_next = 0;
    void trace(unsigned int kind, unsigned int reg, unsigned int value);

    Interface i_init;

    MemoryMapper * m_mapper = nullptr;
    unsigned int m_refused = 0;         // обращений, на которые занятая плата не ответила

    // Отложенное событие, в микросекундах
    unsigned int m_event = 0;
    uint64_t m_timeout = 0;
    uint64_t m_acc = 0;
    // «Слово готово» - своё, отдельно от операции: обращение к регистру данных
    // во время сброса, чтения или записи не должно её отменять
    uint64_t m_word_timeout = 0;
    uint64_t m_word_acc = 0;
    bool m_word_pending = false;
    unsigned int m_seek_us = 0;
    unsigned int m_step_us = 0;
    unsigned int m_word_us = 0;
    unsigned int m_init_us = 0;
    unsigned int m_rev_us = 0;          // оборот диска

    int m_small = -1;                   // перемычка «5 МБ»: -1 - по числу цилиндров

    unsigned int m_err = 0;
    unsigned int m_sector = 0;
    unsigned int m_cyl = 0;
    unsigned int m_head = 0;
    unsigned int m_csr = 0;
    unsigned int m_track = 0;           // где стоят головки
    unsigned int m_command = 0;
    unsigned int m_pointer = 0;         // адрес буфера, словами; он же счётчик слов

    uint16_t m_buf[256] = {};

    // Поверхность в идентификаторах дорожки: цилиндр * 8 + физическая головка
    enum { ID_TRACKS = 1024 * 8 };
    uint8_t m_id_head[ID_TRACKS];

    unsigned int m_reads = 0;
    unsigned int m_writes = 0;
    unsigned int m_formats = 0;

    emulator::Result attach(const std::string &file_name);
    void reset_ids();
    void schedule(unsigned int event, unsigned int us);
    void schedule_word();
    unsigned int seek_us(unsigned int cylinder) const;
    bool small_drive() const;
    void set_status(unsigned int status);
    void raise_done();
    void clear_done();
    void raise_data();
    void clear_data();
    void command(unsigned int cmd);
    bool refused(unsigned int address);
    void next_word();
    void csr_touched();
    void buffer_end();
    unsigned int physical_head() const;
    bool track_offset(uint64_t &offset) const;
    bool sector_found(uint64_t &offset) const;
    void finish();
    void fail(unsigned int err);
    void give_buffer();
    void complete(unsigned int err);
    void read_done();
    void write_done();
    void format_done();
    void event_done(unsigned int event);

protected:
    // Регистры - номер слова в окне
    enum Reg {
        DW_ID = 0, DW_ERR = 2, DW_SECTOR = 3, DW_DATA = 4, DW_CYL = 5,
        DW_HEAD = 6, DW_CSR = 7, DW_STATUS = 8, DW_NONE = 0xFF
    };
    // Разряды РСНУ
    static const unsigned int ST_DONE  = 0000001;
    static const unsigned int ST_INIT  = 0000010;
    static const unsigned int ST_DRQ1  = 0000200;
    static const unsigned int ST_SMALL = 0000400;
    static const unsigned int ST_BUSY  = 0100000;
    // Запросы обмена: ЗОА - операция завершена, ЗОБ - запрос данных
    enum Request { REQ_A = 0, REQ_B = 1 };
    // Что сделает прошивка, когда адрес буфера вернётся к началу: ничего, ЗОА (буфер
    // прочтён), запись или разметка. Остаётся и после конца команды. Номера - как в снимке
    enum Op { OP_NONE = 0, OP_READ, OP_WRITE, OP_FORMAT };
    enum Event { EV_NONE = 0, EV_RESTORE, EV_READ, EV_WRITE, EV_FORMAT, EV_READ_MISS, EV_WRITE_MISS, EV_INIT };

    HddImage m_image;
    unsigned int m_cylinders = 0;
    unsigned int m_cfg_cylinders = 0;   // из конфигурации, 0 - по размеру образа
    unsigned int m_cfg_heads = 4;
    unsigned int m_cfg_sectors = 16;
    unsigned int m_heads = 4;           // вставленного образа
    unsigned int m_sectors = 16;
    bool m_grows_image = false;         // образ короче накопителя дополняется нулями

    unsigned int m_status = 0;          // РСНУ без разрядов шины
    unsigned int m_op = OP_NONE;

    DWController(InterfaceManager *im, EmulatorConfigDevice *cd, unsigned int window);

    bool busy() const { return (m_status & ST_BUSY) != 0; }
    unsigned int pending_event() const { return m_event; }
    // Снимок прежнего вида: другое событие на месте того же отсчёта
    void set_pending_event(unsigned int event) { m_event = event; }
    // Снимок, где слово лежало в общем событии
    void adopt_word_event();
    void set_buffer_position(unsigned int pointer) { m_pointer = pointer & 255; }

    // Параметры прочитаны: образ из конфигурации и первая начальная установка
    emulator::Result load_image();
    emulator::Result open_image(const std::string &file_name);
    void init_controller();

    //--- Шина и образ: у каждой платы свои
    virtual bool geometry_ok() const = 0;
    // Накопитель, который узнаётся по размеру образа в байтах; false - геометрия из
    // параметров
    virtual bool known_geometry(MAYBE_UNUSED uint64_t size, MAYBE_UNUSED unsigned int &heads,
                                MAYBE_UNUSED unsigned int &cylinders, MAYBE_UNUSED unsigned int &sectors) const
        { return false; }
    // Место сектора с этим номером на дорожке образа; false - такого номера на дорожке нет
    virtual bool image_sector(unsigned int id, unsigned int &index) const
        { index = id; return id < m_sectors; }
    // Регистр по адресу, DW_NONE - не отвечает ничего
    virtual unsigned int reg_of(unsigned int address) const = 0;
    // ЗОА или ЗОБ поднялся или упал
    virtual void request_changed(unsigned int request, bool up) = 0;
    // Начальная установка платы, до снятия запросов
    virtual void bus_init() {}
    // Запись в РСНУ, до начальной установки, которую она может начать
    virtual void status_written(MAYBE_UNUSED unsigned int value) {}

    unsigned int read_reg(unsigned int reg, bool peek);
    void write_reg(unsigned int reg, unsigned int value);

public:
    std::string files;
    HddImage & hdd_image() override { return m_image; }
    const std::string & hdd_files() const override { return files; }

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
