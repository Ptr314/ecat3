// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Generic sound device class

#include <algorithm>
#include <iostream>

#include "sound.h"

GenericSound::GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    m_initialized(false),
    m_clock_freq(0),
    m_counter(0),
    m_samples_per_buffer(2048),
    m_sample_rate(22050),
    m_audio_device(0),
    m_volume(100),
    m_muted(false)
{
    m_buffer.resize(m_samples_per_buffer);
    cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu"));
    init_sound(cpu->clock);
}

void GenericSound::init_sound(unsigned int clock_freq)
{
    if (m_initialized) {
        return;
    }

    m_clock_freq = clock_freq;
    m_counter = 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL audio init failed: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_AudioSpec desired_spec, obtained_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = m_sample_rate;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.channels = 1;
    desired_spec.samples = m_samples_per_buffer;
    desired_spec.userdata = this;
    desired_spec.callback = audio_callback;

    m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &obtained_spec, 0);
    if (m_audio_device == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    m_sample_rate = obtained_spec.freq;
    m_samples_per_buffer = obtained_spec.samples;

    m_buffer.resize(m_samples_per_buffer*2);
    m_buffer_pos = 0;

    m_counts_per_sample = (m_clock_freq << 7) / m_sample_rate; // * 128 to make it more precise

    SDL_PauseAudioDevice(m_audio_device, 0);
    m_initialized = true;
}

GenericSound::~GenericSound()
{
    if (m_initialized) {
        m_initialized = false;
        SDL_PauseAudioDevice(m_audio_device, 1);
        SDL_CloseAudioDevice(m_audio_device);
        // SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void GenericSound::set_volume(unsigned int volume)
{
    m_volume = volume;
}

void GenericSound::set_muted(bool muted)
{
    m_muted = muted;
}

void GenericSound::clock(unsigned int counter)
{
    if (!m_initialized) return;

    m_counter += counter << 7;

    if (m_counter >= m_counts_per_sample) {
        m_counter -= m_counts_per_sample;
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        static int overflow_delta = m_samples_per_buffer / 8;
        if (m_buffer_pos >= m_buffer.size()) {
            std::copy(
                m_buffer.begin() + overflow_delta,
                m_buffer.begin() + m_buffer_pos,
                m_buffer.begin()
            );
            m_buffer_pos -= overflow_delta;
            std::cerr << "Sound buffer overflow!" << std::endl;
        };
        m_buffer[m_buffer_pos++] = static_cast<int16_t>(calc_sound_value());
    }
}

void GenericSound::audio_callback(void* userdata, Uint8* stream, int len)
{
    GenericSound* sound = static_cast<GenericSound*>(userdata);
    sound->handle_audio_callback(stream, len);
}

void GenericSound::handle_audio_callback(Uint8* stream, int len)
{
    if (!m_initialized) return;

    std::lock_guard<std::mutex> lock(m_buffer_mutex);

    const int samples_requested = len / sizeof(int16_t);
    const int samples_available = static_cast<int>(m_buffer_pos);
    const int samples_to_copy = std::min(samples_requested, samples_available);

    // static int n = 0;
    // n++;
    // if (samples_available > samples_requested) {
    //     qDebug() << name << ": " << n << " + " << samples_available - samples_requested;
    // } else
    // if (samples_available < samples_requested) {
    //     qDebug() << name << ": " << n << " --- " << samples_requested - samples_available;
    // };

    // We use 'fill_value' later to fill missed samples.
    // The default value is 0 - "silence" in case the buffer is totally empty.
    // And the first buffer value, if we have less values than expected
    int16_t fill_value = 0;

    if (samples_to_copy > 0) {
        fill_value = m_buffer[0];
        // We have some data -
        // So putting it to the end of the output
        std::copy_n(
            m_buffer.data(),
            samples_to_copy,
            reinterpret_cast<int16_t*>(stream) + (samples_requested - samples_to_copy)
        );

        // And if we have more data than expected, moving the extra data to the start of the buffer
        std::copy(
            m_buffer.begin() + samples_to_copy,
            m_buffer.begin() + m_buffer_pos,
            m_buffer.begin()
        );
        m_buffer_pos -= samples_to_copy;
    }

    if (samples_to_copy < samples_requested) {
        // We need to fill missing data, so doing it at the start of the output
        std::fill_n(
            reinterpret_cast<int16_t*>(stream),
            samples_requested - samples_to_copy,
            fill_value
        );
    }
}
