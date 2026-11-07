// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: System registers of the Электроника МС 0585 module НС1

#pragma once

#include "emulator/core.h"

// Addresses from the start of the range, 17773600:
//   000-076  identification PROM of the machine, a byte on each word (155РЕ3)
//   100      memory configuration, monitor present, bit 7 - halt enable
//   102      slots in use
//   104      error lamps: 0 lights, reads 0
//   200      17777750, the bus maintenance register: reads 0
//
// The autotest copies the first eight bytes of the PROM into the configuration
// table and clears them if their sum (the same as of a module ROM) is not zero.
// No dump of the chip of an Э-85 is known: the first four bytes are serial, the
// next two zero, the last two make the sum come out. The machine takes the number
// of the DEC Pro 350 PROM in Xhomer, since VENIX/PRO 2.0 ties its licence to it.
class E85Sys: public AddressableDevice
{
private:
    uint8_t m_id[32] = {};
    unsigned int m_config = 0;
    unsigned int m_slots = 0;
    bool m_halt_enable = false;
    unsigned int m_lamps = 0xF;

public:
    E85Sys(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_e85_sys(InterfaceManager *im, EmulatorConfigDevice *cd);
