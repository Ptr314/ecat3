// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Indirect memory access through an address register and data registers

#include "indirect_memory.h"
#include "emulator/utils.h"

IndirectMemory::IndirectMemory(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 16, "address", MODE_W)
{
    can_read = true;
    can_write = true;

    for (unsigned int i = 0; i < INDIRECT_MAX_TARGETS; i++) {
        m_targets[i].device = nullptr;
        m_targets[i].value = 0;
        m_targets[i].scale = 1;
        m_targets[i].byte_wide = false;
    }
}

emulator::Result IndirectMemory::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    // @data[n] = device, in register order: the address register is at offset 0
    // and data register n at offset (n+1)*2
    for (size_t i = 0; i < cd->parameters.size(); i++)
    {
        if (cd->parameters[i].name != "@data") continue;

        const std::string &range = cd->parameters[i].left_range;
        if (range.empty())
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{IndirectMemory|" + std::string(QT_TRANSLATE_NOOP("IndirectMemory", "Incorrect range for")) + "} " + name);

        const unsigned int id = parse_numeric_value(range.substr(1, range.length() - 2));
        if (id >= INDIRECT_MAX_TARGETS)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{IndirectMemory|" + std::string(QT_TRANSLATE_NOOP("IndirectMemory", "Too many data registers")) + "} " + name);

        m_targets[id].device = dynamic_cast<AddressableDevice*>(
            im->dm->get_device_by_name(cd->parameters[i].value, false));

        if (m_targets[id].device == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{IndirectMemory|" + std::string(QT_TRANSLATE_NOOP("IndirectMemory", "Device is not addressable")) + "} " + cd->parameters[i].value);

        const std::string scale = cd->extended_parameter((unsigned int)i, "scale");
        m_targets[id].scale = scale.empty() ? 1 : parse_numeric_value(scale, 10);
        m_targets[id].byte_wide = cd->extended_parameter((unsigned int)i, "width") == "8";
        if (m_targets[id].scale == 0)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{IndirectMemory|" + std::string(QT_TRANSLATE_NOOP("IndirectMemory", "Incorrect range for")) + "} " + name);

        if (id + 1 > m_count) m_count = id + 1;
    }

    if (m_count == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{IndirectMemory|" + std::string(QT_TRANSLATE_NOOP("IndirectMemory", "No data registers defined")) + "} " + name);

    // The address register is as wide as the deepest target can use
    m_address_mask = read_confg_value(cd, "address_mask", false, (unsigned int)0xFFFF);
    m_auto_increment = read_confg_value(cd, "auto_increment", false, false);

    // The address register plus one register per target, two bytes each
    addresable_size = (m_count + 1) * 2;

    return emulator::Result::ok();
}

void IndirectMemory::reset(MAYBE_UNUSED bool cold)
{
    m_address = 0;
    i_address.change(0);
    for (unsigned int i = 0; i < m_count; i++) m_targets[i].value = 0;
}

void IndirectMemory::set_address(unsigned int value)
{
    m_address = value & m_address_mask;
    i_address.change(m_address);
    latch();
}

void IndirectMemory::latch()
{
    for (unsigned int i = 0; i < m_count; i++) {
        Target &t = m_targets[i];
        t.value = t.byte_wide ? (t.device->get_value(target_address(t)) & 0xFF)
                              : (t.device->get_value_word(target_address(t)) & 0xFFFF);
    }
}

void IndirectMemory::step()
{
    if (m_auto_increment) set_address(m_address + 2);
}

unsigned int IndirectMemory::get_value_word(unsigned int address)
{
    const unsigned int reg = address >> 1;   // 0 is the address register

    if (reg == 0) return m_address & 0xFFFF;

    const unsigned int id = reg - 1;
    if (id >= m_count) return _FFFF;

    // The latch, not the memory: software re-reading this register without
    // touching the address gets what the last address write captured
    return m_targets[id].value;
}

void IndirectMemory::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    const unsigned int reg = address >> 1;   // 0 is the address register

    if (reg == 0) {
        set_address(value);
        return;
    }

    const unsigned int id = reg - 1;
    if (id >= m_count) return;

    Target &t = m_targets[id];
    if (t.byte_wide) {
        t.value = value & 0xFF;
        t.device->set_value(target_address(t), t.value);
    } else {
        t.value = value & 0xFFFF;
        t.device->set_value_word(target_address(t), t.value);
    }
    step();
}

unsigned int IndirectMemory::get_value(unsigned int address)
{
    // A byte access picks a lane of the 16-bit register it lands in
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void IndirectMemory::set_value(unsigned int address, unsigned int value, bool force)
{
    const unsigned int b = value & 0xFF;
    const unsigned int reg = address >> 1;
    const unsigned int id = reg - 1;

    // A word-wide target takes the byte into its own lane only
    if (reg != 0 && id < m_count && !m_targets[id].byte_wide) {
        Target &t = m_targets[id];
        if (address & 1) t.value = (t.value & 0x00FF) | (b << 8);
        else             t.value = (t.value & 0xFF00) | b;
        t.device->set_value(target_address(t) + (address & 1), b);
        step();
        return;
    }

    // Anything else sees a word with the byte on its lane and zero on the other
    set_value_word(address & ~1u, (address & 1) ? (b << 8) : b, force);
}

void IndirectMemory::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    //The latched address the data registers read and write through
    w.u("address", m_address);
    //And what they latched: a data register reads the latch, not the memory,
    //and the memory may have changed since the address was written
    uint16_t latched[INDIRECT_MAX_TARGETS];
    for (unsigned int i = 0; i < m_count; i++) latched[i] = (uint16_t)m_targets[i].value;
    w.array("latched", latched, m_count);
}

emulator::Result IndirectMemory::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("address", m_address);
    uint16_t latched[INDIRECT_MAX_TARGETS] = {0, 0, 0, 0};
    m_relatch = !r.array("latched", latched, m_count);
    if (!m_relatch)
        for (unsigned int i = 0; i < m_count; i++) m_targets[i].value = latched[i];
    return emulator::Result::ok();
}

void IndirectMemory::state_restored()
{
    //A state written before the latches were saved: the targets are restored
    //by now, and the address register has not moved since it was written, so
    //sampling them again gives what they held unless the program wrote the
    //memory behind the latch after that
    if (m_relatch) latch();
    m_relatch = false;
}

std::vector<DeviceFieldInfo> IndirectMemory::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"address", "Contents of the address register",              false});
    r.push_back({"targets", "Data registers and the devices they reach",     false});
    return r;
}

bool IndirectMemory::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "address")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_address);
        return true;
    }

    if (field == "targets")
    {
        out.numeric = false;
        for (unsigned int i = 0; i < m_count; i++)
        {
            if (!out.text.empty()) out.text += "\n";
            out.text += "        " + std::to_string(i) + " -> "
                      + ((m_targets[i].device != nullptr)? m_targets[i].device->name : std::string("-"));
            if (m_targets[i].scale != 1) out.text += " scale " + std::to_string(m_targets[i].scale);
            if (m_targets[i].byte_wide) out.text += " width 8";
        }
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_indirect_memory(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IndirectMemory(im, cd);
}
