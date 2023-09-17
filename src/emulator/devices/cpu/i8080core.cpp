#include "i8080core.h"

i8080core::i8080core()
{
    //TODO: 8080 core constructor
    context.registers.regs.PC = 0;
    context.halted = false;
}

inline uint8_t i8080core::next_byte()
{
    return read_mem(context.registers.regs.PC++);
}

inline uint8_t i8080core::read_command()
{
    //TODO: flipping M1
    return next_byte();
}

inline uint8_t i8080core::calc_flags(uint32_t v1, uint32_t v2, uint32_t value)
{

    return    ((v1^v2^value) & F_HALF_CARRY)
           || ( (value >> 8) & F_CARRY )
           || ZERO_SIGN[(uint8_t)value]
           || PARITY[(uint8_t)value];
}

inline void i8080core::do_ret()
{
    PartsRecLE T;
    T.b.L = read_mem(context.registers.regs.SP);
    T.b.H = read_mem(context.registers.regs.SP+1);
    context.registers.regs.PC = T.w;
    context.registers.regs.SP += 2;
}

inline void i8080core::do_jump()
{
    PartsRecLE T;
    T.b.L = next_byte();
    T.b.H = next_byte();
    context.registers.regs.PC = T.w;
}

unsigned int i8080core::execute()
{
    uint8_t command;
    unsigned int XX, YYY, ZZZ, PP, Q;
    PartsRecLE T, D;
    unsigned int timer;

}
