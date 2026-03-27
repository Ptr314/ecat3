// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port-based keyboard device

#pragma once

#include "emulator/devices/common/keyboard.h"

struct KeyMapData {
    unsigned int key_code;
    unsigned int value;
    bool shift;
    bool ctrl;
    bool rus;
};


class MapKeyboard: public Keyboard
{
private:
    Interface i_ruslat;
    Interface i_ready;

protected:
    bool shift_pressed;
    bool ctrl_pressed;
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

    void set_rus(bool new_rus) override;
    void send_key(unsigned int value);

public:
    MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;

    emulator::Result load_config(SystemData *sd) override;

    void reset(bool cool) override;
};

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
