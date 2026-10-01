// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat sound synthesis card 5/2 (ЯЗС 5/2)

#pragma once

#include <cstdint>
#include <vector>

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"

// The card of the Novosibirsk computer laboratory: five tone channels, two
// drum channels and a timer interrupt, all on the 16 registers of a slot
// (C0n0-C0nF, n = slot + 8). Described at agatcomp.ru (jzs52.shtml); the
// schematic is docs-external/Agat/sound/agat-sound-schem.pdf.
//
// Two КР580ВИ53 count the tones (1.02 MHz) and the interrupt (7980 Hz); their
// GATE inputs are all one enable bit. Each tone channel is an analog circuit
// - an envelope built by RC networks and diodes from two latch bits, a
// transistor key chopping it with the counter output, a coupling stage - that
// puts its signal on up to three buses, and the buses go through passive band
// filters into an op-amp mixer. That part is simulated as the circuit it is,
// from the component values, by backward Euler on small nodal systems; the
// drums are their CMOS multivibrators and RC envelopes, simplified.
//
// The card has no audio output of its own: it is a SoundSource mixed into the
// machine's speaker (mix = yazs). Its interrupt is an open collector on the
// IRQ line of the bus, which the core has no wired-AND for: the card takes the
// line of the rest of the machine on irq_in and drives irq with both.

// One counter of a 580ВИ53, stepped by events instead of clocks: until_event()
// says how many clocks the output keeps its level, advance() moves by up to
// that many. Nothing of it can be read on this card (its RD is tied high), so
// there is no read path and no latch.
struct Vi53Counter
{
    enum { IDLE = 0, LOAD, RUN, WAIT, PAUSED, DONE };

    uint8_t mode = 0;           // 0..5
    uint8_t rw = 3;             // 1 LSB, 2 MSB, 3 LSB then MSB
    bool bcd = false;
    bool msb_next = false;
    uint8_t lsb = 0;
    uint32_t n = 0;             // count in effect, 1..65536
    uint32_t pending_n = 0;     // a new count of mode 2/3, taken at the next reload
    bool has_pending = false;
    bool gate = true;
    bool out = true;
    uint8_t state = IDLE;
    uint8_t phase = 0;          // modes 2/4/5: 1 while the one-clock low pulse lasts
    uint32_t left = 0;          // RUN: clocks to the next event

    static const uint32_t NEVER = 0xFFFFFFFFu;

    void control(unsigned int cw);
    void write(unsigned int value);
    void set_gate(bool g);
    uint32_t until_event() const;
    void advance(uint32_t k);

private:
    void load();
    void event();
};

class AgatYazs : public AddressableDevice, public SoundSource
{
public:
    static const int TONES = 5;
    static const int COUNTERS = 6;
    static const int CH_NODES = 6;      // B, C, F, G, H, J of a tone channel
    static const int BUS_NODES = 9;     // MH, LH, HH, Lm, Hm, N48, SOUND, Mc, Hc

private:
    Interface i_irq_in;                 // IRQ of the rest of the machine, active low
    Interface i_irq;                    // IRQ to the processor, active low
    Interface i_ext;                    // external interrupt input of the rear connector

    // Counters 0-2 are the tones 1-3 (first chip, C0n0-C0n3), 3-4 the tones
    // 4-5 and 5 the interrupt timer (second chip, C0n4-C0n7)
    Vi53Counter m_cnt[COUNTERS];

    uint8_t m_regs[16];                 // what was last written to each address
    bool m_cet;                         // counting and timer interrupt enabled (!D7 of C0nF)
    bool m_ext_en;                      // external interrupt enabled (!D6 of C0nF)
    bool m_flag_t;                      // timer interrupt flag
    bool m_flag_e;                      // external interrupt flag
    uint32_t m_irqs;                    // times the timer flag was set, for scripts
    unsigned int m_port_value;          // what the external port reads

    // Time. The model advances in steps of m_substep processor clocks; the
    // tone counters run at the processor clock, the noise register at 1/16
    // of it and the interrupt counter at 1/128 (one divider chain, D31/D32)
    unsigned int m_substep;
    unsigned int m_pending;             // clocks not yet stepped
    unsigned int m_div;                 // position in the 1/128 divider
    uint32_t m_lfsr;                    // К176ИР10 as a 17-bit shift register

    // Tone channels: node voltages, the base drive of the key (X = voltage
    // behind C2) and the levels of the latch bits
    double m_v[TONES][CH_NODES];
    double m_x[TONES];
    bool m_tout[TONES];                 // counter output the base drive last saw
    bool m_d2[TONES], m_d3[TONES];      // diode states, kept as the next guess

    // Buses, band filter and mixer
    double m_bus[BUS_NODES];
    double m_vo;                        // mixer output, volts from its bias

    // Drum 6
    double m_d6_c27, m_d6_e, m_d6_p0, m_d6_c30, m_d6_c31, m_d6_nz;
    double m_d6_a33, m_d6_c35, m_d6_c32, m_d6_n, m_d6_m, m_d6_c36;
    bool m_d6_o1, m_d6_o2;
    // Drum 7
    double m_d7_c37, m_d7_e, m_d7_c39, m_d7_c40, m_d7_k, m_d7_c42;
    bool m_d7_o3, m_d7_o4;
    double m_noise_v;                   // NOISE after its RC filter

    // Output accumulated over the clocks of the current slice
    double m_out_sum;
    unsigned int m_out_count;
    double m_out;                       // the last slice's average
    double m_peak[7];                   // highest envelope since the last write: tones, drums

    // Nothing sounds and nothing moves: the analog part is not stepped
    bool m_sleeping;
    unsigned int m_quiet;
    // Steps a channel (the drums) has stood still; past IDLE_STEPS it is not
    // stepped until a write touches it
    static const unsigned int IDLE_STEPS = 4096;
    uint32_t m_ch_quiet[TONES];
    uint32_t m_dr_quiet;
    bool m_dr_silent = true;            // the last step let nothing through the drums
    double m_delta;                     // largest change of the current step

    // Derived from the clock, not state
    double m_h;                         // step, seconds
    double m_tick_on, m_tick_off;       // base drive decay per clock, key on and off
    double m_gain;
    std::vector<double> m_ch_inv;       // 16 configurations x 36
    std::vector<double> m_bus_inv;      // 216 configurations x 81, filled on demand
    std::vector<char> m_bus_valid;

    void write_timer(unsigned int chip, unsigned int reg, unsigned int value);
    void set_cet(bool cet);
    void irq_edge(bool old_out);
    void update_irq();
    unsigned int status() const;

    void run(unsigned int clocks);
    void step();
    double tone_step(int c, unsigned int on_clocks);
    double tone_solve(int c, bool key, double *x);
    void bus_step(const double *chout, double i_drums);
    const double * bus_inverse(unsigned int config);
    double drums_step(double vs, bool full);
    void wake();
    void wake_channel(int c);
    void wake_drums();
    void cold_analog();
    void prepare();
    void analog_vars(std::vector<double*> &v);

public:
    AgatYazs(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;

    int32_t sound_sample(int64_t amplitude) override;
    bool sound_volatile() override { return !m_sleeping; }

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_agat_yazs(InterfaceManager *im, EmulatorConfigDevice *cd);
