// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AY-3-8910 programmable sound generator

#pragma once

#include <cstdint>

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"
#include "emulator/devices/common/connector.h"

// Three square wave channels, a noise generator and an envelope generator.
// The chip has no audio output of its own here: it is a SoundSource mixed
// into a GenericSound device (mix = ay). Its registers are reached through
// the memory map; the "bus" parameter picks how the two write kinds of the
// host bus are turned into the register-select and data cycles of the chip.
// A board that is pulled out of its connector hears no writes and is silent.
//
// Two chips on one port - the 2xAY boards of the БК, TurboSound and
// GryphonSound - are two such devices, both on 0177714 with through = 1 and
// with chip = 1 and chip = 2. Each follows the register-select words itself:
// register 0377 selects the first chip and 0376 the second (TurboSound), bits
// 15 and 14 of the word select the first, the second or both (GryphonSound).
// A chip takes data only while it is selected; after a reset the first one
// is. The second names the first in "pair": it is plugged in and out with it,
// and takes a share of the mix only once a program has written to it, so a
// program for a single AY sounds exactly as before.
//
// A chip is mono unless "left" and "right" name the channels of each side
// (left = bc, right = ab: the pair of the AZBK board, B in both). Each side is
// then at full scale with all of its channels at their loudest.
//
// The same chip may play as a YM2149 ("type", or the ~ym line where the host
// switches it: the AZBK, bit 3 of 0177212). Its envelope has 32 steps, each
// half as long, and its own DAC; a fixed volume v is step 2v + 1. The levels
// are MAME's model of the YM2149 (ay8910.cpp), see YM_VOLUME.

#define AY_REGS             16
#define AY_TONE_CHANNELS    3

class AY8910 : public AddressableDevice, public SoundSource, public Pluggable
{
private:
    Interface i_porta;                  // I/O port A output (register 14), $FF as an input
    Interface i_portb;                  // I/O port B output (register 15), $FF as an input
    // The pins of the chip, for bus = pins (the Oric and the Правец-8Д, where
    // a VIA drives them): BDIR, BC1, the data lines in, and the data the chip
    // puts out in a read cycle
    Interface i_bdir;
    Interface i_bc1;
    Interface i_da;
    Interface i_da_out;
    // 1: play as a YM2149. Optional; without it "type" decides
    Interface i_ym;
    bool m_ym = false;
    void set_ym(bool ym);
    unsigned int env_max() const { return m_ym ? 0x1F : 0x0F; }
    void env_tick();
    // The chip takes the data cycles only after an address cycle whose upper
    // four bits were its own mask code, 0000
    bool m_active = true;
    void pin_cycle();

    unsigned int m_bus;                 // host bus protocol, AY_BUS_*
    unsigned int m_frequency;           // chip clock, Hz
    uint32_t m_step;                    // 16.16 tone ticks per system clock
    uint32_t m_acc;

    unsigned int m_board_chip = 0;      // 1 or 2 on a 2xAY board, 0 alone

    // Channels of each side, bit 0 for A; both 7 for a mono chip. Read by the
    // constructor: the mixer asks sound_stereo() before this device's
    // load_config() when it comes first in the configuration
    bool m_stereo = false;
    bool m_channels_bad = false;        // "left" or "right" names no channel
    unsigned int m_left_mask = 7;
    unsigned int m_right_mask = 7;
    bool m_selected = true;             // this chip takes the data writes
    AY8910 * m_pair = nullptr;          // the first chip, for the second
    AY8910 * m_partner = nullptr;       // the second chip, for the first
    bool plugged() const { return m_pair ? m_pair->m_plugged : m_plugged; }

    // Written to since the reset. Until then every register is 0 and the
    // output is silence whatever the generators do, so the ticks are only
    // counted, and catch_up() brings the generators to where ticking would
    // have left them - exactly, before the first write or a look from a
    // script. The УК-НЦ carries three such chips, rarely used
    bool m_used;
    uint64_t m_idle_ticks;
    void catch_up();

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
    void output_ports();
    unsigned int read_reg(unsigned int reg);
    void restart_envelope();
    void tick();
    unsigned int level(unsigned int mask);
    int32_t side_sample(unsigned int mask, int64_t amplitude);

protected:
    void plug_changed() override;

public:
    AY8910(InterfaceManager *im, EmulatorConfigDevice *cd);
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;

    int32_t sound_sample(int64_t amplitude) override;
    bool sound_stereo() override { return m_stereo; }
    int32_t sound_sample_left(int64_t amplitude) override;
    int32_t sound_sample_mono(int64_t amplitude) override;
    bool sound_active() override;
    // Before the first write every register is 0 and the output is silence;
    // after it the generators move the level on their own
    bool sound_volatile() override { return m_used; }
    const char * plug_title() const override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_ay8910(InterfaceManager *im, EmulatorConfigDevice *cd);
