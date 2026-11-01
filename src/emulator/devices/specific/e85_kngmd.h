// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НГМД Электроники МС 0585: обвязка 8751 «Кочан» и КР1818ВГ93

#pragma once

#include "emulator/core.h"

class WD1793;

// Всё, что делает контроллер, делает прошивка на i8051; устройство - то, чем она
// связана с машиной (разбор Alex_K, КОЧАН_ОЗУ_ПОРТЫ.TXT, и ТО4). Плата держит
// регистры обмена, буфер сектора 512 байт со счётчиком и триггер магистрали.
//
// Со стороны ЦП (диапазон без базы): X00-X16 - регистры, по записи и по чтению
// разные (восемь и восемь), X20 РЗЧ - буфер, X22 РНА - сброс адреса буфера,
// X24 РСТ - запуск: отдаёт магистраль контроллеру, X26 РЗ - запись в буфер.
//
// Со стороны 8051 (порты mapper'а платы, база 1000, младший байт адреса, старшие
// разряды MOVX на плате никуда не идут): 00-0E - те же регистры, P1.3 выбирает набор,
// 10 - буфер, 12 - сброс адреса, 14 - вернуть магистраль ЦП, 16 - запись в буфер,
// 18-1E - регистры ВГ93.
//
// Выводы 8051: P1.0 - сторона (1 - низ), P1.1-2 - накопитель (инверсно), P1.3 -
// набор регистров, P1.4 - INTB, P1.5 - INTA (0 - запрос); P2.0 - мотор, P2.1 - запрет
// выбора, P2.4 - INTRQ, P2.5 - DRQ, P2.6 - магистраль у контроллера, P2.7 - готовность.
class E85Kngmd: public AddressableDevice
{
private:
    Interface i_p1;
    Interface i_p2;
    Interface i_pins;           // ко входу P2 процессора
    Interface i_drq;
    Interface i_intrq;
    Interface i_init;
    Interface i_reset;          // RST процессора
    Interface i_select;
    Interface i_side;
    Interface i_motor;
    Interface i_irqa;           // операция завершена
    Interface i_irqb;           // сменилось состояние накопителей

    WD1793 * m_fdc = nullptr;

    uint8_t m_write[8] = {};    // регистры записи ЦП
    uint8_t m_read[8] = {};     // регистры чтения ЦП
    uint8_t m_buffer[512];
    unsigned int m_pointer = 0;
    bool m_busy = false;        // магистраль у контроллера
    unsigned int m_ready_wait = 0;

    unsigned int m_commands = 0;

    void update_pins();
    void update_outputs();
    unsigned int mcu_read(unsigned int address);
    void mcu_write(unsigned int address, unsigned int value);
    unsigned int host_read(unsigned int address);
    void host_write(unsigned int address, unsigned int value);
    void start();

public:
    E85Kngmd(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_e85_kngmd(InterfaceManager *im, EmulatorConfigDevice *cd);
