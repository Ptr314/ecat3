// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Wave generator, source

#include "generator.h"
#include "emulator/utils.h"

Generator::Generator(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , pulse_stored(0)
    , in_pulse(false)
    , enabled(true)
    , i_out(this, im, 1, "out", MODE_W)
    , i_enable(this, im, 1, "enable", MODE_R, 1)
    , i_trigger(this, im, 1, "trigger", MODE_R, 2)
    , triggered(false)
    , m_pulses(0)
{
    m_clocked = true;   //clock() is overridden here
}

emulator::Result Generator::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    //The frequency of this generator's own clock domain, set by the base class
    //from clock_source - not the master processor's, which is all this used to
    //be able to see
    triggered = (i_trigger.linked > 0);
    if (triggered)
    {
        //У одновибратора своей частоты нет, её задаёт вход запуска
        total_counts = 0;
    } else {
        unsigned int freq = parse_numeric_value(cd->get_parameter("frequency").value);
        total_counts = m_system_clock / freq;
    }

    std::string lens = cd->get_parameter("length", false).value;
    if (lens.empty())
        pulse_counts = 1;
    else
        pulse_counts = parse_numeric_value(lens);

    std::string pol = str_tolower(cd->get_parameter("polarity", false).value);
    if (pol.empty() || pol == "positive" || pol == "pos" || pol == "p" || pol == "1")
        positive = true;
    else
        if (pol == "negative" || pol == "neg" || pol == "n" || pol == "0")
            positive = false;
        else
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect polarity for")) + "} " + this->name);

    i_enable.change(1);

    return emulator::Result::ok();
}

void Generator::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == 1)
    {
        enabled = (new_value & 1) > 0;
        return;
    }

    //Фронт входа запуска: выдать импульс, если разрешено
    if (((new_value & 1) != 0) && ((old_value & 1) == 0) && enabled)
    {
        in_pulse = true;
        pulse_stored = 0;
        m_pulses++;
        i_out.change(positive?1:0);
    }
}

void Generator::system_clock(unsigned int counter)
{
    clock_stored += counter;

    if (in_pulse) {
        pulse_stored += counter;
        if (pulse_stored >= pulse_counts)
        {
            in_pulse = false;
            if (positive)
                i_out.change(0);
            else
                i_out.change(1);
        }
    }

    if (triggered) return;

    if (clock_stored >= total_counts)
    {
        clock_stored -= total_counts;
        if (enabled)
        {
            in_pulse = true;
            pulse_stored = 0;
            m_pulses++;
            if (positive)
                i_out.change(1);
            else
                i_out.change(0);
        }
    }
}

void Generator::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.n("pulse_stored", pulse_stored);
    w.b("in_pulse", in_pulse);
    w.b("enabled", enabled);
}

emulator::Result Generator::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    r.u("pulse_stored", pulse_stored);
    r.b("in_pulse", in_pulse);
    r.b("enabled", enabled);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> Generator::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"enabled", "1 if the generator is running",    false});
    r.push_back({"out",     "Current level of the output",      false});
    r.push_back({"period",  "Period, in system clock counts",   false});
    r.push_back({"pulses",  "Pulses emitted since reset",       false});
    return r;
}

bool Generator::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "enabled") { out.values.push_back(enabled?1:0);    return true; }
    if (field == "out")     { out.values.push_back(i_out.value);    return true; }
    out.width = 32;
    if (field == "period")  { out.values.push_back(total_counts);   return true; }
    if (field == "pulses")  { out.values.push_back((unsigned int)m_pulses); return true; }
    out.width = 0;

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

ComputerDevice * create_generator(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Generator(im, cd);
}

