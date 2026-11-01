// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 (К1810ВМ86/ВМ88) emulator interface class

#pragma once

#include "emulator/core.h"
#include "i8086core.h"

class i8086;

// A board that serves the processor's I/O cycles itself and holds it meanwhile. The
// core cannot stop in the middle of an instruction, so a write is latched and the
// processor stops at the next instruction (gate_ready), and a read is told before
// the instruction starts and is run when the data is there (gate_input).
class I8086Gate
{
public:
    virtual ~I8086Gate() {}
    virtual bool gate_ready() = 0;
    virtual bool gate_input(uint16_t port, bool word) = 0;
    virtual uint16_t gate_in(uint16_t port, bool word) = 0;
    virtual void gate_out(uint16_t port, uint16_t value, bool word) = 0;
};

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
    uint8_t  mem_peek8(uint32_t address) override;
};

// Emulator device
//
// Lines: intr (from the interrupt controller, active high), nmi (active high,
// taken on the rising edge), inta (pulsed once per acknowledge, the
// controller puts the vector out on its rising edge) and vector (8 bits, wired
// to the controller's data output). The real chip runs two INTA cycles; the
// i8259 here acknowledges on the first edge it sees, so one pulse is driven
// and both cycles are timed. reset (active high) holds the processor, and it starts
// from FFFF0 when the line falls; unconnected, it is never held. The gate parameter
// names an I8086Gate device, then all I/O goes through it
class i8086 : public CPU
{
    friend class I8086Core;
private:
    Interface i_intr;
    Interface i_nmi;
    Interface i_inta;
    Interface i_vector;
    Interface i_reset;

    I8086Core * core;
    int m_family;
    I8086Gate * m_gate = nullptr;
    bool m_held = false;

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
    bool set_flag(const std::string &name, bool value) override;
    bool get_sp(unsigned int &sp) override;
    bool stack_address(int i, unsigned int &address) override;
    bool waiting() override;
    bool halted() { return core->get_context()->halted; }
    std::vector<unsigned int> get_history() override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_i8086(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_i8088(InterfaceManager *im, EmulatorConfigDevice *cd);
