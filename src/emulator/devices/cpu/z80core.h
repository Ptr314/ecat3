// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Z80 CPU core

#pragma once

#include <cstdint>

#include "z80_context.h"

using namespace Z80;

class z80core
{
private:
    bool process_ints;

    uint8_t next_byte();
    uint8_t read_command();
    uint8_t calc_z80_flags(
                            uint32_t value,         //Value to calc standard flags
                            uint32_t value35,       //Value to get flags 3 and 5
                            uint32_t flags_set,     //Flags to set
                            uint32_t flags_reset,   //Flags to reset
                            uint32_t flags_chg      //Flags to calculate
        );
    void do_ret();
    void do_jump();
    void do_call();
    unsigned int do_jr(unsigned int command, bool cond);
    uint8_t do_rlc(uint8_t v, bool set_szp = false);
    uint8_t do_rrc(uint8_t v, bool set_szp = false);
    uint8_t do_rl(uint8_t v, bool set_szp = false);
    uint8_t do_rr(uint8_t v, bool set_szp = false);
    uint8_t do_sla(uint8_t v);
    uint8_t do_sra(uint8_t v);
    uint8_t do_srl(uint8_t v);
    uint8_t do_sll(uint8_t v);
    uint8_t do_bit(unsigned int bit, uint8_t v);
    uint8_t do_bit_ind(unsigned int bit, uint8_t v, unsigned int address);
    uint8_t do_res(unsigned int bit, uint8_t v);
    uint8_t do_set(unsigned int bit, uint8_t v);
    void do_DD_FD_CB(unsigned int prefix, unsigned int * cycles);
    uint8_t do_add8(uint8_t a, uint8_t b);
    uint8_t do_adc8(uint8_t a, uint8_t b);
    uint8_t do_sub8(uint8_t a, uint8_t b);
    uint8_t do_sbc8(uint8_t a, uint8_t b);
    uint8_t do_and8(uint8_t a, uint8_t b);
    uint8_t do_xor8(uint8_t a, uint8_t b);
    uint8_t do_or8(uint8_t a, uint8_t b);
    void do_cp8(uint8_t a, uint8_t b);
    void do_cpi_cpd(int16_t hlinc);
    void do_ini_ind(int16_t hlinc);
    void do_ldi_ldd(int16_t hlinc);
    void do_outi_outd(int16_t hlinc);
    void do_daa();
    void do_rst(uint16_t address);

    uint32_t get_first_16();
    uint32_t get_second_16(uint32_t PP);
    void store_value_16(uint32_t value);
    uint8_t get_first_8(unsigned int YYY, uint32_t * address, unsigned int *cycles, bool force_hl = false);
    void store_value_8(unsigned int YYY, uint32_t address, uint8_t value, unsigned int * cycles, bool force_hl = false);

    unsigned int execute_command();

protected:
    z80context context;

public:
    z80core();
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(uint16_t address);
    virtual void write_port(uint16_t address, uint8_t value);
    virtual void reset();
    virtual z80context * get_context();
    virtual uint8_t get_command();
    virtual uint16_t get_pc();
    virtual void set_nmi(unsigned int nmi_val);
    virtual void set_int(unsigned int int_val);

    unsigned int execute();
};
