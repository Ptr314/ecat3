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
class GenericSound;

// A device that produces sound but has no audio output of its own: its level is
// added to the output of a GenericSound listed in its "mix" parameter. The
// sample is expected in the range -amplitude..amplitude.
//
// The mix is summed on every instruction of its processor, and almost always
// to the same value. A source that knows when its sample changes says so with
// sound_changed() and answers false to sound_volatile(); the mix then keeps
// the last sum until something is reported. A source that cannot tell stays
// volatile, the default, and is asked on every clock as before.
//
// A source may be stereo (the Агат ЯЗС has two line outputs). sound_sample()
// is then its right channel and the one a mono output takes, so it must be
// the sample that sounds right alone: summing two channels of a pseudo-stereo
// source cancels what one of them has turned over. sound_sample_left() is the
// other channel. A GenericSound with a stereo source opens its device in
// stereo, and every mono source sounds the same in both channels
class SoundSource
{
    friend class GenericSound;
    GenericSound * m_mixer = nullptr;

public:
    virtual ~SoundSource() = default;
    virtual int32_t sound_sample(int64_t amplitude) = 0;
    // Decided by the configuration, asked once when the output is opened
    virtual bool sound_stereo() { return false; }
    virtual int32_t sound_sample_left(int64_t amplitude) { return sound_sample(amplitude); }
    // A source that is switched off (a board pulled out of its connector)
    // takes no share of the mix, so the others keep their loudness
    virtual bool sound_active() { return true; }
    // The sample may change without sound_changed() being called
    virtual bool sound_volatile() { return true; }

protected:
    // The sample has (or may have) changed
    void sound_changed();
    // sound_active() or sound_volatile() has changed as well
    void sound_mode_changed();
};

class GenericSound: public ComputerDevice
{
private:
    std::vector<SoundSource*> m_sources;    // mixed into the output, see "mix"

    // The sources taking a share of the mix and the number of shares, the
    // device's own output included. Taken once a sample rather than on every
    // clock: activity changes only when a source is first written to or reset
    std::vector<SoundSource*> m_active;
    int64_t m_shares = 1;
    bool m_sources_volatile = true;     // some source in m_active is volatile
    void refresh_sources();

    // The sum of the device's own output and the sources, kept while nothing
    // that makes it up changes
    int64_t m_level = 0;
    int64_t m_level_left = 0;           // stereo only: the left channel of it
    bool m_level_dirty = true;

    // 2 when a source of the mix is stereo: the buffer, the capture and the
    // device then take frames of left and right
    unsigned int m_channels = 1;

    // idle_share = 0: a source joins the mix only once it has been heard - its
    // sample has left the one it gave just after the reset. Otherwise a board
    // nobody plays would take its share and quieten everything else. Checked
    // once a millisecond, with or without an audio device, so that a script
    // sees the same "mixed" field in a silent run
    std::vector<std::string> m_source_names;
    std::vector<int32_t> m_idle_sample;
    std::vector<char> m_heard;
    bool m_idle_share = true;
    bool m_idle_taken = false;          // m_idle_sample holds this reset's samples
    bool m_listening = false;           // some source is still unheard
    int64_t m_listen_left = 0;          // ticks to the next check
    void listen_to_sources();
    bool in_mix(size_t i) { return m_sources[i]->sound_active() && m_heard[i]; }
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

    // How often the audio device found the buffer short of what it asked for
    // and had to pad it with a repeated sample, and how often the emulation
    // outran the device and a slice of the buffer was thrown away. Both are
    // audible - a pad is the buzz of an underrun - and both say the producer
    // and the consumer are out of step rather than that the machine is wrong
    uint64_t m_underruns = 0;
    uint64_t m_overflows = 0;

    // Sample accumulator
    int64_t m_accumulator;
    int64_t m_accumulator_left = 0;
    int64_t m_acc_counter;
    float m_last_input;
    float m_last_input_left = 0;

    // DC offset removal, keeps silence at 0 to avoid clicks on start/stop/mute
    DCBlocker m_dc_blocker;
    DCBlocker m_dc_blocker_left;

    // Low pass filter
    bool m_use_lpf;
    bool m_limiter = false;             // soft limiting of the final sample (limiter = 1)
    bool m_lpf_on = false;              // m_use_lpf, and the cutoff below half the sample rate
    int m_lpf_coutoff;
    ButterworthLowPassFilter m_filter;
    ButterworthLowPassFilter m_filter_left;

    // Запись того, что уходит в звуковое устройство, в WAV (команда record).
    // Работает и без устройства (--no-sound): тогда тракт выборок заводится
    // только ради записи, на 44100 Гц
    bool m_capture = false;
    bool m_pipeline = false;            // m_counts_per_sample и фильтры настроены
    std::string m_capture_file;
    std::vector<int16_t> m_capture_data;    // m_channels to a frame
    void setup_pipeline();
    emulator::Result write_capture();

    // The sound of a video being recorded, taken away by the render thread
    // (take_video_samples) and so never more than a few frames long. Mono
    // always: the right channel, see SoundSource
    bool m_video_capture = false;
    std::vector<int16_t> m_video_data;

    static void audio_callback(void* userdata, uint8_t* stream, int len);
    void handle_audio_callback(uint8_t* stream, int len);

protected:
    unsigned int m_volume;
    int64_t m_amplitude;
    bool m_muted;
    // calc_sound_value() may change without sound_changed() - true for every
    // device that does not know better, as speaker
    bool m_self_volatile = true;
    void sound_changed() { m_level_dirty = true; }
    void init_sound(unsigned int clock_freq);
    virtual int16_t calc_sound_value() = 0;

public:
    GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd);
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;
    ~GenericSound();

    virtual emulator::Result load_config(SystemData *sd) override;
    virtual void reset(bool cold) override;
    virtual void clock(unsigned int counter) override;
    virtual void set_volume(unsigned int volume);
    virtual void set_muted(bool muted);

    // The sound of a video, see VideoRecorder. begin and end on the emulation
    // thread, take from any; samples at sample_rate(), counted in emulated time
    void begin_video_capture();
    void end_video_capture();
    void take_video_samples(std::vector<int16_t> &out);
    unsigned int sample_rate() const { return m_sample_rate; }
    // The rate the samples really come at in emulated time: the divider
    // m_counts_per_sample is rounded down, so there are a few more of them
    // than sample_rate() says - 0.6 s an hour on a 1 MHz machine
    unsigned int produced_rate() const;

    // Called by the sources, see SoundSource
    void source_changed() { m_level_dirty = true; }
    void source_mode_changed() { refresh_sources(); }

    // The cutoff of the filter, offered to the configuration editor only by a
    // machine that sets lpf_editable = 1 (the Агат with the ЯЗС)
    ConfigFields get_config_fields() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};
