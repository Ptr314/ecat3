// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Юниор ФВ-6506: дополнительная память и пересылки в нее через ПДП

#pragma once

#include "emulator/core.h"

class I8257;

// Дополнительная память Юниора - до семи блоков по 64 Кбайт - в адресное
// пространство процессора не попадает вообще. Добраться до нее можно только
// пересылкой ПДП парой каналов 0/1 ВТ57: номер блока выставляется разрядами
// 0-2 порта C системного ВВ55 (активный ноль), сторону, на которой стоит блок,
// задает триггер направления по порту $50, а какая сторона читается, а какая
// пишется - разряды режима в счетчиках самих каналов.
//
// Устройство слушает порт $50 и следит за регистром режима ВТ57: включение
// каналов 0 и 1 вместе и есть команда "перешли". Процессор на время пересылки
// снимается с шины (hold), как оно и происходит на машине.
class UniorMemory: public AddressableDevice
{
private:
    Interface i_block;              // Разряды номера блока со стороны канала 0
    // Вторая линия выбора блока. У Юниора ее нет: номер блока там один, а
    // сторону пересылки задает триггер порта $50. У Арго регистров выбора два
    // ($A1 и $A9), номер блока стоит только в одном из них, и какой это
    // регистр - то и есть направление
    Interface i_block2;

    AddressableDevice * Main = nullptr;
    AddressableDevice * Ext = nullptr;
    I8257 * DMA = nullptr;

    unsigned int m_blocks = 1;
    unsigned int m_block_mask = 0x07;   // Разряды номера блока в регистре
    unsigned int m_block_shift = 0;
    bool m_block_invert = true;         // Активный ноль, как у Юниора
    unsigned int m_direction = 0;   // Триггер по порту $50
    unsigned int m_last_block = 0;  // Номер блока последней пересылки
    unsigned int m_last_count = 0;
    uint64_t m_transfers = 0;
    uint64_t m_bytes = 0;

    unsigned int block_of(const Interface &i) const;
    unsigned int selected_block() const;
    unsigned int direction() const;
    unsigned int ext_read(unsigned int block, unsigned int address);
    void ext_write(unsigned int block, unsigned int address, unsigned int value);
    void run_transfer();

public:
    UniorMemory(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_unior_memory(InterfaceManager *im, EmulatorConfigDevice *cd);
