// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Multiplexer IC device

#pragma once

#include "emulator/core.h"

class Multiplexer:public ComputerDevice
{
private:
    Interface i_a;
    Interface i_b;
    Interface i_out;
    Interface i_s;

public:
    Multiplexer(InterfaceManager *im, EmulatorConfigDevice *cd);
    void interface_callback(unsigned callback_id, unsigned new_value, unsigned old_value) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_mux(InterfaceManager *im, EmulatorConfigDevice *cd);
