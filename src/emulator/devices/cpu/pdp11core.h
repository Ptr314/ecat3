// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: PDP-11 compatible CPU core (К1801ВМ1, К1801ВМ2)

#pragma once

#include <cstdint>
#include "pdp11_context.h"

class pdp11core
{
protected:
    pdp11context context;

    bool has_eis;               // MUL/DIV/ASH/ASHC, absent on the 1801ВМ1

    // Interrupt inputs. All of them are level sensitive, the requesting
    // device is responsible for dropping the line once served.
    bool is_virq;               // vectored request over the bus
    uint16_t virq_vector;
    bool is_irq2;               // fixed vector 0100
    bool is_irq3;               // fixed vector 0270
    bool is_halt_req;           // external console (halt mode) request

    // Set by a failed bus access; aborts the current instruction and
    // turns into a trap through vector 4 once it unwinds.
    bool m_abort;

    // RTT defers the trace trap until after the following instruction
    bool m_no_trace;


    uint16_t fetch();
    uint16_t read_word_checked(uint16_t address);
    void write_word_checked(uint16_t address, uint16_t value);

    void set_flag(uint16_t flag, bool value);
    bool get_flag(uint16_t flag) const;
    void set_nz(uint16_t value, bool is_byte);

    pdp11operand decode_operand(unsigned int spec, bool is_byte, unsigned int & cycles);
    uint16_t read_operand(const pdp11operand & op, bool is_byte);
    void write_operand(const pdp11operand & op, bool is_byte, uint16_t value);

    void push(uint16_t value);
    uint16_t pop();
    void do_trap(uint16_t vector);
    void enter_halt_mode();
    bool check_interrupts(unsigned int & cycles);
    void do_branch(uint16_t command, bool condition, unsigned int & cycles);

    bool execute_single(uint16_t command, unsigned int & cycles);
    bool execute_double(uint16_t command, unsigned int & cycles);
    bool execute_misc(uint16_t command, unsigned int & cycles);

public:
    // Where execution begins after INIT. The address is wired outside the chip
    // and differs between systems: the БК starts executing straight from it,
    // while other 1801 machines keep a PC/PSW pair there and are configured
    // with start_from_vector.
    uint16_t start_address;
    bool start_from_vector;

    // Vector used by the HALT instruction and the external console request
    uint16_t halt_vector;

    pdp11core(int family_type);
    virtual ~pdp11core(){};

    // Bus access, provided by the emulator device
    virtual uint16_t read_word(uint16_t address) = 0;
    virtual void write_word(uint16_t address, uint16_t value) = 0;
    virtual uint8_t read_byte(uint16_t address) = 0;
    virtual void write_byte(uint16_t address, uint8_t value) = 0;

    virtual void reset();
    virtual pdp11context * get_context();
    virtual uint16_t get_pc();
    uint16_t get_command();

    virtual void set_virq(bool state, uint16_t vector);
    virtual void set_irq2(bool state);
    virtual void set_irq3(bool state);
    virtual void set_halt(bool state);

    unsigned int execute();
};
