// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Юниор ФВ-6506: магнитофон с дистанционным управлением (лента .bt)

#pragma once

#include <vector>

#include "emulator/devices/common/tape.h"

// Магнитофон Юниора - не «звук в линию», а накопитель: ОС TCP/M обращается с
// кассетой как с дискетой. Машина сама ведет лентопротяжку («Маяк-231»)
// разрядами порта B дополнительного ВВ55, а данные идут через КР580ВВ51 в
// синхронном режиме: байты, а не полупериоды. Отсюда и устройство: лента -
// это последовательность записей с паузами между ними, а не звуковая дорожка.
//
// Формат .bt (снят с образов А. Морозова и сверен с ПЗУ):
//   [dword]                         - заголовок файла
//   [dword пауза][dword длина][AA AA 19 00][данные длиной "длина"]
//   ...
// Пауза считается в битовых интервалах, преамбула AA AA 19 00 - то, что ПЗУ
// шлет перед каждой записью (FB50), а данные начинаются с синхросимвола $E6,
// который ВВ51 ловит в режиме охоты и в буфер не кладет.
class UniorTape: public TapeRecorder
{
private:
    struct Record {
        uint32_t              gap = 0;  // Пауза перед записью, в битах
        // Те же две величины в единицах файла: пауза и длительность самой
        // записи. Хранятся, чтобы образ, который машина не трогала, ушел
        // обратно в файл байт в байт
        uint32_t              file_gap = 0;
        uint32_t              file_dur = 0;
        bool                  from_file = false;
        std::vector<uint8_t>  bytes;    // Преамбула вместе с данными
    };

    enum Transport { T_STOP, T_PLAY, T_RECORD, T_BACK, T_FORWARD };

    Interface i_control;        // Порт B дополнительного ВВ55: команды лентопротяжке
    Interface i_data_out;       // Байты в приемник ВВ51
    Interface i_data_in;        // Байты из передатчика ВВ51
    Interface i_ready;          // DSR ВВ51: лента идет (активный ноль)

    std::vector<Record> m_records;
    uint64_t m_total_bits = 0;
    // Сколько единиц файла приходится на один битовый интервал. У ленты из
    // файла считается по ней самой, у чистой кассеты берется из скорости
    double   m_unit_per_bit = 0.0;
    //Чистая кассета: записей нет, а лента есть. Ее длина - откуда ей взяться в
    //файле из одного заголовка - берется параметром, по умолчанию сторона С-60
    bool     m_blank = false;
    uint64_t m_blank_bits = 0;
    bool     m_dirty = false;   // Машина писала на кассету, а в файл это еще не легло

    Transport m_transport = T_STOP;
    uint64_t  m_pos = 0;        // Положение головки, в битах от начала ленты
    uint64_t  m_ticks = 0;      // Дробная часть такта
    unsigned int m_ticks_per_bit = 1;
    unsigned int m_fast_speed = 15;

    // Куда смотрит головка: номер записи и байт внутри нее. Пересчитывается
    // при перемотке, а при воспроизведении просто двигается вперед
    size_t   m_rec = 0;
    size_t   m_byte = 0;
    bool     m_in_gap = true;

    // Запись: байты копятся, пока машина не остановит лентопротяжку, и только
    // потом ложатся на ленту - так же, как это делает настоящая запись сектора
    std::vector<uint8_t> m_write_buf;
    uint64_t m_write_start = 0;
    uint64_t m_write_last = 0;  // Где головка была на последнем записанном байте
    uint64_t m_writes = 0;
    uint64_t m_bytes_read = 0;

    bool     m_carrier = false; // Головка внутри записи: для ВВ51 это DSR
    bool     m_half = false;    // Вторая половина битового интервала: слышимый манчестер
    int      m_bit_out = -1;    // Бит, который сейчас на линии, -1 - тишина
    uint8_t  m_write_byte = 0;  // Байт, который передатчик ВВ51 кладет на ленту
    unsigned int m_write_bits = 0;
    unsigned int m_sound_level = 0;
    uint64_t m_sound_edges = 0; // Перепадов в динамике: сценарию слышимость видна так

    static unsigned int tape_mode_of(Transport t);
    void set_carrier(bool on);
    emulator::Result parse_image(const uint8_t * raw, size_t size, const std::string &where);
    void build_image(std::vector<uint8_t> &out) const;
    int  current_bit() const;
    int  next_write_bit();
    void sound_level(unsigned int level);
    void locate(uint64_t pos);
    uint64_t record_start(size_t index) const;
    void advance_bit();
    void set_transport(Transport t);
    void commit_write();
    void rebuild_timeline();

public:
    UniorTape(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    emulator::Result load_file(const std::string &file_name, const std::string &fmt) override;
    void play() override;
    void stop() override;
    void rewind() override;
    int get_position() override;
    int get_total() override;
    int get_mode() override;
    void set_recording(bool recording) override;
    unsigned get_record_size() override;
    void get_save_data(std::vector<uint8_t> &out) override;
    std::string get_record_name() override;
    std::vector<uint8_t> * get_record_data() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_unior_tape(InterfaceManager *im, EmulatorConfigDevice *cd);
