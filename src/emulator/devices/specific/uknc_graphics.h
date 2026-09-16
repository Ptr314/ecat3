// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ graphics unit - indirect plane access and octet drawing

#pragma once

#include "emulator/core.h"

// Registers 177010-177026 on the peripheral processor's bus: how the УК-НЦ
// reaches its three memory planes and how it draws.
//
//   177010  адрес планов        запись сюда сразу читает три плана в 177012/177014
//   177012  данные плана 0
//   177014  данные планов 1 и 2 младший байт в план 1, старший в план 2
//   177016  код цвета точки     три разряда, по одному на план
//   177020  цвет фона, точки 1-4
//   177022  цвет фона, точки 5-8
//   177024  октет точки         чтение загружает фон из памяти, запись рисует
//   177026  маска               разряд запрещает запись в свой план
//
// The ROM draws everything through this: it puts the foreground colour in
// 177016, the background of eight dots in 177020/177022, and writes a slice of
// a character to 177024. Every bit set in that octet takes the foreground
// colour, every clear bit the background, and the three plane bytes go to
// memory at once - eight dots of any of eight colours in one bus cycle.
class UKNCGraphics: public AddressableDevice
{
private:
    RAM * m_plane[3]{};

    unsigned int m_address = 0;     // 177010
    unsigned int m_data0 = 0;       // 177012
    unsigned int m_data12 = 0;      // 177014
    unsigned int m_color = 0;       // 177016, three bits
    unsigned int m_bg_low = 0;      // 177020, dots 0-3
    unsigned int m_bg_high = 0;     // 177022, dots 4-7
    unsigned int m_octet = 0;       // 177024
    unsigned int m_mask = 0;        // 177026, three bits

    unsigned int m_draws = 0;       // octets drawn, for scripts

    void latch_planes();                        // a write to 177010 samples memory
    void load_background();                     // reading 177024 takes the dots under the cursor
    void draw_octet(unsigned int octet);        // writing 177024 puts eight dots down

    void unpack_background(uint8_t out[3]) const;
    void pack_background(const uint8_t in[3]);

public:
    UKNCGraphics(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_uknc_graphics(InterfaceManager *im, EmulatorConfigDevice *cd);
