// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Клавиатура ZX Spectrum поверх клавиатуры машины

#pragma once

#include "emulator/core.h"

class Keyboard;

// ПЗУ Спектрума читает клавиатуру портом $FE: на старших разрядах адреса стоит
// маска полурядов (активный ноль), в ответе разряды 0-4 - пять клавиш полуряда,
// тоже активным нулем. Читать можно сразу несколько полурядов, и тогда ответ -
// их поразрядное И; игры этим пользуются постоянно.
//
// Своих клавиш у этого устройства нет: оно спрашивает, что сейчас нажато, у
// клавиатуры самой машины (ids_held()) и раскладывает ее клавиши по матрице
// Спектрума. На Арго это и происходит: клавиши у машины свои, а ПЗУ на кассете
// - стоковое ($028E: LD L,$2F; LD DE,$FFFF; LD BC,$FEFE; IN A,(C)), значит в
// режиме ZX что-то подает ему матрицу Спектрума. Что именно - со схемы не
// выяснить, поэтому раскладка взята очевидная: клавиша машины отвечает за ту
// клавишу Спектрума, которая на ней написана.
class ZXKeyboard: public AddressableDevice
{
private:
    Interface i_port;               // Полный адрес обращения к порту
    // Вход магнитофона - разряд 6 того же чтения. У Спектрума лента и
    // клавиатура живут в одном порту, и загрузчик ПЗУ пользуется обоими
    // сразу: полуряд $7F он читает ради разряда 6, а по разряду 0 того же
    // ответа бросает загрузку, если нажат пробел
    Interface i_ear;
    Keyboard * Source = nullptr;    // У кого спрашивать нажатия

    // Восемь полурядов по пять клавиш, именами клавиш машины
    std::string m_matrix[8][5];
    unsigned int m_last = 0xFF;     // Последний ответ, для LOG

    void set_default_matrix();
    bool ear_level() const;

public:
    ZXKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_zx_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
