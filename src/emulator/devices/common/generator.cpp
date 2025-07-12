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

void Generator::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);

    unsigned int main_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;

    QString freqs = cd->get_parameter("frequency").value;
    unsigned int freq = parse_numeric_value(freqs);

    total_counts = main_clock / freq;

    QString lens = cd->get_parameter("length").value;
    if (lens.isEmpty())
        pulse_counts = 1;
    else
        pulse_counts = parse_numeric_value(lens);

    QStringList positives;
    positives << "positive" << "pos" << "p" << "1";
    QStringList negatives;
    negatives << "negative" << "neg" << "n" << "0";

    QString pol = cd->get_parameter("polarity", false).value.toLower();
    if (pol.isEmpty() || positives.contains(pol))
        positive = true;
    else
        if (negatives.contains(pol))
            positive = false;
        else
            QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect polarity for %1").arg(this->name));

    i_enable.change(1);
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

