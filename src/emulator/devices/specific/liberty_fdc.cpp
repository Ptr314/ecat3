// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Liberty Drive, a КР1818ВГ93 floppy card for Apple //e / Правец-8А

#include "liberty_fdc.h"
#include "emulator/devices/common/wd1793.h"
#include "emulator/utils.h"

LibertyFDC::LibertyFDC(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_latch(this, im, 8, "latch", MODE_W)
    , i_drive(this, im, 2, "drive", MODE_W)
    , i_side(this, im, 1, "side", MODE_W)
{
}

emulator::Result LibertyFDC::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_fdc = dynamic_cast<WD1793*>(im->dm->get_device_by_name(cd->get_parameter("fdc").value));
    if (m_fdc == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{LibertyFDC|" + std::string(QT_TRANSLATE_NOOP("LibertyFDC", "A wd1793 device is expected")) + "} " + name);
    LibertyFDC::reset(true);
    return emulator::Result::ok();
}

void LibertyFDC::reset(bool cold)
{
    (void)cold;
    m_latch = 0;
    drive_lines();
}

void LibertyFDC::drive_lines()
{
    i_latch.change(m_latch);
    i_drive.change(m_latch & 1);
    i_side.change((m_latch >> 1) & 1);
}

unsigned LibertyFDC::get_value(unsigned address)
{
    const unsigned a = address & 0x0F;
    if (a < 4) {
        unsigned v = m_fdc->get_value(a) & 0xFF;
        if (a == 0) v = (v & 0x7F) | ((m_latch & 0x40) ? 0 : 0x80);
        return v;
    }
    if (a == 4) return m_fdc->drq_active() ? 0x80 : 0;
    return 0xFF;
}

unsigned LibertyFDC::get_direct(unsigned address)
{
    const unsigned a = address & 0x0F;
    if (a < 4) {
        unsigned v = m_fdc->get_direct(a) & 0xFF;
        if (a == 0) v = (v & 0x7F) | ((m_latch & 0x40) ? 0 : 0x80);
        return v;
    }
    if (a == 4) return m_fdc->drq_active() ? 0x80 : 0;
    return 0xFF;
}

void LibertyFDC::set_value(unsigned address, unsigned value, bool force)
{
    const unsigned a = address & 0x0F;
    if (a < 4) {
        m_fdc->set_value(a, value & 0xFF, force);
    } else if (a == 8) {
        m_latch = value & 0xFF;
        drive_lines();
    }
}

std::vector<DeviceFieldInfo> LibertyFDC::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"latch", "The control latch at $C0n8", false});
    return r;
}

bool LibertyFDC::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "latch") {
        out.numeric = true;
        out.values.push_back(m_latch);
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

void LibertyFDC::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("latch", m_latch, 8);
}

emulator::Result LibertyFDC::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("latch", m_latch);
    return emulator::Result::ok();
}

ComputerDevice * create_liberty_fdc(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new LibertyFDC(im, cd);
}
