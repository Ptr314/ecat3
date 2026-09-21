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
class SoundSource
{
    friend class GenericSound;
    GenericSound * m_mixer = nullptr;

public:
    virtual ~SoundSource() = default;
    virtual int32_t sound_sample(int64_t amplitude) = 0;
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
    bool m_level_dirty = true;

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
    int64_t m_acc_counter;
    float m_last_input;

    // DC offset removal, keeps silence at 0 to avoid clicks on start/stop/mute
    DCBlocker m_dc_blocker;

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

    // Called by the sources, see SoundSource
    void source_changed() { m_level_dirty = true; }
    void source_mode_changed() { refresh_sources(); }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};
