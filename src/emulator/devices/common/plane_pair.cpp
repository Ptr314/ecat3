// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A word-wide address space built from two byte-wide memory planes

#include <algorithm>

#include "plane_pair.h"
#include "emulator/utils.h"

PlanePair::PlanePair(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    can_read = true;
    can_write = true;
}

emulator::Result PlanePair::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    m_low  = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("low").value, false));
    m_high = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("high").value, false));

    if (m_low == nullptr || m_high == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PlanePair|" + std::string(QT_TRANSLATE_NOOP("PlanePair", "Both planes must be memory devices")) + "} " + name);

    m_base = read_confg_value(cd, "base", false, (unsigned int)0);
    m_index = read_confg_value(cd, "index", false, (unsigned int)0) != 0;

    // Two bytes of address space per byte of a plane, limited by the shallower
    // of the two and by whatever the base offset leaves
    const unsigned int low_left  = (m_low->get_size()  > m_base)? (m_low->get_size()  - m_base) : 0;
    const unsigned int high_left = (m_high->get_size() > m_base)? (m_high->get_size() - m_base) : 0;
    const unsigned int depth = (low_left < high_left)? low_left : high_left;

    const unsigned int width = m_index? depth : depth * 2;
    addresable_size = read_confg_value(cd, "size", false, width);
    if (addresable_size > width) addresable_size = width;

    if (addresable_size == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PlanePair|" + std::string(QT_TRANSLATE_NOOP("PlanePair", "Planes are too small for the base offset")) + "} " + name);

    return emulator::Result::ok();
}

void PlanePair::reset(MAYBE_UNUSED bool cold)
{
    m_low_buf = m_high_buf = nullptr;
    m_depth = 0;
    if (dynamic_cast<RAM*>(m_low) == nullptr || dynamic_cast<RAM*>(m_high) == nullptr) return;
    if (!m_low->is_plain() || !m_high->is_plain()) return;
    m_low_buf = m_low->get_buffer();
    m_high_buf = m_high->get_buffer();
    if (m_low_buf == nullptr || m_high_buf == nullptr) { m_low_buf = m_high_buf = nullptr; return; }
    m_depth = std::min(m_low->get_size(), m_high->get_size());
}

unsigned int PlanePair::get_value(unsigned int address)
{
    if (m_low_buf != nullptr) {
        const unsigned int index = index_of(address);
        if (index < m_depth)
            return m_index? m_low_buf[index] : ((address & 1)? m_high_buf : m_low_buf)[index];
    }

    // Bit 0 of the address picks the plane, the rest is the index in it.
    // Addressed by index, a byte access is the low byte of the word
    if (m_index) return m_low->get_value(index_of(address)) & 0xFF;
    Memory * plane = (address & 1)? m_high : m_low;
    return plane->get_value(index_of(address));
}

void PlanePair::set_value(unsigned int address, unsigned int value, bool force)
{
    if (m_low_buf != nullptr) {
        const unsigned int index = index_of(address);
        if (index < m_depth) {
            (m_index? m_low_buf : ((address & 1)? m_high_buf : m_low_buf))[index] = (uint8_t)value;
            return;
        }
    }
    if (m_index) { m_low->set_value(index_of(address), value & 0xFF, force); return; }
    Memory * plane = (address & 1)? m_high : m_low;
    plane->set_value(index_of(address), value & 0xFF, force);
}

unsigned int PlanePair::get_direct(unsigned int address)
{
    if (m_index) return m_low->get_direct(index_of(address)) & 0xFF;
    Memory * plane = (address & 1)? m_high : m_low;
    return plane->get_direct(index_of(address));
}

unsigned int PlanePair::get_value_word(unsigned int address)
{
    // A word is one byte of each plane at the same index, which is why the
    // default two-byte composition of AddressableDevice would be wrong here:
    // it would read two consecutive indexes of alternating planes
    const unsigned int index = index_of(m_index? address : (address & ~1u));
    if (index < m_depth) return m_low_buf[index] | (m_high_buf[index] << 8);
    return (m_low->get_value(index) & 0xFF) | ((m_high->get_value(index) & 0xFF) << 8);
}

void PlanePair::set_value_word(unsigned int address, unsigned int value, bool force)
{
    const unsigned int index = index_of(m_index? address : (address & ~1u));
    if (index < m_depth) {
        m_low_buf[index] = (uint8_t)value;
        m_high_buf[index] = (uint8_t)(value >> 8);
        return;
    }
    m_low->set_value(index, value & 0xFF, force);
    m_high->set_value(index, (value >> 8) & 0xFF, force);
}

std::vector<DeviceFieldInfo> PlanePair::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"planes", "Names of the low and high memory planes", false});
    r.push_back({"base",   "Offset in the planes that address 0 maps to", false});
    r.push_back({"index",  "1 when the address is a plane index rather than a processor address", false});
    return r;
}

bool PlanePair::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "planes")
    {
        out.numeric = false;
        out.text = m_low->name + " / " + m_high->name;
        return true;
    }

    if (field == "index")
    {
        out.numeric = true;
        out.width = 8;
        out.values.push_back(m_index? 1 : 0);
        return true;
    }

    if (field == "base")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_base);
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_plane_pair(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PlanePair(im, cd);
}
