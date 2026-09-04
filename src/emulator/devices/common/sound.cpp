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
    , m_last_input(0)
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

    // Other sound devices whose output is mixed into this one: mix = ay|dac
    std::string mix = cd->get_parameter("mix", false).value;
    if (!mix.empty()) {
        std::vector<std::string> names = split_string(mix, '|', true);
        for (size_t i = 0; i < names.size(); i++) {
            std::string dev_name = str_trim(names[i]);
            SoundSource * src = dynamic_cast<SoundSource*>(im->dm->get_device_by_name(dev_name, false));
            if (src == nullptr)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{GenericSound|" + std::string(QT_TRANSLATE_NOOP("GenericSound", "Not a sound source")) + "} " + dev_name);
            m_sources.push_back(src);
        }
    }

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

    m_dc_blocker.setup(m_sample_rate, 20);

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
            size_t overflow_delta = m_samples_per_buffer / 8;
            std::copy(
                m_buffer.begin() + overflow_delta,
                m_buffer.begin() + m_buffer_pos,
                m_buffer.begin()
            );
            m_buffer_pos -= overflow_delta;
            // std::cerr << "Sound buffer overflow!" << std::endl;
        };

        // Using average value between counts
        float v = (m_acc_counter > 0) ? static_cast<float>(m_accumulator) / m_acc_counter : m_last_input;
        m_last_input = v;
        m_accumulator = m_acc_counter = 0;

        // Removing DC offset so silence sits at 0 regardless of the device's idle level
        float out = m_dc_blocker.process(v);

        if (m_muted) out = 0;

        // Applying LPF if expected
        if (m_use_lpf) out = m_filter.process(out);

        if (out > 32767.0f) out = 32767.0f;
        else if (out < -32768.0f) out = -32768.0f;

        m_buffer[m_buffer_pos++] = static_cast<int16_t>(out);
    } else {
        // Accumulating values between counts to get average when expected.
        // The sources are averaged with the device's own output so that the
        // sum stays within the amplitude.
        int64_t v = calc_sound_value();
        for (size_t i = 0; i < m_sources.size(); i++) v += m_sources[i]->sound_sample(m_amplitude);
        if (!m_sources.empty()) v /= (int64_t)(m_sources.size() + 1);
        m_accumulator += v;
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
    // The default value is 0 - "silence" in case the buffer is totally empty.
    // And the first buffer value, if we have less values than expected
    int16_t fill_value = 0;

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

//------------------- Introspection and control ----------------------------//

std::vector<DeviceFieldInfo> GenericSound::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"volume",  "Current volume, 0-100",    false});
    r.push_back({"muted",   "1 if the sound is muted",  false});
    return r;
}

std::vector<DeviceCommandInfo> GenericSound::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"volume",  "0-100",    "Sets the volume"});
    r.push_back({"mute",    "[0|1]",    "Mutes or unmutes the sound"});
    return r;
}

bool GenericSound::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "volume")  { out.values.push_back(m_volume);       return true; }
    if (field == "muted")   { out.values.push_back(m_muted?1:0);    return true; }

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

emulator::Result GenericSound::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "volume") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{GenericSound|" + std::string(QT_TRANSLATE_NOOP("GenericSound", "Command 'volume' expects a value")) + "}");
        set_volume(parse_numeric_value(p[0]));
        return emulator::Result::ok();
    }

    if (command == "mute") {
        set_muted(p.empty() || p[0].empty() || parse_numeric_value(p[0]) != 0);
        return emulator::Result::ok();
    }

    return ComputerDevice::send_command(command, parameters);
}
