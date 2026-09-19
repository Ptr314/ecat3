// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ sound - the speaker line and the tone grid of register 177716

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"

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
// The grid comes from one binary divider, which is why 60 Гц is really 62.5:
// every frequency is 128 кГц divided by a power of two. UKNCBTL takes the
// same taps of the same counter.
//
// The ROM itself uses both ways. A key press is a pulse on bit 7 alone
// (`BIS #200,@#177716`, cleared 40 ms later by `BIC #17600`), and Ctrl-G sets
// bits 7 and 8, a 60 Гц tone for 0.32 s - the ROM listing calls it 500 Гц, but
// bit 8 is the lowest step of the grid.
class UKNCSound: public GenericSound
{
private:
    // Bits 7-12 of the register: bit 0 here is the line, bits 1-5 the grid
    Interface i_input;

    // Ticks of the processor this device is clocked with. The grid counter at
    // 128 кГц is computed from them only when a grid bit is on: clock() runs
    // on every instruction of the processor, and a division there cost more
    // than the rest of the sound together. The floor of the product is exactly
    // what a running count with a carried remainder gives
    uint64_t m_ticks = 0;
    uint64_t grid() const;

    virtual int16_t calc_sound_value() override;
    bool level() const;
    // The level moves on its own only while a grid tap is on together with
    // the line; otherwise only a write to the register changes it
    void update_volatile();

public:
    UKNCSound(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_uknc_sound(InterfaceManager *im, EmulatorConfigDevice *cd);
