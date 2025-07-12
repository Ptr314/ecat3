// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: RAM pages memory manager device

#pragma once

#include "emulator/core.h"

class PageMapper:public AddressableDevice
{
private:
    Interface i_page;
    Interface i_segment;

    unsigned int PagesCount;
    Memory * pages[32];
    unsigned int Frame;
    unsigned int PageMask;
    unsigned int SegmentMask;
    unsigned int address_mask;

public:
    PageMapper(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd) override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
};

ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd);
