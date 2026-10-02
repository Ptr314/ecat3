// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 (К1810ВМ86/ВМ88) emulator interface class

#pragma once

#include "emulator/core.h"
#include "i8086core.h"

class i8086;

// Library wrapper
class I8086Core: public i8086core
{
private:
    i8086 * emulator_device;

public:
    I8086Core(i8086 * emulator_device, int family);
    uint8_t  mem_read8(uint32_t address) override;
    void     mem_write8(uint32_t address, uint8_t value) override;
    uint16_t mem_read16(uint32_t address) override;
    void     mem_write16(uint32_t address, uint16_t value) override;
    uint8_t  io_read8(uint16_t port) override;
    void     io_write8(uint16_t port, uint8_t value) override;
    uint16_t io_read16(uint16_t port) override;
    void     io_write16(uint16_t port, uint16_t value) override;
    unsigned mem_wait(uint32_t address, bool write, unsigned offset) override;
    uint8_t  int_ack() override;
};

// Emulator device
//
// Lines: intr (from the interrupt controller, active high), nmi (active high,
// taken on the rising edge), inta (pulsed once per acknowledge, the
// controller puts the vector out on its rising edge) and vector (8 bits, wired
// to the controller's data output). The real chip runs two INTA cycles; the
// i8259 here acknowledges on the first edge it sees, so one pulse is driven
// and both cycles are timed
class i8086 : public CPU
{
    friend class I8086Core;
private:
    Interface i_intr;
    Interface i_nmi;
    Interface i_inta;
    Interface i_vector;

    I8086Core * core;
    int m_family;

    //Linear addresses of the last instructions executed, for LOG cpu.history
    enum { HISTORY_SIZE = 256 };
    uint32_t m_history[HISTORY_SIZE] = {};
    unsigned m_history_pos = 0;

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

protected:
    unsigned int get_pc() override;

public:
    i8086(InterfaceManager *im, EmulatorConfigDevice *cd, int family);
    ~i8086();

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    unsigned int execute() override;
    unsigned int read_mem(unsigned int address) override;
    void write_mem(unsigned int address, unsigned int data) override;

    std::vector<std::pair<std::string, std::string>> get_registers() override;
    std::vector<std::pair<std::string, std::string>> get_flags() override;
    unsigned int get_command() override;
    DisAsm * create_disasm(const std::string &data_path, emulator::Result &res) override;

    void set_context_value(const std::string &name, unsigned int value) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_i8086(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_i8088(InterfaceManager *im, EmulatorConfigDevice *cd);
