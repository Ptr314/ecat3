// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MOS 6522 versatile interface adapter (VIA)

#pragma once

#include <cstdint>

#include "emulator/core.h"

// Two 8-bit ports with handshake lines, two 16-bit timers, a shift register
// and an interrupt controller, clocked by the processor's phi2.
//
//   0 ORB/IRB   1 ORA/IRA (with handshake)   2 DDRB   3 DDRA
//   4/5 T1 counter (a read of 4 clears the T1 flag, a write of 5 loads and
//   starts it)   6/7 T1 latch   8/9 T2 counter   A SR   B ACR   C PCR
//   D IFR   E IER   F ORA without handshake
//
// A pin whose DDR bit is 0 is an input and is put out as 1 (pulled up); a
// read gives the output latch for the output pins and the input lines for
// the others. In the T1 PB7 mode (ACR bit 7) PB7 is the timer's output.
// The shift register keeps its value and its flag, but does not shift: a
// mode that would is counted (note_unsupported()).
class VIA6522: public AddressableDevice
{
private:
    Interface i_pa;         // port A pins as driven
    Interface i_pa_in;      // what drives port A from outside
    Interface i_pb;
    Interface i_pb_in;
    Interface i_ca1;
    Interface i_ca2;        // an output in the PCR output modes
    Interface i_ca2_in;
    Interface i_cb1;
    Interface i_cb2;
    Interface i_cb2_in;
    Interface i_irq;        // ~IRQ, 0 while an enabled flag is set

    uint8_t m_ora = 0, m_orb = 0, m_ddra = 0, m_ddrb = 0;
    uint8_t m_ira_latch = 0, m_irb_latch = 0;   // latched on CA1/CB1 (ACR bits 0/1)
    uint8_t m_acr = 0, m_pcr = 0, m_ifr = 0, m_ier = 0, m_sr = 0;
    uint16_t m_t1_latch = 0xFFFF;
    uint16_t m_t1 = 0xFFFF;
    uint16_t m_t2_latch_lo = 0xFF;
    uint16_t m_t2 = 0xFFFF;
    bool m_t1_reload = false;   // the counter takes the latch on the next clock
    bool m_t1_armed = false;    // a one-shot that has not timed out yet
    bool m_t2_armed = false;
    bool m_t2_hold = false;     // a T2 just loaded starts counting a clock later
    bool m_pb7 = true;          // the T1 output on PB7
    bool m_ca2_out = true, m_cb2_out = true;
    bool m_ca2_pulse = false, m_cb2_pulse = false;  // a pulse output, back high next clock
    unsigned m_irq_out = 1;

    void set_flag(uint8_t bits);
    void clear_flag(uint8_t bits);
    void update_irq();
    void output_a();
    void output_b();
    void set_ca2(bool v);
    void set_cb2(bool v);
    void apply_pcr();
    void ca2_access();
    void cb2_access();
    void t1_run(unsigned n);
    void t2_run(unsigned n);
    uint8_t read_a() const;
    uint8_t read_b() const;
    unsigned read_reg(unsigned reg) const;
    void ca1_edge(bool level);
    void cb1_edge(bool level);
    void ca2_edge(bool level);
    void cb2_edge(bool level);

public:
    VIA6522(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    unsigned int clocks_to_event() const override;
    bool has_clock_events() const override { return true; }
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_via6522(InterfaceManager *im, EmulatorConfigDevice *cd);
