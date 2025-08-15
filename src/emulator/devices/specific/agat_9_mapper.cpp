// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat-9 memory manager

#include "agat_9_mapper.h"
#include "emulator/utils.h"

Agat9Mapper::Agat9Mapper(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{}

void Agat9Mapper::load_config(SystemData *sd)
{
    AddressableDevice::load_config(sd);

    m_pm = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("pm").value));
    m_ram = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_bios = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("bios").value));
    m_page_map = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("page_map").value));
    Port * rom_mode = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("rom_mode").value));

}

unsigned Agat9Mapper::get_value(unsigned address)
{
    unsigned v = _FFFF;

    if (address < 0xC000) {
        // RAM
        unsigned segment = address / 8192;
        unsigned bank = m_page_map->get_value(segment);
        unsigned offset = address & 0x1FFF;
        v = m_ram->get_value(bank*8192 + offset);
    }

    return v;
}

void Agat9Mapper::set_value(unsigned int address, unsigned int value, bool force)
{
    if (address < 0xC000) {
        // RAM
        unsigned segment = address / 8192;
        unsigned bank = m_page_map->get_value(segment);
        unsigned offset = address & 0x1FFF;
        m_ram->set_value(bank*8192 + offset, value);
    }
}

ComputerDevice * create_agat_9_mapper(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat9Mapper(im, cd);
}
