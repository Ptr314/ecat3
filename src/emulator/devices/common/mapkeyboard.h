// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port-based keyboard device

#pragma once

#include "emulator/utils.h"     // _FFFF в инициализаторе поля ниже
#include "emulator/devices/common/keyboard.h"

struct KeyMapData {
    unsigned int key_code;
    unsigned int value;
    bool shift;
    bool ctrl;
    bool rus;
};

// The same entry addressed by the machine's own key id instead of a host code.
// Both tables describe one keyboard, they just answer different questions:
// "which host key reaches this" and "what this key of the machine sends".
struct KeyIdData {
    std::string  id;
    unsigned int value;
    bool shift;
    bool ctrl;
    bool rus;
    //Marks the entry a letter key sends when the case latch calls for it (/L).
    //A key without such an entry is left alone by ЗАГЛ and СТР, which is what
    //keeps the digit row working under them.
    bool lcase;
};


class MapKeyboard: public Keyboard
{
private:
    Interface i_ruslat;
    Interface i_ready;
    Interface i_pressed;

    // Keys currently held down, to drive i_pressed. Keys pressed on the drawing
    // are counted separately: they carry an id, not a host code.
    std::vector<unsigned int> keys_held;
    std::vector<std::string> ids_down;
    void update_pressed();

    // Machine key id of a host code, so that typing on the real keyboard lights
    // the drawing up too. Built by matching the two tables on the value they send.
    std::vector<std::pair<unsigned int, std::string> > code_to_id;
    void build_code_to_id();
    std::string id_of_code(unsigned int code) const;

protected:
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed = false;
    unsigned code_ruslat;
    unsigned ruslat_bit;
    unsigned rus_value;
    bool m_use_pin = true;
    bool m_use_codes = false;
    bool m_has_rus_switches = false;
    uint8_t m_rus_switches[2];

    Port * port_value;
    Port * port_ruslat;

    std::vector<KeyMapData> key_map;
    std::vector<KeyIdData> id_map;

    void set_rus(bool new_rus) override;
    void send_key(unsigned int value);

    //What went out last, so that ПОВТ can send it again
    unsigned int m_last_value = _FFFF;
    void repeat_key(const std::string &id, bool press) override;

    emulator::Result parse_key_table(const std::vector<std::string> &body, const std::string &file) override;
    int find_id_entry(const std::string &id, bool shift, bool lcase, bool match_rus) const;
    void send_key_id(const std::string &id, bool press) override;
    void set_shift_state(bool pressed) override { shift_pressed = pressed; }
    void set_ctrl_state(bool pressed) override { ctrl_pressed = pressed; }
    void set_alt_state(bool pressed) override { alt_pressed = pressed; }

public:
    MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;
    bool needs_shift(unsigned int key) override;

    emulator::Result load_config(SystemData *sd) override;

    void reset(bool cool) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
