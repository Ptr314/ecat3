// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8080 emulator interface class

#pragma once

#include "emulator/core.h"
#include "emulator/devices/cpu/i8080core.h"


using namespace I8080;

class i8080;

//Library wrapper
class I8080Core: public i8080core
{
private:
    i8080 * emulator_device;

public:
    I8080Core(i8080 * emulator_device);
    virtual uint8_t read_mem(uint16_t address) override;
    virtual void write_mem(uint16_t address, uint8_t value) override;
    virtual uint8_t read_port(uint16_t address) override;
    virtual void write_port(uint16_t address, uint8_t value) override;
    virtual void inte_changed(unsigned int inte) override;
    virtual bool int_request() override;
    virtual uint8_t int_acknowledge() override;
    virtual uint16_t int_call_address() override;
};

//Emulator class
class i8080: public CPU
{
private:
    Interface i_nmi;
    Interface i_int;
    Interface i_inte;
    Interface i_m1;
    //The acknowledge strobe of a ВН59 and the CALL address it answers with
    Interface i_inta;
    Interface i_int_address;

    i8080core * core;

    //Instruction read during the interrupt acknowledge, or 0x100 when this
    //machine does not take interrupts on INT at all
    unsigned int m_int_opcode;

protected:
    virtual unsigned int get_pc() override;

public:
    i8080(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~i8080();
    virtual void reset(bool cold) override;
    emulator::Result load_config(SystemData *sd) override;
    virtual unsigned int execute() override;
    virtual unsigned int read_mem(unsigned int address) override;
    virtual void write_mem(unsigned int address, unsigned int data) override;
    virtual unsigned int read_port(unsigned int address);
    virtual void write_port(unsigned int address, unsigned int data);
    virtual void inte_changed(unsigned int inte);
    bool int_request();
    uint8_t int_acknowledge();
    uint16_t int_call_address();

    virtual std::vector<std::pair<std::string, std::string>> get_registers() override;
    virtual std::vector<std::pair<std::string, std::string>> get_flags() override;
    virtual unsigned int get_command() override;
    virtual std::string disasm_table() const override { return "i8080.dis"; }

    virtual void set_context_value(const std::string &name, unsigned int value) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

};

unsigned int read_mem(unsigned int address);
void write_mem(unsigned int address, unsigned int data);

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd);
