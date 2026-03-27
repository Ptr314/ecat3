// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: One bit sound device class

#include "speaker.h"
#include "emulator/utils.h"

#define SPK_MODE_LEVEL  0
#define SPK_MODE_FLIP   1

Speaker::Speaker(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericSound(im, cd)
    , mode(SPK_MODE_LEVEL)
    , input(0)
    , i_input(this, im, 1, "input", MODE_R, 1)
    , i_mixer(this, im, 8, "mixer", MODE_R)
{
}

int16_t Speaker::calc_sound_value()
{
    if (InputWidth + MixerWidth == 0) return m_amplitude;

    int32_t V = 0;
    if (InputWidth != 0) {
        V += input;
    }
    for (unsigned int i=0; i < MixerWidth; i++)
        V += (i_mixer.value >> i) & 0x01;

    return (V * m_amplitude * 2 / (InputWidth + MixerWidth)) - m_amplitude;
}

void Speaker::reset(bool cold)
{
    GenericSound::reset(cold);
    InputWidth = (i_input.linked == 0) ? 0 : 1;
    MixerWidth = (i_mixer.linked == 0) ? 0 : CalcBits(i_mixer.linked_bits, 8);
}

emulator::Result Speaker::load_config(SystemData *sd)
{
    emulator::Result res = GenericSound::load_config(sd);
    if (!res) return res;

    std::string s = str_tolower(cd->get_parameter("mode", false).value);
    if (s.empty() || s == "level")
        mode = SPK_MODE_LEVEL;
    else
    if (s == "flip")
        mode = SPK_MODE_FLIP;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{Speaker|" + std::string(QT_TRANSLATE_NOOP("Speaker", "Unknown speaker type")) + "} " + s);

    return emulator::Result::ok();
}

void Speaker::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    unsigned int new_input = input;

    if (mode == SPK_MODE_FLIP) {
        if (i_input.neg_edge())
            new_input = input ^ 1;
    } else
        new_input = i_input.value  & 0x01;

    input = new_input;
}

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Speaker(im, cd);
}
