// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ sound - the speaker line and the tone grid of register 177716

#include "uknc_sound.h"

#define GRID_RATE   128000      // частота делителя, из которой берётся вся сетка

#define IN_LINE     0x01        // разряд 7 регистра
#define IN_GRID     0x3E        // разряды 8-12

// Отводы делителя для разрядов 8-12: 62.5 Гц, 250 Гц, 500 Гц, 1 кГц, 8 кГц.
// Разряд сдвига N даёт частоту GRID_RATE / 2^(N+1)
static const unsigned int GRID_TAPS[5] = {10, 8, 7, 6, 3};

UKNCSound::UKNCSound(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericSound(im, cd)
    , i_input(this, im, 6, "input", MODE_R)
{
}

void UKNCSound::reset(bool cold)
{
    GenericSound::reset(cold);
    m_ticks = 0;
}

void UKNCSound::clock(unsigned int counter)
{
    // Такты считаем всегда, даже без звукового устройства: поле level должно
    // отвечать одинаково в любом прогоне
    m_ticks += counter;
    GenericSound::clock(counter);
}

uint64_t UKNCSound::grid() const
{
    // 2^64 / 128000 / 6,25 МГц - больше полугода работы машины без сброса
    return (m_system_clock != 0)? m_ticks * GRID_RATE / m_system_clock : 0;
}

bool UKNCSound::level() const
{
    const unsigned int in = i_input.value;

    // Разряд 7 в нуле глушит всё
    if ((in & IN_LINE) == 0) return false;

    const unsigned int grid = (in & IN_GRID) >> 1;
    if (grid == 0) return true;
    const uint64_t counter = this->grid();

    // Включённые частоты собираются «по И»: звучит, пока ни одна из них не
    // подняла свой уровень. Это NOR, а не AND, но перевёрнутая волна звучит
    // так же, и UKNCBTL собирает именно так
    for (unsigned int i = 0; i < 5; i++)
        if ((grid & (1u << i)) && ((counter >> GRID_TAPS[i]) & 1))
            return false;
    return true;
}

int16_t UKNCSound::calc_sound_value()
{
    return (int16_t)(level() ? m_amplitude : -m_amplitude);
}

std::vector<DeviceFieldInfo> UKNCSound::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericSound::get_device_fields();
    r.push_back({"control", "Разряды 7-12 регистра 177716, сдвинутые к нулю", false});
    r.push_back({"level",   "Уровень на выходе прямо сейчас, 0 или 1",       false});
    r.push_back({"grid",    "Счётчик делителя сетки частот, 128 кГц",        false});
    return r;
}

bool UKNCSound::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "control") { out.values.push_back(i_input.value & 077); return true; }
    if (field == "level")   { out.values.push_back(level()? 1 : 0);     return true; }
    if (field == "grid")    { out.width = 32; out.values.push_back((unsigned int)grid()); return true; }
    out.numeric = false;
    return GenericSound::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_sound(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCSound(im, cd);
}
