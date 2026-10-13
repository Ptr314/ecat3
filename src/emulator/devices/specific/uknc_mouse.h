// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ mouse, register 177400 of the peripheral processor

#pragma once

#include <atomic>

#include "emulator/core.h"
#include "emulator/devices/common/mouse.h"

// Мышь УК-НЦ (github.com/Ptr314/ecat3/issues/62): одно слово 177400 на шине
// периферийного процессора. Чтение отдаёт смещение с прошлого чтения и кнопки:
//   разряды 1-7  - X со знаком, растёт слева направо;   разряд 0 - правая кнопка
//   разряды 9-15 - Y со знаком, растёт снизу вверх;     разряд 8 - левая кнопка
// Смещение - не больше 63 по модулю: что сверх того, остаётся до следующего
// чтения. На адаптере -64 по обеим осям с обеими кнопками (100601) значит
// «мышь не готова» - такого слова эмулятор не выдаёт. Смещения «квадратные»,
// пиксельный аспект разбирает программа.
//
// Мышь включена с пуска; кнопка на панели инструментов её выключает, и тогда
// она не отвечает вовсе (диапазон routed, отдаёт адрес дальше): чтение 177400
// кончается таймаутом шины, как без неё. Указатель хоста окно захватывает
// только после того, как программа прочла регистр (mouse_used()).
class UKNCMouse: public AddressableDevice, public HostMouse
{
private:
    MemoryMapper * m_mapper = nullptr;
    std::string m_icon;
    std::atomic<bool> m_on;
    // Программа читала регистр после сброса: только тогда щелчок в окне
    // захватывает указатель
    std::atomic<bool> m_used;

    // Пишет хост (окно или сценарий), забирает поток эмуляции
    std::atomic<int> m_dx;              // вправо, влево - отрицательные
    std::atomic<int> m_dy;              // вниз, вверх - отрицательные
    std::atomic<unsigned> m_buttons;    // разряд 0 - левая, 1 - правая

    unsigned m_last = 0;                // последнее прочитанное слово
    unsigned m_reads = 0;

    static void add_pending(std::atomic<int> &axis, int delta);
    static int take(std::atomic<int> &axis);
    static unsigned compose(int dx, int dy, unsigned buttons);
    unsigned read_word();

public:
    UKNCMouse(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force = false) override;
    unsigned int route(unsigned int address, unsigned int mode) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;
    ConfigFields get_config_fields() override { return { mouse_switch_field() }; }

    void move(int dx, int dy, int buttons) override;
    bool mouse_plugged() const override { return m_on; }
    bool mouse_used() const override { return m_used; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_uknc_mouse(InterfaceManager *im, EmulatorConfigDevice *cd);
