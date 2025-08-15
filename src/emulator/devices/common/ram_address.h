// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: RAM IC using a part of address to store a value

#pragma once

#include "emulator/core.h"

class RAMAddress:public RAM
{
private:
    unsigned m_size;
    unsigned m_address_shift;
    unsigned m_address_mask;
    unsigned m_value_shift;
    unsigned m_value_mask;
    Interface i_we;
    std::vector<unsigned> m_values;

public:
    RAMAddress(InterfaceManager *im, EmulatorConfigDevice *cd);
    void load_config(SystemData *sd) override;
    unsigned get_value(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;
};

ComputerDevice * create_ram_address(InterfaceManager *im, EmulatorConfigDevice *cd);
