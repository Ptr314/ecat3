// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8257 (КР580ВТ57) DMA controller device

#pragma once

#include "emulator/core.h"

class I8257: public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;

public:
    uint8_t RgA[8];
    uint8_t RgC[8];
    uint8_t PtrA[4];        // Flip-flops to write high and low bytes alternately
    uint8_t PtrC[4];
    uint8_t RgMode;
    uint8_t RgState;

    I8257(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;

    //A powered-up ВТ57 has no channel enabled, and that is what keeps the ВГ75
    //from fetching garbage before the guest programs either of them
    void clear_registers();

    bool channel_enabled(unsigned int channel) const
    {
        return ((RgMode >> (channel & 3)) & 1) != 0;
    }

    //One DMA cycle on a channel: the address the bus master must read, with the
    //channel registers advanced
    unsigned int dma_next(unsigned int channel);

    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_i8257(InterfaceManager *im, EmulatorConfigDevice *cd);
