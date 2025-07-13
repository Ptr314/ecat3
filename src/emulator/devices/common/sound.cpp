// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Generic sound device class

#include <algorithm>

#include "sound.h"

GenericSound::GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    m_initialized(false),
    m_clock_freq(0),
    m_counter(0),
    m_samples_per_buffer(4096),
    m_sample_rate(22050),
    m_audio_device(0),
    m_volume(100),
    muted(false)
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

    m_counts_per_sample = m_clock_freq / m_sample_rate * 128;

    SDL_PauseAudioDevice(m_audio_device, 0);
    m_initialized = true;
}

GenericSound::~GenericSound()
{
    if (m_initialized) {
        SDL_CloseAudioDevice(m_audio_device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void GenericSound::set_volume(unsigned int volume)
{
    m_volume = volume;
}

void GenericSound::set_muted(bool muted)
{
    this->muted = muted;
}

// // Queue version
// void GenericSound::clock(unsigned int counter)
// {
//     if (!m_initialized) return;

//     m_counter += counter*128;

//     if (m_counter >= m_counts_per_sample) {
//         m_counter -= m_counts_per_sample;

//         m_buffer[m_buffer_pos++] = static_cast<int16_t>(calc_sound_value());
//         if (m_buffer_pos >= m_samples_per_buffer) {
//             m_buffer_pos = 0;
//             SDL_QueueAudio(m_audio_device, m_buffer.data(), m_samples_per_buffer);
//         }
//     }
// }

// Callback version
void GenericSound::clock(unsigned int counter)
{
    if (!m_initialized) return;

    double samples_to_generate = static_cast<double>(counter) * m_sample_rate / m_clock_freq;
    m_accumulated_samples += samples_to_generate;

    while (m_accumulated_samples >= 1.0) {
        m_accumulated_samples -= 1.0;
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        if (m_buffer_pos < m_samples_per_buffer) {
            m_last_value = static_cast<int16_t>(calc_sound_value());
            m_buffer[m_buffer_pos++] = m_last_value;
        }
    }
}

void GenericSound::audio_callback(void* userdata, Uint8* stream, int len) {
    GenericSound* sound = static_cast<GenericSound*>(userdata);
    sound->handle_audio_callback(stream, len);
}

void GenericSound::handle_audio_callback(Uint8* stream, int len) {
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    const int samples_requested = len / sizeof(int16_t);
    const int samples_available = static_cast<int>(m_buffer_pos);
    const int samples_to_copy = std::min(samples_requested, samples_available);

    if (samples_to_copy > 0) {
        std::copy_n(
            m_buffer.data(),
            samples_to_copy,
            reinterpret_cast<int16_t*>(stream)
            );

        std::copy(
            m_buffer.begin() + samples_to_copy,
            m_buffer.begin() + m_buffer_pos,
            m_buffer.begin()
            );

        m_buffer_pos -= samples_to_copy;
    }

    static int ok_count = 0;
    static int fill_count = 0;
    if (samples_to_copy < samples_requested) {
        fill_count++;
        qDebug() << samples_to_copy << " " << samples_requested <<  " ok: " << ok_count << " fill: " << fill_count;
        std::fill_n(
            reinterpret_cast<int16_t*>(stream) + samples_to_copy,
            samples_requested - samples_to_copy,
            m_last_value
            );
    } else {
        ok_count++;
    }
}
