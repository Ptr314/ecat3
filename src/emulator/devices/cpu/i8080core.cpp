#include "i8080core.h"

i8080core::i8080core()
{
    //TODO: 8080 core constructor
    reset();
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
    T.b.H = read_mem(static_cast<uint16_t>(context.registers.regs.SP+1));
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

inline void i8080core::do_call()
{
    PartsRecLE T;
    T.b.L = next_byte();
    T.b.H = next_byte();
    context.registers.regs.SP -= 2;
    write_mem(context.registers.regs.SP, context.registers.regs.PC & 0xFF);
    write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), context.registers.regs.PC >> 8);
    context.registers.regs.PC = T.w;
}

void i8080core::reset()
{
    context.registers.regs.PC = 0;
    context.halted = false;
    context.int_enable = 0;
}

unsigned int i8080core::execute()
{
    uint8_t command;
    unsigned int XX, YYY, ZZZ, PP, Q;
    PartsRecLE T, D;
    unsigned int cycles;

    //Store PC for debug purposes
    context.registers.regs.PC2 = context.registers.regs.PC;

    command = next_byte();

    //XX YYY ZZZ
    //   PPQ
    XX = command >> 6;
    YYY = (command >> 3) & 0x07;
    ZZZ = command & 0x07;
    PP = (YYY >> 1) & 0x03;
    Q = YYY & 1;

    //Initially we take the first value
    cycles = TIMING[command][0];

    switch (XX) {
    case 0:
        //00_YYY_ZZZ
        switch (ZZZ) {
        case 0:
            //00_YYY_000
            //NOP
            break;
        case 1:
            //00_YYY_001
            switch (Q) {
            case 0:
                //00_RP0_001
                //LXI RP, DATA16
                T.b.L = next_byte();
                T.b.H = next_byte();
                if (PP == 3)
                    context.registers.regs.SP = T.w;
                else
                    context.registers.reg_array_16[PP] = T.w;
                break;
            default:
                //00_RP1_001
                //DAD RP
                //TODO: Implement
                break;
            }
            break;
        }
        break;
    case 1:
        //01_YYY_ZZZ
        break;
    case 2:
        //10_YYY_ZZZ
        break;
    case 3:
        //11_YYY_ZZZ
        break;
    }


}
