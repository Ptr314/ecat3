// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple II / //e input-output unit (Правец-82/8М/8А/8С, CM632)

#pragma once

#include "emulator/core.h"

class Apple2MMU;

// What the IOU reads of a keyboard: the code latch with its strobe, the "a key
// is held" line, and on a Правец-8А the eighth bit of the code
class Apple2KeyLatch
{
public:
    virtual ~Apple2KeyLatch() {}
    // The seven bits of the last code, with the strobe in bit 7
    virtual unsigned kbd_latch() const = 0;
    virtual void kbd_clear_strobe() = 0;
    virtual bool kbd_any_down() const = 0;
    // KD7 of the Правец keyboard, latched with the strobe
    virtual bool kbd_bit7() const = 0;
};

// The display the IOU asks for the vertical blanking ($C019)
class Apple2VideoSource
{
public:
    virtual ~Apple2VideoSource() {}
    virtual bool in_vbl() const = 0;
};

// The soft switches of the //e that are not memory switches, the keyboard
// registers, the game port and, on a Правец, the code table switch at $C060.
// Mapped at $C000-$C07F except $C030 (the speaker, a port of its own). The
// memory switches written here go on to the MMU
class Apple2IOU: public AddressableDevice
{
public:
    enum VideoFlag {
        VF_TEXT = 1, VF_MIXED = 2, VF_PAGE2 = 4, VF_HIRES = 8,
        VF_80COL = 0x10, VF_ALTCHAR = 0x20, VF_AN3 = 0x40, VF_80STORE = 0x80
    };

private:
    Apple2MMU * m_mmu = nullptr;
    Apple2KeyLatch * m_kbd = nullptr;
    Apple2VideoSource * m_video = nullptr;
    bool m_iie = true;
    bool m_pravetz = false;         // $C060 is the keyboard's, not the tape's
    bool m_altchar_on_read = false; // the Правец-8А switches ALTCHAR on a read too

    unsigned m_flags = 0;           // VideoFlag
    unsigned m_an = 0;              // AN0-AN3
    bool m_softsw = false;          // Правец: the 8-bit code table

    Interface i_pb;                 // buttons PB0-PB2, 1: pressed
    Interface i_tape_in;            // the 82/8M: tape input in $C060 bit 7
    Interface i_tape_out;           // toggled by $C020
    Interface i_softsw;             // to the keyboard
    Interface i_an;                 // AN0-AN3 out to the game port
    unsigned m_pb_fixed = 0;        // jumper X18: PB2 always 1

    unsigned m_pdl[4] = {255, 255, 255, 255};
    // The paddle timers count the processor's clocks: read off its bus time
    // at the access, so the device needs no clocking of its own
    uint64_t m_pdl_start = 0;
    bool m_pdl_started = false;
    unsigned m_pdl_elapsed = 0;     // from a snapshot, until state_restored()
    uint64_t now() const;

    void set_flag(unsigned f, bool on);
    unsigned status(unsigned address);
    void access(unsigned address, bool read, unsigned value);

public:
    Apple2IOU(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;

    unsigned video_flags() const { return m_flags; }
    bool softsw() const { return m_softsw; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;
};

ComputerDevice * create_apple2_iou(InterfaceManager *im, EmulatorConfigDevice *cd);
