// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AY-3-8910 programmable sound generator

#include <cstring>

#include "ay8910.h"
#include "emulator/utils.h"

// Host bus protocols
#define AY_BUS_DIRECT   0   // even address: register select, odd address: data
#define AY_BUS_BK       1   // БК, port 0177714: a word write selects a register,
                            // a byte write sends data; both are inverted by the port

// БК sound boards run the chip from 12 MHz / 7
#define AY_DEFAULT_FREQUENCY    1714275

// Output level of the 16 volume steps, 0..1000 (logarithmic DAC of the 8910)
static const unsigned int AY_VOLUME[16] = {
    0, 14, 21, 29, 42, 62, 85, 137, 169, 265, 353, 450, 570, 728, 869, 1000
};

// Bits actually stored by each register
static const uint8_t AY_REG_MASK[AY_REGS] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
    0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
};

AY8910::AY8910(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_porta(this, im, 8, "porta", MODE_W)
    , i_portb(this, im, 8, "portb", MODE_W)
    , m_bus(AY_BUS_DIRECT)
    , m_frequency(AY_DEFAULT_FREQUENCY)
    , m_step(0)
    , m_acc(0)
    , m_latch(0)
{
    m_clocked = true;   //clock() is overridden here
    addresable_size = 2;
    can_read = true;
    can_write = true;
    memset(m_regs, 0, sizeof(m_regs));
    reset(true);
}

emulator::Result AY8910::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    std::string bus = str_tolower(cd->get_parameter("bus", false).value);
    if (bus.empty() || bus == "direct")
        m_bus = AY_BUS_DIRECT;
    else if (bus == "bk")
        m_bus = AY_BUS_BK;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{AY8910|" + std::string(QT_TRANSLATE_NOOP("AY8910", "Unknown bus protocol")) + "} " + bus);

    m_frequency = read_confg_value(cd, "frequency", false, (unsigned int)AY_DEFAULT_FREQUENCY);
    if (m_frequency == 0 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{AY8910|" + std::string(QT_TRANSLATE_NOOP("AY8910", "Incorrect clock frequency")) + "}");

    // Tone ticks per system clock, 16.16
    m_step = (uint32_t)(((uint64_t)m_frequency << 16) / 8 / m_system_clock);

    return emulator::Result::ok();
}

void AY8910::reset(MAYBE_UNUSED bool cold)
{
    memset(m_regs, 0, sizeof(m_regs));
    m_latch = 0;
    m_acc = 0;
    for (unsigned int c = 0; c < AY_TONE_CHANNELS; c++) {
        m_tone_count[c] = 0;
        m_tone_out[c] = 0;
    }
    m_prescale = 0;
    m_noise_count = 0;
    m_rng = 1;
    restart_envelope();
    i_porta.change(0);
    i_portb.change(0);
}

//------------------- Registers --------------------------------------------//

void AY8910::select(unsigned int reg)
{
    m_latch = reg & 0x0F;
}

void AY8910::write_reg(unsigned int reg, unsigned int value)
{
    if (reg >= AY_REGS) return;
    m_regs[reg] = (uint8_t)(value & AY_REG_MASK[reg]);
    switch (reg) {
    case 13:
        restart_envelope();
        break;
    case 14:
        i_porta.change(m_regs[reg]);
        break;
    case 15:
        i_portb.change(m_regs[reg]);
        break;
    default:
        break;
    }
}

unsigned int AY8910::read_reg(unsigned int reg)
{
    return (reg < AY_REGS) ? m_regs[reg] : 0xFF;
}

void AY8910::restart_envelope()
{
    unsigned int shape = m_regs[13];
    m_env_attack = (shape & 0x04) ? 0x0F : 0x00;
    if ((shape & 0x08) == 0) {
        // Shapes 0-7: one slope, then silence
        m_env_hold = true;
        m_env_alternate = (m_env_attack != 0);
    } else {
        m_env_hold = (shape & 0x01) != 0;
        m_env_alternate = (shape & 0x02) != 0;
    }
    m_env_count = 0;
    m_env_step = 15;
    m_env_holding = false;
    m_env_volume = (unsigned int)m_env_step ^ m_env_attack;
}

//------------------- Generators -------------------------------------------//

// One tone tick, eight chip clocks
void AY8910::tick()
{
    for (unsigned int c = 0; c < AY_TONE_CHANNELS; c++) {
        unsigned int period = m_regs[c * 2] | (m_regs[c * 2 + 1] << 8);
        if (period == 0) period = 1;
        if (++m_tone_count[c] >= period) {
            m_tone_count[c] = 0;
            m_tone_out[c] ^= 1;
        }
    }

    // Noise and envelope are clocked at half the tone rate
    m_prescale ^= 1;
    if (m_prescale) return;

    unsigned int noise_period = m_regs[6];
    if (noise_period == 0) noise_period = 1;
    if (++m_noise_count >= noise_period) {
        m_noise_count = 0;
        // 17-bit LFSR with taps at bits 0 and 3
        m_rng ^= (((m_rng & 1) ^ ((m_rng >> 3) & 1)) << 17);
        m_rng >>= 1;
    }

    if (!m_env_holding) {
        unsigned int env_period = m_regs[11] | (m_regs[12] << 8);
        if (env_period == 0) env_period = 1;
        if (++m_env_count >= env_period) {
            m_env_count = 0;
            m_env_step--;
            if (m_env_step < 0) {
                if (m_env_hold) {
                    if (m_env_alternate) m_env_attack ^= 0x0F;
                    m_env_holding = true;
                    m_env_step = 0;
                } else {
                    if (m_env_alternate) m_env_attack ^= 0x0F;
                    m_env_step = 15;
                }
            }
            m_env_volume = (unsigned int)m_env_step ^ m_env_attack;
        }
    }
}

void AY8910::clock(unsigned int counter)
{
    m_acc += counter * m_step;
    while (m_acc >= 0x10000) {
        m_acc -= 0x10000;
        tick();
    }
}

// Sum of the three channels, 0..3000
unsigned int AY8910::level()
{
    unsigned int mixer = m_regs[7];
    unsigned int noise = m_rng & 1;
    unsigned int sum = 0;
    for (unsigned int c = 0; c < AY_TONE_CHANNELS; c++) {
        // Mixer bits are active low: a set bit disables the source
        unsigned int tone_on  = m_tone_out[c] | ((mixer >> c) & 1);
        unsigned int noise_on = noise | ((mixer >> (c + 3)) & 1);
        if (tone_on && noise_on) {
            unsigned int volume = m_regs[8 + c];
            unsigned int step = (volume & 0x10) ? m_env_volume : (volume & 0x0F);
            sum += AY_VOLUME[step];
        }
    }
    return sum;
}

int32_t AY8910::sound_sample(int64_t amplitude)
{
    return (int32_t)((int64_t)level() * amplitude / 1500 - amplitude);
}

//------------------- Bus --------------------------------------------------//

unsigned int AY8910::get_value(unsigned int address)
{
    if (m_bus == AY_BUS_BK) return 0xFF;
    return (address & 1) ? read_reg(m_latch) : m_latch;
}

void AY8910::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (m_bus == AY_BUS_BK) {
        // A byte write to the port: only the low byte reaches the data lines
        if ((address & 1) == 0) write_reg(m_latch, (value ^ 0xFF) & 0xFF);
        return;
    }
    if (address & 1)
        write_reg(m_latch, value);
    else
        select(value);
}

unsigned int AY8910::get_value_word(unsigned int address)
{
    if (m_bus == AY_BUS_BK) return 0xFFFF;
    return get_value(address & ~1) | (get_value(address | 1) << 8);
}

void AY8910::set_value_word(unsigned int address, unsigned int value, bool force)
{
    if (m_bus == AY_BUS_BK) {
        // A word write to the port: the low byte selects the register
        select(value ^ 0xFF);
        return;
    }
    set_value(address & ~1, value & 0xFF, force);
    set_value(address | 1, (value >> 8) & 0xFF, force);
}

unsigned int AY8910::get_direct(unsigned int address)
{
    return (address & 1) ? read_reg(m_latch) : m_latch;
}

//------------------- Introspection ----------------------------------------//

std::vector<DeviceFieldInfo> AY8910::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"regs",     "Registers 0-15; without a range all of them",  true});
    r.push_back({"register", "Selected register",                           false});
    r.push_back({"level",    "Output level, 0-3000",                        false});
    r.push_back({"envelope", "Current envelope volume, 0-15",               false});
    return r;
}

bool AY8910::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "regs") {
        if (from == 0 && to == 0) to = AY_REGS - 1;
        if (to >= AY_REGS) to = AY_REGS - 1;
        if (from > to) from = to;
        out.width = 8;
        out.has_start = true;
        out.start = from;
        for (unsigned int i = from; i <= to; i++) out.values.push_back(m_regs[i]);
        return true;
    }
    if (field == "register") { out.values.push_back(m_latch);       return true; }
    out.width = 16;
    if (field == "level")    { out.values.push_back(level());       return true; }
    out.width = 0;
    if (field == "envelope") { out.values.push_back(m_env_volume);  return true; }

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

ComputerDevice * create_ay8910(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AY8910(im, cd);
}
