// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder device

#pragma once

#include <functional>

#include "emulator/core.h"
#include "emulator/devices/common/speaker.h"
#include "emulator/devices/common/tape_bk.h"
#include "emulator/devices/common/tape_bt.h"
#include "emulator/devices/common/tape_record.h"
#include "emulator/devices/common/tape_rk86.h"
#include "emulator/devices/common/tape_uknc.h"
#include "emulator/devices/common/tape_zx.h"

#define TAPE_STOPPED 0
#define TAPE_READ    1
#define TAPE_WRITE   0
//Перемотка: у ленты, которую ведет сама машина, в окне должны нажиматься
//клавиши перемотки, а не воспроизведения
#define TAPE_FORWARD 2
#define TAPE_BACK    3

enum class TapeEnc {
    MSX,
    RK86,
    UKNC,
    BK,
    //Лента Юниора несет байты, а не полупериоды: модуляция у машины
    //аппаратная, и кодировщики полупериодов для нее не работают
    UNIOR,
    ZX
};

// Чем записана лента. Само устройство одно на все машины: и лентопротяжка, и
// таймлайн записей с паузами, и перемотка, и чистая кассета у них общие, а
// носитель говорит лишь о том, что лежит в записи и куда оно уходит
enum class TapeMedium {
    Levels,     // Уровни линии, по выборке на интервал: машина считает перепады
    Bytes,      // Байты: модуляция аппаратная, данные идут через ВВ51
    Pulses      // Полупериоды по запросу из исходного файла: лента Спектрума
};

enum TapeTransport { T_STOP, T_PLAY, T_RECORD, T_BACK, T_FORWARD };

enum class TapeWriterState {
    Measuring,
    Preamble,
    Data,
    Stops
};

class TapeRecorder: public ComputerDevice
{
private:
    Interface i_input;
    Interface i_output;
    Interface i_speaker;
    Interface i_motor;

    // Лента-накопитель: лентопротяжкой командует машина, а данные идут байтами
    // через ВВ51. Неподключенные линии ничего не стоят, поэтому они есть у
    // всякой ленты, а не только у той, которой они нужны
    Interface i_control;
    Interface i_data_out;
    Interface i_data_in;
    Interface i_ready;

    Speaker * speaker;
    EmulatorConfigDevice * speaker_config;  //The speaker reads it in load_config(), so it lives as long

protected:
    unsigned int baud_rate;
    unsigned int ticks_per_bit;
    unsigned int ticks_counter;
    unsigned int tape_mode;
    std::vector<uint8_t> data;
    unsigned int data_size;
    unsigned int data_position;
    int bit_shift;
    unsigned int total_seconds;
    void set_baud_rate(unsigned int baud);
    void set_data(const std::vector<uint8_t> &new_data);
    bool is_recording = false;
    //Лентопротяжку держит машина: окно в этом случае только показывает, что
    //происходит, и не останавливает ленту, закрываясь
    bool motor_on = false;
    std::string loaded_name;
    //Единственное место, где меняется режим: отсюда же уходит извещение в окно
    void set_tape_mode(unsigned int new_mode);
    //Звук ленты. Носителю, который отдает машине байты, а не полупериоды,
    //базовый clock() не годится, а слышно должно быть так же, как у
    //остальных: он сам выдает уровень и крутит динамик
    void tape_sound(unsigned int level) { i_speaker.change(level); }
    void clock_sound(unsigned int counter) { speaker->clock(counter); }
    void notify_state();
    TapeEnc m_tape_enc = TapeEnc::MSX;
    TapeMedium medium = TapeMedium::Levels;
    uint64_t cycle_counter = 0;
    uint64_t last_edge_cycles = 0;
    bool has_last_edge = false;
    uint8_t last_level = 0;
    TapeWriterState writer_state = TapeWriterState::Measuring;
    void write_edge(uint64_t counter);
    void store_bit(unsigned bit);
    bool record_keeps_sync();
    static void encode_msx(const std::vector<uint8_t> &buffer, std::vector<uint8_t> &buffer_encoded);
    unsigned measured_counter = 0;
    uint64_t measured_time = 0;
    uint64_t measured_time_x2 = 0;
    uint64_t max_delta = 0;
    unsigned byte_counter = 0;
    unsigned short_counter = 0;
    uint8_t current_byte = 0;
    std::vector<uint8_t> recorded_bytes{};
    bk_tape::Decoder bk_decoder;
    rk86_tape::Decoder rk86_decoder;
    uknc_tape::Decoder uknc_decoder;

    //----------------------- Лента как таймлайн ----------------------------//
    // Записи с паузами между ними, положение головки в единицах ленты и все,
    // что с этим связано. Общее для всех носителей: что такое единица, сказано
    // в tape_record.h
    std::vector<TapeRecord> m_records;
    uint64_t m_total_units = 0;
    // Сколько единиц файла .bt приходится на один битовый интервал
    double   m_unit_per_bit = 0.0;
    //Чистая кассета: записей нет, а лента есть
    bool     m_blank = false;
    uint64_t m_blank_units = 0;
    bool     m_dirty = false;   // Машина писала на кассету, а в файл это еще не легло

    TapeTransport m_transport = T_STOP;
    uint64_t  m_pos = 0;        // Положение головки, в единицах ленты
    uint64_t  m_ticks = 0;      // Дробная часть такта
    unsigned int m_fast_speed = 15;

    // Куда смотрит головка: номер записи и байт внутри нее
    size_t   m_rec = 0;
    size_t   m_byte = 0;
    bool     m_in_gap = true;

    // Запись: байты копятся, пока машина не остановит лентопротяжку
    std::vector<uint8_t> m_write_buf;
    uint64_t m_write_start = 0;
    uint64_t m_write_last = 0;
    uint64_t m_writes = 0;
    uint64_t m_bytes_read = 0;

    bool     m_carrier = false; // Головка внутри записи: для ВВ51 это DSR
    bool     m_half = false;    // Вторая половина битового интервала
    int      m_bit_out = -1;    // Бит, который сейчас на линии, -1 - тишина
    uint8_t  m_write_byte = 0;
    unsigned int m_write_bits = 0;
    unsigned int m_sound_level = 0;
    uint64_t m_sound_edges = 0; // Перепадов в динамике

    static unsigned int tape_mode_of(TapeTransport t);
    void set_carrier(bool on);
    emulator::Result parse_bt(const uint8_t * raw, size_t size, const std::string &where);
    void build_bt(std::vector<uint8_t> &out) const;
    int  current_bit() const;
    int  next_write_bit();
    void sound_level(unsigned int level);
    void locate(uint64_t pos);
    uint64_t record_start(size_t index) const;
    void advance_unit();
    void set_transport(TapeTransport t);
    void commit_write();
    void rebuild_timeline();

    //Тракт ленты-накопителя. Отделен не для красоты: шаг у него другой -
    //полубитовый цикл while против «не больше одной выборки за вызов» у
    //уровневой ленты, - и свести их в один значило бы сдвинуть выдачу
    //у семи эталонных тестов
    emulator::Result bytes_load_file(const std::string &file_name);
    void bytes_clock(unsigned int counter);

    //------------------------- Лента Спектрума -----------------------------//
    // Волна не разворачивается: в m_records лежат блоки как они были в файле,
    // а полупериоды выдает автомат. Почему - написано в tape_zx.h
    zx_tape::Pulser m_zx;
    unsigned int m_zx_pulse = 0;    // Сколько тактов идет текущий полупериод
    unsigned int m_zx_level = 0;
    std::vector<uint8_t> m_source;  // Исходный файл: он же и едет в снимок

    emulator::Result zx_load_file(const std::string &file_name, bool tzx);
    void zx_clock(unsigned int counter);
    void zx_locate(uint64_t pos);
    unsigned int zx_next_pulse();

public:
    std::string files;

    TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual ~TapeRecorder();

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    //The tape in the machine is the drive's own business, the same way a
    //floppy is: a recording exists nowhere else until it is saved
    bool state_owns_file(const std::string &parameter) const override
        { return parameter == "image" || parameter == "file"; }

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    //Формат ленты по расширению, из секции [TapeFiles]. Публичный потому, что
    //то же самое нужно окну магнитофона, а две копии одних и тех же четырех
    //строк расходятся при первом же новом расширении
    std::string format_for_extension(const std::string &ext);

    virtual emulator::Result load_file(const std::string &file_name, const std::string &fmt);
    virtual void play();
    virtual void stop();
    virtual void rewind();
    virtual void mute(bool muted);
    virtual void volume(unsigned int volume);
    virtual int get_position();
    virtual int get_total();
    virtual int get_mode();
    virtual void set_recording(bool recording);
    //Состояние для окна: оно ничего не хранит само, а спрашивает устройство
    bool get_recording() const { return is_recording; }
    bool is_machine_driven() const { return motor_on; }
    const std::string & get_loaded_name() const { return loaded_name; }
    virtual unsigned get_record_size();
    //Что уходит в файл при сохранении. По умолчанию - то, что машина записала;
    //лента-накопитель отдает здесь весь образ кассеты, потому что записанный
    //сектор сам по себе не кассета
    virtual void get_save_data(std::vector<uint8_t> &out);
    virtual std::string get_record_name();
    virtual std::vector<uint8_t> * get_record_data();

    std::function<void(unsigned int)> on_mode_changed;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd);
