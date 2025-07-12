// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Zilog Z80 emulator interface class

#pragma once

#include "emulator/core.h"

#ifndef EXTERNAL_Z80
#include "emulator/devices/cpu/z80core.h"
#else
#include "libs/z80.hpp"
#endif

//using namespace z80;

class z80;

#ifndef EXTERNAL_Z80
//Library wrapper
class z80Core: public z80core
{
private:
    z80 * emulator_device;

public:
    z80Core(z80 * emulator_device);
    virtual uint8_t read_mem(uint16_t address) override;
    virtual void write_mem(uint16_t address, uint8_t value) override;
    virtual uint8_t read_port(uint16_t address) override;
    virtual void write_port(uint16_t address, uint8_t value) override;
    //virtual void inte_changed(unsigned int inte) override;
};
#endif

//Emulator class
class z80: public CPU
{
private:
    Interface i_nmi;
    Interface i_int;
    Interface i_m1;

#ifndef EXTERNAL_Z80
    z80core * core;
#else
    Z80 * core_ext;
#endif

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

protected:
    virtual unsigned int get_pc() override;

#ifdef LOG_CPU
    virtual void log_state(uint8_t command, bool before, unsigned int cycles=0) override;
#endif

public:
    z80(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~z80();
    virtual void reset(bool cold) override;
    virtual unsigned int execute() override;
    virtual unsigned int read_mem(unsigned int address) override;
    virtual void write_mem(unsigned int address, unsigned int data) override;
    virtual unsigned int read_port(unsigned int address);
    virtual void write_port(unsigned int address, unsigned int data);
    //virtual void inte_changed(unsigned int inte);

    virtual QList<QPair<QString, QString>> get_registers() override;
    virtual QList<QPair<QString, QString>> get_flags() override;
    virtual unsigned int get_command() override;

    virtual void set_context_value(QString name, unsigned int value) override;

};

unsigned int read_mem(unsigned int address);
void write_mem(unsigned int address, unsigned int data);

ComputerDevice * create_z80(InterfaceManager *im, EmulatorConfigDevice *cd);
