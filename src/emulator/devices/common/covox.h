// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Covox, an 8-bit DAC on the output lines of a parallel port

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"
#include "emulator/devices/common/connector.h"

// A resistor ladder on the eight output lines of a printer port: whatever
// byte the program leaves on the port is the output level, 0 the lowest and
// 0377 the highest. There is no clock and no register of its own - the port
// holds the byte - so the program sets the sample rate by how often it writes.
// Like ay8910 it has no audio output of its own and is mixed into a speaker
// (mix = covox). It can be pulled out of a connector, and then it is silent.
class Covox : public ComputerDevice, public SoundSource, public Pluggable
{
private:
    Interface i_input;

    // idle_share = 0: until the input first leaves 0 after reset the DAC
    // takes no share of the mix, so a board nobody plays leaves the rest loud
    bool m_idle_share;
    bool m_used;

    // Last sample and the inputs it came from: asked for once per instruction,
    // while the byte changes only when the program writes the port
    unsigned int m_cached_level;
    int64_t m_cached_amplitude;
    int32_t m_cached_sample;

protected:
    void plug_changed() override;

public:
    Covox(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    int32_t sound_sample(int64_t amplitude) override;
    bool sound_active() override;
    const char * plug_title() const override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_covox(InterfaceManager *im, EmulatorConfigDevice *cd);
