// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Generic sound device class

#include <algorithm>
#include <iostream>

#include "sound.h"
#include "emulator/utils.h"
#include "emulator/audio/audio_driver.h"

#ifdef USE_SDL_AUDIO
    #include "emulator/audio/audio_driver_sdl.h"
#else
    #include "emulator/audio/audio_driver_miniaudio.h"
#endif

GenericSound::GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , m_initialized(false)
    , m_clock_freq(0)
    , m_counter(0)
    , m_samples_per_buffer(2048)
    , m_sample_rate(22050)
    , m_audio_driver(NULL)
    , m_volume(100)
    , m_muted(false)
    , m_use_lpf(false)
    , m_lpf_coutoff(5000)
    , m_accumulator(0)
    , m_acc_counter(0)
{
    m_amplitude = m_volume * 32000 / 100;
    m_buffer.resize(m_samples_per_buffer);
    cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu"));
}

emulator::Result GenericSound::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    m_lpf_coutoff = read_confg_value(cd, "lpf", false, (unsigned int)m_lpf_coutoff);
    m_use_lpf = (m_lpf_coutoff > 0);

    init_sound(cpu->clock);

    return emulator::Result::ok();
}

void GenericSound::init_sound(unsigned int clock_freq)
{
    if (m_initialized) {
        return;
    }

    m_clock_freq = clock_freq;
    m_counter = 0;

#ifdef USE_SDL_AUDIO
    m_audio_driver = new SDLAudioDriver();
#else
    m_audio_driver = new MiniaudioDriver();
#endif

    if (!m_audio_driver->open(m_sample_rate, 1, m_samples_per_buffer, audio_callback, this)) {
        std::cerr << "Audio driver: failed to open device" << std::endl;
        delete m_audio_driver;
        m_audio_driver = NULL;
        return;
    }

    m_sample_rate = m_audio_driver->getObtainedSampleRate();
    m_samples_per_buffer = m_audio_driver->getObtainedBufferSamples();

    m_buffer.resize(m_samples_per_buffer*2);
    m_buffer_pos = 0;

    m_counts_per_sample = (m_clock_freq << 8) / m_sample_rate; // * 128 to make it more precise

    if (m_use_lpf) m_filter.setup(m_sample_rate, m_lpf_coutoff);

    m_audio_driver->start();
    m_initialized = true;
}

GenericSound::~GenericSound()
{
    if (m_initialized) {
        m_initialized = false;
        if (m_audio_driver) {
            m_audio_driver->stop();
            m_audio_driver->close();
            delete m_audio_driver;
            m_audio_driver = NULL;
        }
    }
}

void GenericSound::set_volume(unsigned int volume)
{
    m_volume = volume;
    m_amplitude = m_volume * 32000 / 100;
}

void GenericSound::set_muted(bool muted)
{
    m_muted = muted;
}

void GenericSound::clock(unsigned int counter)
{
    if (!m_initialized) return;

    m_counter += counter << 8;

    if (m_counter >= m_counts_per_sample) {
        m_counter -= m_counts_per_sample;

#if USE_QT_THREADING
        QMutexLocker lock(&m_buffer_mutex);
#else
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
#endif

        // Checking buffer overflow and discarding a part of it if expected
        if (m_buffer_pos >= m_buffer.size()) {
            static int overflow_delta = m_samples_per_buffer / 8;
            std::copy(
                m_buffer.begin() + overflow_delta,
                m_buffer.begin() + m_buffer_pos,
                m_buffer.begin()
            );
            m_buffer_pos -= overflow_delta;
            // std::cerr << "Sound buffer overflow!" << std::endl;
        };

        // Using average value between counts
        int16_t v = static_cast<int16_t>(m_accumulator / m_acc_counter);
        m_accumulator = m_acc_counter = 0;

        // Applying LPF if expected
        if (m_use_lpf) {
            float out = m_filter.process(static_cast<float>(v));
            v = static_cast<int16_t>(out);
        }

        m_buffer[m_buffer_pos++] = v;
    } else {
        // Accumulating values between counts to get average when expected
        m_accumulator += m_muted ? -m_amplitude : calc_sound_value();
        m_acc_counter++;
    }
}

void GenericSound::audio_callback(void* userdata, uint8_t* stream, int len)
{
    GenericSound* sound = static_cast<GenericSound*>(userdata);
    sound->handle_audio_callback(stream, len);
}

void GenericSound::handle_audio_callback(uint8_t* stream, int len)
{
    if (!m_initialized) return;

#if USE_QT_THREADING
    QMutexLocker lock(&m_buffer_mutex);
#else
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
#endif

    const int samples_requested = len / sizeof(int16_t);
    const int samples_available = static_cast<int>(m_buffer_pos);
    const int samples_to_copy = std::min(samples_requested, samples_available);

    // We use 'fill_value' later to fill missed samples.
    // The default value is -m_amplitude - "silence" in case the buffer is totally empty.
    // And the first buffer value, if we have less values than expected
    int16_t fill_value = -m_amplitude;

    if (samples_to_copy > 0) {
        fill_value = m_buffer[0];
        // We have some data, putting it to the end of the output
        std::copy_n(
            m_buffer.data(),
            samples_to_copy,
            reinterpret_cast<int16_t*>(stream) + (samples_requested - samples_to_copy)
        );

        // And if we have more data than expected, we move the extra data to the beginning
        std::copy(
            m_buffer.begin() + samples_to_copy,
            m_buffer.begin() + m_buffer_pos,
            m_buffer.begin()
        );
        m_buffer_pos -= samples_to_copy;
    }

    if (samples_to_copy < samples_requested) {
        // We need to fill in the missing data, so we do it at the beginning of the output.
        std::fill_n(
            reinterpret_cast<int16_t*>(stream),
            samples_requested - samples_to_copy,
            fill_value
        );
    }
}
