// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 memory manager

#pragma once

#include "emulator/core.h"

class Agat9Mapper:public AddressableDevice
{
private:
    Port * m_pm;
    RAM * m_ram;
    ROM * m_bios;
    RAM * m_page_map;
    Port * rom_mode;

public:
    Agat9Mapper(InterfaceManager *im, EmulatorConfigDevice *cd);
    void load_config(SystemData *sd) override;
    unsigned get_value(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;
};

ComputerDevice * create_agat_9_mapper(InterfaceManager *im, EmulatorConfigDevice *cd);

