// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8080 (КР580ВМ80) CPU core

#pragma once

#include <cstdint>

#include "i8080_context.h"


class i8080core
{
private:
    uint8_t next_byte();
    uint8_t read_command();
    uint8_t calc_base_flags(uint32_t value);
    uint8_t calc_half_carry(uint8_t v1, uint8_t v2, uint8_t c);
    void do_ret();
    void do_jump();
    void do_call();

protected:
    i8080context context;

public:
    i8080core();
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(uint16_t address);
    virtual void write_port(uint16_t address, uint8_t value);
    virtual void inte_changed(unsigned int inte);
    virtual void reset();
    virtual i8080context * get_context();

    unsigned int execute();
    virtual uint8_t get_command()
    {
        return read_mem(context.registers.regs.PC);
    }

    virtual uint16_t get_pc()
    {
        return context.registers.regs.PC;
    }

};
