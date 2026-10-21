// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A printer on the lines of a parallel port

#pragma once

#include "connector.h"
#include "byte_tap.h"

// Принтер на линиях параллельного порта: байт на ~data, строб на ~strobe,
// ответ - ~busy, ~ready, ~online. Сам он ничего не печатает: принятые байты
// идут в поля output/text и в окно «Порты ввода-вывода», откуда их можно
// записать в файл.
//
// Байт берётся по подъёму ~strobe (строб с активным нулём подаётся через !).
// С этого момента принтер занят (busy = 1, ready = 0) на время char_time, потом
// снова готов. Так с ним работают все три порта машин:
//   - ДВК-4, КР1801ВП1-033: строб снимается, когда принтер снимает готовность
//     (запрос nAC-A), а готовность вернувшаяся - это прерывание;
//   - БК, порт 177714 (БОС): ждёт готовности в разряде 14 входа, поднимает
//     разряд 14 выхода и ждёт, пока готовность пропадёт;
//   - УК-НЦ, ВВ55: подтверждение (PB7) - занятость принтера.
//
// Отключённый принтер (команда plug, параметр plugged или разъём, на котором
// он стоит) строб не слушает и на всех выходах держит 0.
class Printer : public PluggableDevice, public ByteTap
{
private:
    Interface i_data;
    Interface i_strobe;
    Interface i_busy;
    Interface i_ready;
    Interface i_online;

    uint64_t m_char_ticks = 0;          // время байта в тактах домена
    bool m_busy = false;
    int64_t m_left = 0;                 // сколько ещё занят
    unsigned int m_printed = 0;

    void update_lines(bool all = false);

protected:
    void plug_changed() override;

public:
    Printer(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    const char * plug_title() const override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_printer(InterfaceManager *im, EmulatorConfigDevice *cd);
