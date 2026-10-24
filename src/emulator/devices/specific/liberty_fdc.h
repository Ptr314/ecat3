// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Liberty Drive, a КР1818ВГ93 floppy card for Apple //e / Правец-8А

#pragma once

#include "emulator/core.h"

class WD1793;

// The glue of the card around its ВГ93, as its ROM («Slim3.05») uses it:
//   $C0n0-$C0n3  the ВГ93; bit 7 of its status is the card's NOT READY, the
//                inverse of latch bit 6 (cleared by the reset, so the driver
//                looks for its drives again after one)
//   $C0n4        read: bit 7 = DRQ
//   $C0n8        write: the latch - bit 0 the drive, 1 the side, 6 set by the
//                driver in use, 7 double step
class LibertyFDC: public AddressableDevice
{
private:
    WD1793 * m_fdc = nullptr;
    Interface i_latch;
    Interface i_drive;      // the drive select of the fdd devices, 0 or 1
    Interface i_side;       // 1: side 1
    void drive_lines();
    unsigned m_latch = 0;

public:
    LibertyFDC(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_liberty_fdc(InterfaceManager *im, EmulatorConfigDevice *cd);
