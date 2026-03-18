// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/speaker.h"

#define TAPE_STOPPED 0
#define TAPE_READ    1
#define TAPE_WRITE   0

enum class TapeEnc {
    MSX,
    RK86
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

    Speaker * speaker;

protected:
    unsigned int system_clock;
    unsigned int baud_rate;
    unsigned int ticks_per_bit;
    unsigned int ticks_counter;
    unsigned int tape_mode;
    QByteArray data;
    unsigned int data_size;
    unsigned int data_position;
    int bit_shift;
    unsigned int total_seconds;
    void set_baud_rate(unsigned int baud);
    void set_data(QByteArray new_data);
    bool is_recording = false;
    TapeEnc m_tape_enc = TapeEnc::MSX;
    uint64_t cycle_counter = 0;
    uint64_t last_edge_cycles = 0;
    bool has_last_edge = false;
    TapeWriterState writer_state = TapeWriterState::Measuring;
    void write_edge(uint64_t counter);
    void store_bit(unsigned bit);
    static void encode_msx(const QByteArray &buffer, QByteArray &buffer_encoded);
    unsigned measured_counter = 0;
    uint64_t measured_time = 0;
    uint64_t measured_time_x2 = 0;
    uint64_t max_delta = 0;
    unsigned byte_counter = 0;
    unsigned short_counter = 0;
    uint8_t current_byte = 0;
    std::vector<uint8_t> recorded_bytes{};
public:
    QString files;

    TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual ~TapeRecorder();

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    void load_config(SystemData *sd) override;
    void clock(unsigned int counter) override;

    virtual void load_file(QString file_name, QString fmt);
    virtual void play();
    virtual void stop();
    virtual void rewind();
    virtual void mute(bool muted);
    virtual void volume(unsigned int volume);
    virtual int get_position();
    virtual int get_total();
    virtual int get_mode();
    virtual void set_recording(bool recording);
    virtual unsigned get_record_size();
    virtual std::vector<uint8_t> * get_record_data();
};

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd);

