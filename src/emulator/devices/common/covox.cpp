// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Covox, an 8-bit DAC on the output lines of a parallel port

#include "covox.h"
#include "emulator/utils.h"

Covox::Covox(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , i_input(this, im, 8, "input", MODE_R, 1)
    , m_cached_level(_FFFF)
    , m_cached_amplitude(0)
    , m_cached_sample(0)
{
}

// The level is the byte on the input and nothing else
void Covox::interface_callback(MAYBE_UNUSED unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if ((new_value & 0xFF) != (old_value & 0xFF)) sound_changed();
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
    return m_plugged;
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
    return r;
}

bool Covox::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "value")   { out.values.push_back(i_input.value & 0xFF); return true; }
    if (field == "plugged") { out.values.push_back(m_plugged ? 1 : 0);    return true; }

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

ComputerDevice * create_covox(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Covox(im, cd);
}
