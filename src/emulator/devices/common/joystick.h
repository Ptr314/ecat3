// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Digital joystick driven by host keys

#pragma once

#include <vector>

#include "emulator/core.h"

// A set of contacts, each closed by a host key. The closed contacts are
// presented as bits on the output interface, so a machine reads them through
// whatever port the real joystick was plugged into. The key-to-bit table
// comes from a map file in the format of the keyboard maps.
class Joystick : public ComputerDevice
{
private:
    struct Contact { unsigned int key; unsigned int bits; };

    Interface i_out;
    std::vector<Contact> m_contacts;
    std::vector<unsigned int> m_held;   // keys currently down
    unsigned int m_state;

    void update();

public:
    Joystick(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    // Called by the emulator for every host key, alongside the keyboard
    void key_event(unsigned int key, bool press);

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_joystick(InterfaceManager *im, EmulatorConfigDevice *cd);
