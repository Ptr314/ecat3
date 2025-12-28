// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: RAM IC using a part of address to store a value

#include "ram_address.h"
#include "emulator/utils.h"
#include <iostream>

RAMAddress::RAMAddress(InterfaceManager *im, EmulatorConfigDevice *cd):
    RAM(im, cd)
    , m_address_shift(4)
    , m_address_mask(0xF)
    , m_value_shift(0)
    , m_value_mask(0xF)
    , m_store_on_read(false)
    , i_we(this, im, 1, "we", MODE_R)
    , i_access(this, im, 1, "access", MODE_W)

{}

void RAMAddress::load_config(SystemData *sd)
{
    RAM::load_config(sd);
    m_address_shift = read_confg_value(cd, "address_shift", false, m_address_shift);
    m_address_mask = read_confg_value(cd, "address_mask", false, m_address_mask);
    m_value_shift = read_confg_value(cd, "value_shift", false, m_value_shift);
    m_value_mask = read_confg_value(cd, "value_mask", false, m_value_mask);
    m_store_on_read = read_confg_value(cd, "store_on_read", false, false);
}

unsigned RAMAddress::get_value(unsigned address)
{
    unsigned a = (address >> m_address_shift) & m_address_mask;
    unsigned v;
    if (m_store_on_read) {
        v = (address >> m_value_shift) & m_value_mask;
        RAM::set_value(a, v, false);
    } else {
        v = RAM::get_value(a);
    }
    // if (name != "port-C1xx" && name != "port-pal")
    //     std::cout << name.toStdString() << ": R " << std::to_string(a) << ":" << std::to_string(v) << std::endl;
    i_access.change(0);
    i_access.change(1);
    return (a << m_address_shift) | (v << m_value_shift) ;
}

void RAMAddress::set_value(unsigned int address, unsigned int value, bool force)
{
    i_access.change(0);
    if ((i_we.value & 1) == 1) {
        unsigned a = (address >> m_address_shift) & m_address_mask;
        unsigned v = (address >> m_value_shift) & m_value_mask;

        RAM::set_value(a, v, force);

        // if (name != "port-C1xx" && name != "port-pal")
        //     std::cout << name.toStdString() << ": W " << std::to_string(a) << ":" << std::to_string(v) << std::endl;

    } else {
        std::cout << "W " + std::to_string(address) + ": blocked by WE";
    }
    i_access.change(1);
}

ComputerDevice * create_ram_address(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new RAMAddress(im, cd);
}
