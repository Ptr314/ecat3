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
{
}

emulator::Result Generator::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    unsigned int main_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;

    unsigned int freq = parse_numeric_value(cd->get_parameter("frequency").value);
    total_counts = main_clock / freq;

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
    enabled = (new_value & 1) > 0;
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

    if (clock_stored >= total_counts)
    {
        clock_stored -= total_counts;
        if (enabled)
        {
            in_pulse = true;
            pulse_stored = 0;
            if (positive)
                i_out.change(1);
            else
                i_out.change(0);
        }
    }
}

ComputerDevice * create_generator(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Generator(im, cd);
}

