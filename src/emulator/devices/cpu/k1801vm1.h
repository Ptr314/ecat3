// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВМ1 / К1801ВМ2 emulator interface class

#pragma once

#include "emulator/core.h"
#include "pdp11core.h"

class k1801vm1;

// Library wrapper
class K1801VM1Core: public pdp11core
{
private:
    k1801vm1 * emulator_device;

public:
    K1801VM1Core(k1801vm1 * emulator_device, int family_type);
    virtual uint16_t read_word(uint16_t address) override;
    virtual void write_word(uint16_t address, uint16_t value) override;
    virtual uint8_t read_byte(uint16_t address) override;
    virtual void write_byte(uint16_t address, uint8_t value) override;
};

// Emulator device
class k1801vm1 : public CPU
{
private:
    Interface i_virq;               // vectored interrupt request
    Interface i_vector;             // vector supplied by the requesting device
    Interface i_irq2;               // request with the fixed vector 0100
    Interface i_irq3;               // request with the fixed vector 0270
    Interface i_halt;               // console (halt mode) request

    K1801VM1Core * core;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

public:
    k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type);
    virtual ~k1801vm1();

    virtual emulator::Result load_config(SystemData *sd) override;
    virtual void reset(bool cold) override;
    virtual unsigned int execute() override;

    // Byte access, used by the debugger and the disassembler
    virtual unsigned int read_mem(unsigned int address) override;
    virtual void write_mem(unsigned int address, unsigned int data) override;

    // Word access, the native width of the bus
    unsigned int read_mem_word(unsigned int address);
    void write_mem_word(unsigned int address, unsigned int data);

    virtual std::vector<std::pair<std::string, std::string>> get_registers() override;
    virtual std::vector<std::pair<std::string, std::string>> get_flags() override;
    virtual unsigned int get_pc() override;
    virtual unsigned int get_command() override;

    virtual void set_context_value(const std::string &name, unsigned int value) override;
};

ComputerDevice * create_k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_k1801vm2(InterfaceManager *im, EmulatorConfigDevice *cd);
