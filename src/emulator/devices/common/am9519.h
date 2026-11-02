// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Am9519 (КР1818ВН19) universal interrupt controller on a К1801 bus

#pragma once

#include "emulator/core.h"
#include "virq_line.h"

// Eight request lines, each with its own vector. A rising edge on ~irq sets a
// bit of the request register, and it stays there until the chip serves it or
// the program clears it; the request reaches the processor as a vectored one
// (~virq, ~vector). A chip chained after this one comes in on ~virq_in and
// ~vector_in, so the order of the chips is the order of their priority.
//
// Registers (the machine's description, ТО2 Э-85, 4.2.14): the even word is the
// data register, the next one the command register on a write and the status
// on a read. What the data register reaches is chosen by a command.
class Am9519: public AddressableDevice
{
private:
    Interface i_irq;
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    Interface i_iako;
    VirqLine m_irq;

    uint8_t m_irr = 0;              // requests
    uint8_t m_isr = 0;              // in service
    uint8_t m_imr = 0xFF;           // masked
    uint8_t m_acr = 0;              // auto clear: served without entering ISR
    uint8_t m_mode = 0;
    uint8_t m_vectors[8] = {};
    unsigned int m_write = 0;       // what the next data write goes to
    unsigned int m_write_line = 0;
    unsigned int m_last = 7;        // the line served last, for the rotating priority
    unsigned int m_own = 0;         // own vector on offer, 0 for none
    int m_own_line = -1;

    unsigned int m_served = 0;

    void chip_reset();
    int highest() const;
    void select_own();
    void update();
    void command(unsigned int value);
    unsigned int status() const;

public:
    Am9519(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
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

ComputerDevice * create_am9519(InterfaceManager *im, EmulatorConfigDevice *cd);
