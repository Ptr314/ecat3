// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder device

#pragma once

#include <functional>

#include "emulator/core.h"
#include "emulator/devices/common/speaker.h"
#include "emulator/devices/common/tape_bk.h"
#include "emulator/devices/common/tape_rk86.h"
#include "emulator/devices/common/tape_uknc.h"

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
    //аппаратная, и кодировщики базового устройства для нее не работают
    UNIOR
};

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
    //Звук ленты. Машине, которая ведет ленту сама и получает с нее байты, а не
    //полупериоды (Юниор), базовый clock() не годится, а слышно должно быть так
    //же, как у остальных: она сама выдает уровень и крутит динамик
    void tape_sound(unsigned int level) { i_speaker.change(level); }
    void clock_sound(unsigned int counter) { speaker->clock(counter); }
    void notify_state();
    TapeEnc m_tape_enc = TapeEnc::MSX;
    uint64_t cycle_counter = 0;
    uint64_t last_edge_cycles = 0;
    bool has_last_edge = false;
    uint8_t last_level = 0;
    TapeWriterState writer_state = TapeWriterState::Measuring;
    void write_edge(uint64_t counter);
    void store_bit(unsigned bit);
    std::string format_for_extension(const std::string &ext);
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
    void clock(unsigned int counter) override;

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
    //лента-накопитель (Юниор) отдает здесь весь образ кассеты, потому что
    //записанный сектор сам по себе не кассета
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

