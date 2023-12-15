#ifndef Z80CORE_H
#define Z80CORE_H

#include <cstdint>

#include "z80_context.h"

using namespace Z80;

class z80core
{
private:
    uint8_t next_byte();
    uint8_t read_command();
    //uint8_t calc_base_flags(uint32_t value);
    uint8_t calc_z80_flags(
                            uint32_t value,         //Value to calc standard flags
                            uint32_t value35,       //Value to get flags 3 and 5
                            uint32_t flags_set,     //Flags to set
                            uint32_t flags_reset,   //Flags to reset
                            uint32_t flags_chg      //Flags to calculate
        );
    uint8_t calc_overflow(uint32_t a, uint32_t b, uint32_t result, uint32_t position);
    uint8_t calc_half_carry(uint8_t v1, uint8_t v2, uint8_t c);
    void do_ret();
    void do_jump();
    void do_call();
    unsigned int do_jr(unsigned int command, bool cond);
    uint8_t do_rlc(uint8_t v);
    uint8_t do_rrc(uint8_t v);
    uint8_t do_rl(uint8_t v);
    uint8_t do_rr(uint8_t v);
    uint8_t do_sla(uint8_t v);
    uint8_t do_sra(uint8_t v);
    uint8_t do_srl(uint8_t v);
    uint8_t do_sll(uint8_t v);
    uint8_t do_bit(unsigned int bit, uint8_t v);
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

    uint32_t get_first_16();
    uint32_t get_second_16(uint32_t PP);
    void store_value_16(uint32_t value);
    uint8_t get_first_8(unsigned int YYY, uint32_t * address, unsigned int *cycles);
    void store_value_8(unsigned int YYY, uint32_t address, uint8_t value, unsigned int * cycles);

    unsigned int execute_command();

protected:
    z80context context;

public:
    z80core();
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(uint16_t address);
    virtual void write_port(uint16_t address, uint8_t value);
    //virtual void inte_changed(unsigned int inte);
    virtual void reset();
    virtual z80context * get_context();
    virtual void set_nmi(unsigned int nmi_val);
    virtual void set_int(unsigned int int_val);

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

#endif // Z80CORE_H
