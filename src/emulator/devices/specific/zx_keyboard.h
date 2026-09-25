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
    // Команда лентопротяжке. В режиме ZX её ведут стрелки, а не процессор:
    // на видео живой машины автор набирает LOAD "" и пускает ленту стрелкой
    // вверх, причём это работает и в запущенной игре. Программой это быть не
    // может - игра занимает машину целиком, а в образе ПЗУ Спектрума нет ни
    // одного перехвата (проверены $0038, $0066, $028E, $0556, $05E7, NMIADD
    // пуст). Значит клавиши разбирает то же железо, что собирает матрицу
    Interface i_control;
    Keyboard * Source = nullptr;    // У кого спрашивать нажатия

    // Восемь полурядов по пять клавиш, именами клавиш машины
    std::string m_matrix[8][5];
    unsigned int m_last = 0xFF;     // Последний ответ, для LOG

    void set_default_matrix();
    bool ear_level() const;
    void sample_keys();

    // Сканирование включено. После входа в режим ZX клавиатуры нет, пока не
    // нажмут Ф10 - так на живой машине, автор ролика так и говорит:
    // «включается сканирование клавиатуры»
    bool m_scan = false;
    bool m_f10_down = false;
    // Матрица, какой ее увидит машина: собирается раз в миллисекунду вместе с
    // опросом клавиш, а не при чтении порта. ПЗУ Спектрума читает $FE в цикле
    // (LD-EDGE-1, $05E7 - раз в полсотни тактов на всю загрузку с ленты), и
    // брать там замок с копией вектора значило бы платить за это десятки тысяч
    // раз в секунду
    uint8_t m_rows[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    unsigned int m_control = 0;     // Последняя команда лентопротяжке, она держится
    unsigned int m_ticks = 0;       // Опрос клавиш раз в миллисекунду

public:
    ZXKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;

    void clock(unsigned int counter) override;
    void reset(bool cold) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_zx_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
