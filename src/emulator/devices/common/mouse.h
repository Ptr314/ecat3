// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Mouse with latched direction outputs (УВК-01 «Марсианка»)

#pragma once

#include <atomic>

#include "connector.h"

// A mouse of the УВК-01 «Марсианка» kind. It has no counters: every step of
// the ball (0.5 mm) sets a flip-flop of its direction, and the flip-flops
// stay set until the computer pulls the reset line. The buttons are plain
// contacts. A program reads the lines, clears them and reads again, so the
// speed of the pointer is set by how often it polls, one step a poll.
//
// Host movement is counted in steps. The steps are handed out one per poll,
// no faster than the ball could make them: a step waits while its flip-flop
// is still set. With the read strobe of the port wired in, a flip-flop that
// is reset before the program has read it gives its step back, so the program
// gets every step the host made. The real mouse loses such steps - programs
// often clear the lines twice a poll - which only makes the pointer lag.
class Mouse : public PluggableDevice
{
private:
    Interface i_out;
    Interface i_reset;
    Interface i_read;                   // strobe of every read of the lines, optional

    unsigned int m_bit_up, m_bit_right, m_bit_down, m_bit_left;
    unsigned int m_bit_button1, m_bit_button2;

    unsigned int m_period;              // system clock counts between two steps
    unsigned int m_ticks;
    unsigned int m_latch;               // direction flip-flops, as output bits
    unsigned int m_unread;              // flip-flops set since the last read
    unsigned int m_buttons_out;         // buttons, as output bits
    unsigned int m_state;               // what is on the output lines

    // Written by the host (GUI or script) thread, taken by the emulation one
    std::atomic<int> m_dx;              // steps to the right, left if negative
    std::atomic<int> m_dy;              // steps down, up if negative
    std::atomic<unsigned> m_buttons;    // bit 0 - button 1, bit 1 - button 2

    bool reset_held() const;
    void update();
    static void add_pending(std::atomic<int> &axis, int delta);

protected:
    void plug_changed() override;

public:
    Mouse(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void system_clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    const char * plug_title() const override;

    // Called by the emulator: movement in steps, buttons as a mask, a negative
    // mask leaves the buttons as they are
    void move(int dx, int dy, int buttons);

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_mouse(InterfaceManager *im, EmulatorConfigDevice *cd);
