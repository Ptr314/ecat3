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

void SoundSource::sound_changed()
{
    if (m_mixer != nullptr) m_mixer->source_changed();
}

void SoundSource::sound_mode_changed()
{
    if (m_mixer != nullptr) m_mixer->source_mode_changed();
}

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
    m_clocked = true;   //clock() is overridden here
    m_amplitude = m_volume * 32000 / 100;
    m_buffer.resize(m_samples_per_buffer);
    //The CPU is not looked up here: a constructor must not reach for another
    //device (it may not exist yet). ComputerDevice::load_config() fills in
    //m_system_clock, and init_sound() below runs after that call
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
            m_source_names.push_back(dev_name);
            src->m_mixer = this;
        }
    }
    m_idle_share = read_confg_value(cd, "idle_share", false, true);
    m_idle_sample.assign(m_sources.size(), 0);
    m_heard.assign(m_sources.size(), m_idle_share ? 1 : 0);

    //Without an audio device the whole sound path stays dormant: clock() drops
    //out on the very first line, so a silent run costs less than a loud one
    if (sd->audio_enabled) init_sound(m_system_clock);

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
    refresh_sources();

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
    m_level_dirty = true;
}

void GenericSound::set_muted(bool muted)
{
    m_muted = muted;
}

void GenericSound::refresh_sources()
{
    m_active.clear();
    m_sources_volatile = false;
    for (size_t i = 0; i < m_sources.size(); i++)
        if (in_mix(i)) {
            m_active.push_back(m_sources[i]);
            if (m_sources[i]->sound_volatile()) m_sources_volatile = true;
        }
    m_shares = 1 + (int64_t)m_active.size();
    m_level_dirty = true;
}

// A millisecond of the device's own clock
static int64_t listen_period(uint64_t clock)
{
    return (clock >= 1000) ? (int64_t)(clock / 1000) : 1;
}

void GenericSound::reset(bool cold)
{
    ComputerDevice::reset(cold);
    // The sources may be reset after this device, so their idle samples are
    // taken at the first check rather than here
    for (size_t i = 0; i < m_heard.size(); i++) m_heard[i] = m_idle_share ? 1 : 0;
    m_idle_taken = false;
    m_listening = !m_idle_share && !m_sources.empty();
    m_listen_left = listen_period(m_system_clock);
    refresh_sources();
}

// The same amplitude every time, whatever the volume: a sample is compared
// with the idle one, not played
#define LISTEN_AMPLITUDE 32000

void GenericSound::listen_to_sources()
{
    m_listen_left += listen_period(m_system_clock);
    if (!m_idle_taken) {
        for (size_t i = 0; i < m_sources.size(); i++)
            m_idle_sample[i] = m_sources[i]->sound_sample(LISTEN_AMPLITUDE);
        m_idle_taken = true;
        return;
    }
    bool unheard = false;
    for (size_t i = 0; i < m_sources.size(); i++) {
        if (m_heard[i]) continue;
        if (m_sources[i]->sound_sample(LISTEN_AMPLITUDE) != m_idle_sample[i]) m_heard[i] = 1;
        else unheard = true;
    }
    m_listening = unheard;
    refresh_sources();
}

void GenericSound::clock(unsigned int counter)
{
    if (m_listening) {
        m_listen_left -= counter;
        if (m_listen_left <= 0) listen_to_sources();
    }

    if (!m_initialized) return;

    // The sources are averaged with the device's own output so that the sum
    // stays within the amplitude; a source that is switched off takes no share.
    // The sum is divided by the shares once a sample, not here: this runs on
    // every instruction of the processor the device is clocked with
    // Summed again only when a part of it may have changed: that is most of
    // the cost of sound, paid on every instruction for the same silence
    if (m_level_dirty || m_self_volatile || m_sources_volatile) {
        m_level_dirty = false;
        m_level = calc_sound_value();
        for (size_t i = 0; i < m_active.size(); i++)
            m_level += m_active[i]->sound_sample(m_amplitude);
    }
    const int64_t level = m_level;

    // The level is weighted by the clock ticks it was held for. An instruction
    // takes from a dozen ticks to a hundred, and counting every one of them
    // once distorts a level that a program holds for a measured time: the
    // bytes of a DAC, the pulse widths of a one-bit output
    m_accumulator += level * counter;
    m_acc_counter += counter;

    m_counter += counter << 8;
    if (m_counter < m_counts_per_sample) return;

#if USE_QT_THREADING
    QMutexLocker lock(&m_buffer_mutex);
#else
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
#endif

    // A long slice (a bus master holding the CPU) may cover several samples:
    // the first one takes the average, the rest repeat it
    while (m_counter >= m_counts_per_sample) {
        m_counter -= m_counts_per_sample;

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
        float v = (m_acc_counter > 0) ? static_cast<float>(m_accumulator) / (float)(m_acc_counter * m_shares) : m_last_input;
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
    }
    refresh_sources();
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

void GenericSound::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    //Only what the emulation side carries between samples. The ring buffer of
    //the audio driver belongs to the host, not to the machine, and the filter
    //histories are worth one click at most
    w.n("counter", m_counter);
    w.n64("accumulator", static_cast<uint64_t>(m_accumulator));
    w.n64("acc_counter", static_cast<uint64_t>(m_acc_counter));
    w.n("volume", m_volume);
    w.b("muted", m_muted);
    //Which sources have been heard, and are therefore in the mix. Left out, a
    //board that has been playing for an hour would be taken for idle again
    w.b("idle_taken", m_idle_taken);
    w.b("listening", m_listening);
    w.n64("listen_left", static_cast<uint64_t>(m_listen_left));
    if (!m_heard.empty())
    {
        std::vector<bool> heard(m_heard.size());
        for (size_t i = 0; i < m_heard.size(); i++) heard[i] = m_heard[i] != 0;
        std::vector<uint32_t> samples(m_idle_sample.size());
        for (size_t i = 0; i < m_idle_sample.size(); i++)
            samples[i] = static_cast<uint32_t>(m_idle_sample[i]);
        //vector<bool> has no contiguous storage of its own
        std::vector<char> flat(heard.size());
        for (size_t i = 0; i < heard.size(); i++) flat[i] = heard[i] ? 1 : 0;
        w.array("heard", reinterpret_cast<const uint8_t *>(flat.data()), flat.size());
        if (!samples.empty()) w.array("idle_sample", samples.data(), samples.size());
    }
}

emulator::Result GenericSound::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    r.u("counter", m_counter);
    uint64_t v = 0;
    if (r.n64("accumulator", v)) m_accumulator = static_cast<int64_t>(v);
    if (r.n64("acc_counter", v)) m_acc_counter = static_cast<int64_t>(v);
    r.u("volume", m_volume);
    r.b("muted", m_muted);
    r.b("idle_taken", m_idle_taken);
    r.b("listening", m_listening);
    if (r.n64("listen_left", v)) m_listen_left = static_cast<int64_t>(v);
    if (!m_heard.empty())
    {
        std::vector<char> flat(m_heard.size(), 0);
        for (size_t i = 0; i < m_heard.size(); i++) flat[i] = m_heard[i];
        r.array("heard", reinterpret_cast<uint8_t *>(flat.data()), flat.size());
        for (size_t i = 0; i < m_heard.size(); i++) m_heard[i] = flat[i];
    }
    if (!m_idle_sample.empty())
    {
        std::vector<uint32_t> samples(m_idle_sample.size());
        for (size_t i = 0; i < m_idle_sample.size(); i++)
            samples[i] = static_cast<uint32_t>(m_idle_sample[i]);
        r.array("idle_sample", samples.data(), samples.size());
        for (size_t i = 0; i < m_idle_sample.size(); i++)
            m_idle_sample[i] = static_cast<int32_t>(samples[i]);
    }
    return emulator::Result::ok();
}

void GenericSound::state_restored()
{
    //Who is in the mix depends on every source having been restored first
    refresh_sources();
    m_level_dirty = true;
}

std::vector<DeviceFieldInfo> GenericSound::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"volume",  "Current volume, 0-100",    false});
    r.push_back({"muted",   "1 if the sound is muted",  false});
    r.push_back({"active",  "1 if an audio device is open. 0 means the machine "
                            "is silent: --no-sound, or no audio device at all",  false});
    r.push_back({"mixed",   "Sources of mix taking a share of it now, '-' for none", false});
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
    if (field == "active")  { out.values.push_back(m_initialized?1:0); return true; }
    if (field == "mixed") {
        out.numeric = false;
        std::string s;
        for (size_t i = 0; i < m_sources.size(); i++)
            if (in_mix(i)) s += (s.empty() ? "" : " ") + m_source_names[i];
        out.text = s.empty() ? "-" : s;
        return true;
    }

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
