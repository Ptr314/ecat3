// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8080 (КР580ВМ80) CPU core for an external library test adaptation

#pragma once

//#include <cstdint>

#include "i8080/i8080.h"

#include "i8080_context.h"

#define CARRY_BIT     0
#define PARITY_BIT    2
#define AUX_CARRY_BIT 4
#define ZERO_BIT      6
#define SIGN_BIT      7

i8080_word_t mem_read(const struct i8080* context, i8080_addr_t addr);
void mem_write(const struct i8080*, i8080_addr_t addr, i8080_word_t word);
i8080_word_t io_read(const struct i8080*, i8080_word_t port);
void io_write(const struct i8080*, i8080_word_t port, i8080_word_t word);

class i8080core
{
private:

protected:
    i8080 cpu_context;
    i8080context ex_cont;

public:
    i8080core()
    {
        cpu_context.mem_read = &mem_read;
        cpu_context.mem_write = &mem_write;
        cpu_context.io_read = &io_read;
        cpu_context.io_write = &io_write;
        cpu_context.udata = this;
        i8080_reset(&cpu_context);
    }

    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(uint16_t address){return 0;};
    virtual void write_port(uint16_t address, uint8_t value){};
    virtual void inte_changed(unsigned int inte){};
    virtual void reset()
    {
        i8080_reset(&cpu_context);
    };
    virtual i8080context * get_context()
    {
        ex_cont.registers.regs.A = cpu_context.a;
        ex_cont.registers.regs.B = cpu_context.b;
        ex_cont.registers.regs.C = cpu_context.c;
        ex_cont.registers.regs.D = cpu_context.d;
        ex_cont.registers.regs.E = cpu_context.e;
        ex_cont.registers.regs.H = cpu_context.h;
        ex_cont.registers.regs.L = cpu_context.l;
        ex_cont.registers.regs.SP = cpu_context.sp;
        ex_cont.registers.regs.PC = cpu_context.pc;

        i8080_word_t flags = 0x02;
        flags |= (
            (cpu_context.cy << CARRY_BIT) |
            (cpu_context.p << PARITY_BIT) |
            (cpu_context.ac << AUX_CARRY_BIT) |
            (cpu_context.z << ZERO_BIT) |
            (cpu_context.s << SIGN_BIT));

        ex_cont.registers.regs.F = flags;

        return &ex_cont;
    };

    virtual uint8_t get_command()
    {
        return read_mem(cpu_context.pc);
    }

    virtual uint16_t get_pc()
    {
        return cpu_context.pc;
    }

    unsigned int execute(){
        i8080_next(&cpu_context);
        int c = cpu_context.cycles;
        cpu_context.cycles = 0;
        return c;
    }
};
