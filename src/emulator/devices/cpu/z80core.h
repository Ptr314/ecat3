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
    uint8_t calc_base_flags(uint32_t value);
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
    void do_bit(unsigned int bit, uint8_t v);
    uint8_t do_res(unsigned int bit, uint8_t v);
    uint8_t do_set(unsigned int bit, uint8_t v);

protected:
    z80context context;

public:
    z80core();
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(uint16_t address);
    virtual void write_port(uint16_t address, uint8_t value);
    virtual void inte_changed(unsigned int inte);
    virtual void reset();
    virtual z80context * get_context();

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
