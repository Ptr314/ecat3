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

unsigned int translate_key_name(const std::string &key)
{
    std::string key_lower = str_tolower(key);
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (str_tolower(KEYS[i].name) == key_lower)
            return KEYS[i].code;

    return _FFFF;
}

std::string key_name(unsigned int code)
{
    const unsigned int count = sizeof(KEYS)/sizeof(KeyDescription);
    for (unsigned int i=0; i<count; i++)
        if (KEYS[i].code == code && KEYS[i].name.length() == 1)
            return KEYS[i].name;
    for (unsigned int i=0; i<count; i++)
        if (KEYS[i].code == code)
            return KEYS[i].name;
    return "";
}

unsigned int Keyboard::translate_key(const std::string &key)
{
    return translate_key_name(key);
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

std::vector<DeviceFieldInfo> Keyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"rus", "1 when the keyboard is in the Rus register", false});
    return r;
}

bool Keyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //Typing that comes out in the wrong alphabet is almost always this
    if (field == "rus")
    {
        out.numeric = true;
        out.values.push_back(rus_mode ? 1 : 0);
        return true;
    }

    return ComputerDevice::get_field(field, from, to, out);
}