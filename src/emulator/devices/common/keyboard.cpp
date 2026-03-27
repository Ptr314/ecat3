// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract keyboard device

#include "keyboard.h"
#include "emulator/utils.h"

Keyboard::Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    rus_mode(false)
{
    reset_priority = 100;
}

emulator::Result Keyboard::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    use_remap = read_confg_value(cd, "use_remap", false, true);

    return emulator::Result::ok();
}

void Keyboard::key_event(unsigned int key, unsigned int native_key, bool press)
{
    unsigned int k;
    if (known_key(key))
        k = key;
    else if (known_key(native_key))
        k = native_key;
    else return;
    if (press)
        key_down(rus_translate(k));
    else
        key_up(rus_translate(k));
}

bool Keyboard::known_key(unsigned int code)
{
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (KEYS[i].code == code)
            return true;

    return false;
}

unsigned int Keyboard::translate_key(const std::string &key)
{
    std::string key_lower = str_tolower(key);
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (str_tolower(KEYS[i].name) == key_lower)
            return KEYS[i].code;

    return _FFFF;
}

void Keyboard::set_rus(bool new_rus)
{
    rus_mode = new_rus;
}

unsigned int Keyboard::rus_translate(unsigned int code)
{
    if (rus_mode && use_remap) {
        for (auto i : RUS_REMAP)
            if (i[0] == code) return i[1];
        return code;
    }
    return code;
}