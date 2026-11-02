// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: System registers of the Электроника МС 0585 module НС1

#include <cstring>

#include "e85_sys.h"
#include "emulator/utils.h"

#define REG_CONFIG  0100
#define REG_SLOTS   0102
#define REG_LAMPS   0104
// 17777560-566, the console terminal registers. With no terminal on pin 9 of the
// "Печать" connector they answer the bus with zeros and take writes: the terminal
// driver of ПРОС sets and clears 17777564 around every character it prints, and
// the factory printer cables leave that pin open
#define REG_CONSOLE 0220

#define HALT_ENABLE 0200

E85Sys::E85Sys(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
{
    can_read = true;
    can_write = true;
    addresable_size = REG_CONSOLE + 010;
}

emulator::Result E85Sys::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    // Table 11: two memory blocks and whether a monitor is plugged in
    m_config = read_confg_value(cd, "memory", false, (unsigned int)0) & 0177;
    m_slots = read_confg_value(cd, "slots", false, (unsigned int)0) & 0377;

    const unsigned int serial = read_confg_value(cd, "serial", false, (unsigned int)1);
    memset(m_id, 0, sizeof(m_id));
    for (unsigned int i = 0; i < 6; i++) m_id[i] = (uint8_t)((serial >> (8 * (i & 3))) & 0xFF);
    // Each word: XOR, then rotate left; the last word cancels what is left
    uint16_t sum = 0xFFFF;
    for (unsigned int i = 0; i < 3; i++) {
        sum ^= (uint16_t)(m_id[2 * i] | (m_id[2 * i + 1] << 8));
        sum = (uint16_t)((sum << 1) | (sum >> 15));
    }
    m_id[6] = (uint8_t)(sum & 0xFF);
    m_id[7] = (uint8_t)(sum >> 8);
    return emulator::Result::ok();
}

void E85Sys::reset(bool cold)
{
    AddressableDevice::reset(cold);
    m_halt_enable = false;
    m_lamps = 0;
}

unsigned int E85Sys::get_value(unsigned int address)
{
    if (address & 1) return 0;
    if (address < 0100) return m_id[(address >> 1) & 037];
    switch (address) {
    case REG_CONFIG: return m_config | (m_halt_enable ? HALT_ENABLE : 0);
    case REG_SLOTS:  return m_slots;
    default:         return 0;
    }
}

void E85Sys::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (address == REG_CONFIG) m_halt_enable = (value & HALT_ENABLE) != 0;
    else if (address == REG_LAMPS) m_lamps = value & 017;
}

std::vector<DeviceFieldInfo> E85Sys::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"lamps", "Error lamps as written, 0 lights", false});
    r.push_back({"halt_enable", "Register 17773700, bit 7", false});
    r.push_back({"id", "First eight bytes of the identification PROM", false});
    return r;
}

bool E85Sys::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "lamps")       { out.values.push_back(m_lamps); return true; }
    if (field == "halt_enable") { out.values.push_back(m_halt_enable ? 1 : 0); return true; }
    if (field == "id") {
        for (unsigned int i = 0; i < 8; i++) out.values.push_back(m_id[i]);
        return true;
    }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void E85Sys::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.b("halt_enable", m_halt_enable);
    w.u("lamps", m_lamps);
}

emulator::Result E85Sys::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.b("halt_enable", m_halt_enable);
    r.u("lamps", m_lamps);
    return emulator::Result::ok();
}

ComputerDevice * create_e85_sys(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Sys(im, cd);
}
