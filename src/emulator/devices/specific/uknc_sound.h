// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ sound - the speaker line and the tone grid of register 177716

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"
#include "libs/audio_filters.h"

class UKNCTimer;

// The sound of the УК-НЦ lives in the system register 177716 of the peripheral
// processor, and it is more than one bit. The technical description (table 32)
// gives:
//
//   разряд 7     линия звукового сигнала. Ноль - звук выключен, что бы ни
//                стояло в разрядах 8-12. При нулях в 8-12 звук - это сама
//                смена уровня разряда программой
//   разряды 8-12 пропускают сетку частот: 8 - 60 Гц, 9 - 250 Гц, 10 - 500 Гц,
//                11 - 1 кГц, 12 - 8 кГц. Несколько включённых частот
//                собираются «по И», и разряд 7 может эту сборку модулировать
//
// The grid is not a divider of its own: the five taps are разряды 10, 8, 7, 6
// and 3 of the counter of the programmable timer (177710-177714), and the
// device therefore has to be told which timer it listens to ("timer" in the
// configuration). The frequencies of table 32 are what those taps give while
// the counter runs through all twelve of its bits, and a program that loads
// the buffer register 177712 with something else moves the whole grid with it
// - which is how the machine plays music. «Замок гоблинов» keeps all five
// bits of the grid on all the time and writes the pitch of the note into
// 177712; a rest is an уставка of 8, a tone far above hearing. Take the grid
// from a divider of its own instead, and the same game turns into a 60 Гц
// rattle that never stops. UKNCBTL takes the same taps of the same counter.
//
// The ROM itself uses the register both ways. A key press is a pulse on bit 7
// alone (`BIS #200,@#177716`, cleared 40 ms later by `BIC #17600`), and Ctrl-G
// sets bits 7 and 8 for 0.32 s - the slowest tap, at whatever the timer happens
// to be counting. The ROM leaves it running through all twelve bits with the
// 2 us step, so the bell comes out four steps of the grid above the 60 Гц of
// table 32: 500 кГц / 2^11, 244 Гц.
class UKNCSound: public GenericSound
{
private:
    // Bits 7-12 of the register: bit 0 here is the line, bits 1-5 the grid
    Interface i_input;

    // The counter the taps are picked from. It is clocked by the timer itself,
    // in the same domain; here it is only read
    UKNCTimer * m_timer;
    unsigned int grid() const;

    // Сколько шагов счётчика таймер насчитал к тому моменту, когда уровень
    // считали в прошлый раз: отрезок между этим и текущим значением и есть то,
    // по чему усредняется уровень
    uint64_t m_seen_steps;
    // Фаза счётчика на конце прошлого отрезка: сколько тактов он к тому
    // моменту уже отстоял в текущем своём шаге
    double m_prev_phase;

    // Фильтр на шагах счётчика - единственное равномерное место в тракте, см.
    // calc_sound_value(). Частота, на которую он настроен, следует за периодом
    // счёта, а m_grid_last - последнее его значение: им заполняется кусок
    // отрезка до первого шага
    ButterworthLowPassFilter m_grid_filter;
    double m_grid_rate;
    double m_grid_last;

    virtual int16_t calc_sound_value() override;
    bool level() const;
    bool level_at(unsigned int counter) const;
    // The level moves on its own only while a grid tap is on together with
    // the line; otherwise only a write to the register changes it
    void update_volatile();

public:
    UKNCSound(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void state_restored() override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_uknc_sound(InterfaceManager *im, EmulatorConfigDevice *cd);
