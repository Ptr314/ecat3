// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Правец-8 keyboard (82: Apple ][ latch, 8А/8С: the CM650 one)

#pragma once

#include <deque>

#include "emulator/devices/common/keyboard.h"
#include "emulator/thread_compat.h"
#include "apple2_iou.h"

// The keyboard of a Правец as the main board sees it: a 7-bit code with a
// strobe, the "a key is held" line (AKD) and, on the 8А/8С, an eighth bit.
// The CM650 microcomputer of the 8А/8С keyboard is not run: the codes come
// from a table, host key -> code, with the modifiers
//   S - Shift held, R - the Cyrillic alphabet (C/L Lock), 8 - the 8-bit code
//   table (MODE1, SOFTSW from the main board)
// and the rule of the book (П8.2, П8.4): in the 8-bit table a letter key
// sends its 7-bit code less $80 - the small letter - unless Shift or Caps
// Lock asks for the capital. An entry marked 8 is sent as it is
class PravetzKeyboard: public Keyboard, public Apple2KeyLatch
{
private:
    struct Entry {
        unsigned int host;
        unsigned int code;
        bool shift;
        bool rus;
        bool mode8;
    };
    std::vector<Entry> m_map;

    Apple2IOU * m_iou = nullptr;
    unsigned int m_key_ruslat = _FFFF;  // C/L Lock (82: ЛАТ)
    unsigned int m_key_caps = _FFFF;    // Caps Lock
    unsigned int m_key_reset = _FFFF;   // RESET, only with CTRL
    unsigned int m_key_f1 = _FFFF;      // F1, F2: the buttons PB0, PB1
    unsigned int m_key_f2 = _FFFF;
    bool m_has_kd7 = true;              // 8А/8С: the eighth bit and the 8-bit table

    Interface i_pb;                     // to the IOU
    Interface i_led_caps;
    Interface i_led_cl;

    // The main board's side, on the emulation thread only
    unsigned int m_latch = 0;           // the code, bit 7: KD7
    bool m_strobe = false;
    unsigned int m_held = 0;
    std::vector<unsigned int> m_down;   // host keys that sent a code and are held
    bool m_shift = false;
    bool m_ctrl = false;
    bool m_caps = false;
    unsigned int m_pb = 0;
    unsigned int m_codes = 0;
    // Auto repeat of the IOU: a key held 0.5 s strobes its code again, 15
    // times a second (the book, 2.1)
    uint64_t m_repeat_clock = 0;
    uint64_t m_repeat_delay = 0;
    uint64_t m_repeat_period = 0;

    // Host keys arrive on the GUI thread, the latch lives on the emulation one
    std::deque<std::pair<unsigned int, bool> > m_queue;
    compat_mutex m_queue_mutex;
    volatile bool m_queued = false;
    void process(unsigned int key, bool press);
    int find(unsigned int key, bool shift, bool rus, bool mode8) const;
    void update_leds();

protected:
    void set_rus(bool new_rus) override;

public:
    PravetzKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    ConfigFields get_config_fields() override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    bool needs_shift_char(char ch, unsigned int key) override;
    bool key_for_char(unsigned int codepoint, unsigned int &key, bool &shift) override;
    unsigned int rus_switch_key() const override { return m_key_ruslat; }



    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;

    unsigned kbd_latch() const override { return (m_latch & 0x7F) | (m_strobe ? 0x80 : 0); }
    void kbd_clear_strobe() override { m_strobe = false; }
    bool kbd_any_down() const override { return m_held > 0; }
    bool kbd_bit7() const override { return (m_latch & 0x80) != 0; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;
};

ComputerDevice * create_pravetz_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
