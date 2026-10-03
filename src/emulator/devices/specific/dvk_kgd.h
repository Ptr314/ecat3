// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК monochrome graphics controller КГД

#pragma once

#include <vector>

#include "emulator/core.h"

#define KGD_WIDTH       400
#define KGD_HEIGHT      286
#define KGD_LINE_BYTES  50
#define KGD_VRAM_SIZE   16384

// Контроллер графического дисплея КГД: 16 КБ видеопамяти, доступной
// процессору только через регистры, и картинка 400 x 286, каждая точка
// удвоена по горизонтали. Своего монитора у него нет - картинка подмешивается
// к изображению терминала (КСМ или 15ИЭ), это делает ksm-display.
//
//   176640  управление: разряд 15 - показывать графику, 14 - погасить текст
//           терминала
//   176642  данные: байт видеопамяти по адресу из 176644
//   176644  адрес, 14 разрядов
//   176646  счётчик развёртки: строка (разряды 11-15) и колонка (0-10)
//
// Строка картинки - 50 байт подряд, разряд 0 байта - левая точка (MAME,
// dvk_kgd.cpp; реплика forth32 так же).
class DVKKGD: public AddressableDevice
{
private:
    unsigned int m_cr = 0;
    unsigned int m_ar = 0;
    std::vector<uint8_t> m_vram;
    uint64_t m_ticks = 0;           // для счётчика развёртки
    unsigned int m_line_ticks = 1;  // тактов на строку растра, 64 мкс

public:
    DVKKGD(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    // Для ksm-display: читается из потока отрисовки, как видеопамять КСМ
    bool graphics_on() const { return (m_cr & 0100000) != 0; }
    bool text_off() const { return (m_cr & 0040000) != 0; }
    bool pixel(unsigned x, unsigned y) const
    {
        const unsigned bit = y * KGD_WIDTH + x;
        return ((m_vram[bit >> 3] >> (bit & 7)) & 1) != 0;
    }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_dvk_kgd(InterfaceManager *im, EmulatorConfigDevice *cd);
