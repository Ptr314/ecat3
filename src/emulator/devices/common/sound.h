// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Generic sound device class

#pragma once

#include <SDL.h>
#include <atomic>
#include <vector>
#include <cmath>
#include <iostream>
#include <mutex>

#include "emulator/core.h"

class GenericSound: public ComputerDevice
{
private:
    CPU * cpu;
    bool m_initialized;
    unsigned int m_clock_freq;
    unsigned int m_counter;
    unsigned int m_samples_per_buffer;
    unsigned int m_sample_rate;
    SDL_AudioDeviceID m_audio_device;
    int16_t m_last_value = 0;
    unsigned int m_counts_per_sample;

    std::vector<int16_t> m_buffer;
    size_t m_buffer_pos = 0;
    double m_accumulated_samples = 0.0;
    std::mutex m_buffer_mutex;

    static void audio_callback(void* userdata, Uint8* stream, int len);
    void handle_audio_callback(Uint8* stream, int len);

protected:
    unsigned int m_volume;
    bool muted;
    void init_sound(unsigned int clock_freq);
    virtual unsigned int calc_sound_value() = 0;

public:
    GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~GenericSound();

    virtual void clock(unsigned int counter) override;
    virtual void set_volume(unsigned int volume);
    virtual void set_muted(bool muted);
};
