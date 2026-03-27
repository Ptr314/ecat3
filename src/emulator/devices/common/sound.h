// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Generic sound device class

#pragma once

#include <vector>

#include "emulator/thread_compat.h"
#include "emulator/core.h"
#include "libs/audio_filters.h"

class AudioDriver;

class GenericSound: public ComputerDevice
{
private:
    CPU * cpu;
    bool m_initialized;
    uint64_t m_clock_freq;
    unsigned int m_counter;
    unsigned int m_samples_per_buffer;
    unsigned int m_sample_rate;
    AudioDriver * m_audio_driver;
    uint64_t m_counts_per_sample;

    // Data buffer
    std::vector<int16_t> m_buffer;
    size_t m_buffer_pos = 0;
    mutable compat_mutex m_buffer_mutex;

    // Sample accumulator
    int64_t m_accumulator;
    int64_t m_acc_counter;

    // Low pass filter
    bool m_use_lpf;
    int m_lpf_coutoff;
    ButterworthLowPassFilter m_filter;

    static void audio_callback(void* userdata, uint8_t* stream, int len);
    void handle_audio_callback(uint8_t* stream, int len);

protected:
    unsigned int m_volume;
    int64_t m_amplitude;
    bool m_muted;
    void init_sound(unsigned int clock_freq);
    virtual int16_t calc_sound_value() = 0;

public:
    GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~GenericSound();

    virtual emulator::Result load_config(SystemData *sd) override;
    virtual void clock(unsigned int counter) override;
    virtual void set_volume(unsigned int volume);
    virtual void set_muted(bool muted);
};
