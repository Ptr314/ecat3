// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Covox, an 8-bit DAC on the output lines of a parallel port

#include "covox.h"
#include "emulator/utils.h"

#define COVOX_CALLBACK_INPUT 1

Covox::Covox(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , i_input(this, im, 8, "input", MODE_R, COVOX_CALLBACK_INPUT)
    , m_idle_share(true)
    , m_used(false)
    , m_cached_level(_FFFF)
    , m_cached_amplitude(0)
    , m_cached_sample(0)
{
}

emulator::Result Covox::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;
    m_idle_share = read_confg_value(cd, "idle_share", false, true);
    return emulator::Result::ok();
}

void Covox::reset(bool cold)
{
    ComputerDevice::reset(cold);
    m_used = false;
}

void Covox::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    // The reset of the register behind the DAC drives 0, which is no sign of
    // a program; the order of the two resets does not matter then
    if (callback_id == COVOX_CALLBACK_INPUT && (new_value & 0xFF) != 0) m_used = true;
}

void Covox::plug_changed()
{
    // Nothing to do: the byte is held by the port, not by the DAC
}

int32_t Covox::sound_sample(int64_t amplitude)
{
    // An interface nobody drives reads as all ones: the port has not been
    // written yet, and that is the top of the scale, the same as on the wire
    const unsigned int level = i_input.value & 0xFF;
    if (level != m_cached_level || amplitude != m_cached_amplitude) {
        m_cached_level = level;
        m_cached_amplitude = amplitude;
        m_cached_sample = (int32_t)((int64_t)level * 2 * amplitude / 255 - amplitude);
    }
    return m_cached_sample;
}

bool Covox::sound_active()
{
    return m_plugged && (m_idle_share || m_used);
}

const char * Covox::plug_title() const
{
    return QT_TRANSLATE_NOOP("DeviceOptions", "Covox");
}

std::vector<DeviceFieldInfo> Covox::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"value",   "Byte on the DAC input, 0-255",     false});
    r.push_back({"plugged", "1 if the DAC is plugged in",       false});
    r.push_back({"active",  "1 if the DAC takes a share of the mix", false});
    return r;
}

bool Covox::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "value")   { out.values.push_back(i_input.value & 0xFF); return true; }
    if (field == "plugged") { out.values.push_back(m_plugged ? 1 : 0);    return true; }
    if (field == "active")  { out.values.push_back(sound_active() ? 1 : 0); return true; }

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

ComputerDevice * create_covox(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Covox(im, cd);
}
