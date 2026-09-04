// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AY-3-8910 programmable sound generator

#pragma once

#include <cstdint>

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"

// Three square wave channels, a noise generator and an envelope generator.
// The chip has no audio output of its own here: it is a SoundSource mixed
// into a GenericSound device (mix = ay). Its registers are reached through
// the memory map; the "bus" parameter picks how the two write kinds of the
// host bus are turned into the register-select and data cycles of the chip.

#define AY_REGS             16
#define AY_TONE_CHANNELS    3

class AY8910 : public AddressableDevice, public SoundSource
{
private:
    Interface i_porta;                  // I/O port A output (register 14)
    Interface i_portb;                  // I/O port B output (register 15)

    unsigned int m_bus;                 // host bus protocol, AY_BUS_*
    unsigned int m_frequency;           // chip clock, Hz
    uint32_t m_step;                    // 16.16 tone ticks per system clock
    uint32_t m_acc;

    uint8_t m_regs[AY_REGS];
    unsigned int m_latch;               // selected register

    // Generators. A tone tick is 8 chip clocks; noise and envelope run at 16.
    unsigned int m_tone_count[AY_TONE_CHANNELS];
    unsigned int m_tone_out[AY_TONE_CHANNELS];
    unsigned int m_prescale;
    unsigned int m_noise_count;
    uint32_t m_rng;                     // 17-bit LFSR
    unsigned int m_env_count;
    int m_env_step;
    unsigned int m_env_attack;
    bool m_env_hold;
    bool m_env_alternate;
    bool m_env_holding;
    unsigned int m_env_volume;

    void select(unsigned int reg);
    void write_reg(unsigned int reg, unsigned int value);
    unsigned int read_reg(unsigned int reg);
    void restart_envelope();
    void tick();
    unsigned int level();

public:
    AY8910(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;

    int32_t sound_sample(int64_t amplitude) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_ay8910(InterfaceManager *im, EmulatorConfigDevice *cd);
