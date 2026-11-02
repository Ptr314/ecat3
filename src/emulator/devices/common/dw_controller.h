// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: What the DW hard disk controllers share: the ДВК board and the Электроника МС 0585 one

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/hdd_image.h"

// Контроллеры семейства DW: ДВК (КЗД 3.057.316, dvk-dw) и Э-85 (3.858.232, e85-knmd).
// Регистры те же, буфер на один сектор, образ - секторы подряд. Здесь всё, что у них
// совпадает: образ и геометрия, отложенные события в микросекундах, побайтный доступ
// к регистрам словами, журнал обращений, поля, команды и снимок. У наследников -
// смысл регистров, запросы обмена и нумерация секторов.
//
// Параметры: heads, sectors, cylinders (без него - по размеру образа), small (перемычка
// накопителя 5 МБ, без него - по числу цилиндров), seek_time, step_time, word_time,
// init_time, в микросекундах.
class DWController: public AddressableDevice, public HddImageOwner
{
private:
    // Отладка: кольцо последних обращений к регистрам - чтение или запись,
    // регистр, значение; поле trace
    struct TraceEntry { uint16_t kind; uint16_t reg; uint16_t value; };
    TraceEntry m_trace[1024] = {};
    unsigned int m_trace_next = 0;
    void trace(unsigned int kind, unsigned int reg, unsigned int value);

    // Отложенное событие, в микросекундах
    unsigned int m_event = 0;
    uint64_t m_timeout = 0;
    uint64_t m_acc = 0;
    // «Слово готово» - своё, отдельно от операции: обращение к регистру данных
    // во время сброса, чтения или записи не должно её отменять
    uint64_t m_word_timeout = 0;
    uint64_t m_word_acc = 0;
    unsigned int m_seek_us = 0;
    unsigned int m_step_us = 0;
    unsigned int m_word_us = 0;
    unsigned int m_init_default;

    int m_small = -1;                   // перемычка «5 МБ»: -1 - по числу цилиндров

    emulator::Result attach(const std::string &file_name);

protected:
    static const unsigned int EV_NONE = 0;

    HddImage m_image;
    unsigned int m_cylinders = 0;
    unsigned int m_cfg_cylinders = 0;   // из конфигурации, 0 - по размеру образа
    unsigned int m_heads = 4;
    unsigned int m_sectors = 16;
    bool m_grows_image = false;         // образ короче накопителя дополняется нулями

    unsigned int m_err = 0;
    unsigned int m_sector = 0;
    unsigned int m_cyl = 0;
    unsigned int m_head = 0;
    unsigned int m_csr = 0;
    unsigned int m_track = 0;           // где стоят головки
    unsigned int m_command = 0;

    uint16_t m_buf[256] = {};

    bool m_word_pending = false;
    unsigned int m_init_us = 0;

    unsigned int m_reads = 0;
    unsigned int m_writes = 0;

    DWController(InterfaceManager *im, EmulatorConfigDevice *cd, unsigned int window, unsigned int init_us);

    void schedule(unsigned int event, unsigned int us);
    void schedule_word();
    // Событие из снимка, где слово лежало в общем событии
    void adopt_word_event();
    unsigned int pending_event() const { return m_event; }
    unsigned int seek_us(unsigned int cylinder) const;
    bool small_drive() const;

    // Параметры прочитаны: образ из конфигурации и первая начальная установка
    emulator::Result load_image();
    emulator::Result open_image(const std::string &file_name);

    virtual void init_controller() = 0;
    virtual void word_ready() = 0;
    virtual void event_done(unsigned int event) = 0;
    virtual bool geometry_ok() const = 0;
    virtual unsigned int reg_of(unsigned int address) const = 0;
    virtual unsigned int read_reg(unsigned int reg, bool peek) = 0;
    virtual void write_reg(unsigned int reg, unsigned int value) = 0;
    // Запись в нечётный байт регистра; true - на этом она и кончается
    virtual bool odd_byte_write(unsigned int reg) = 0;
    // Обращение, на которое контроллер не отвечает на шине
    virtual bool refused(MAYBE_UNUSED unsigned int address) { return false; }
    virtual bool trace_skips(MAYBE_UNUSED unsigned int reg) const { return false; }
    virtual void image_opened() {}

public:
    std::string files;
    HddImage & hdd_image() override { return m_image; }
    const std::string & hdd_files() const override { return files; }

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

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
