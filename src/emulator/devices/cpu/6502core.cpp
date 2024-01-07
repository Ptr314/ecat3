#include "6502core.h"

#define REG_A context.A
#define REG_X context.X
#define REG_Y context.Y
#define REG_P context.P
#define REG_S context.S

#define REG_PC context.reg16.PC
#define REG_PCH context.reg8.PCH
#define REG_PCL context.reg8.PCL


mos6502core::mos6502core(int family_type)
{
    memset(&context, 0, sizeof(context));
    context.type = family_type;
}

void mos6502core::reset()
{
    REG_PC = 0;
}

mos6502context * mos6502core::get_context()
{
    return &context;
}

inline uint8_t mos6502core::next_byte()
{
    return read_mem(REG_PC++);
}

inline uint8_t mos6502core::read_command()
{
    return next_byte();
}


uint16_t mos6502core::get_pc()
{
    return REG_PC;
}

uint8_t mos6502core::get_command()
{
    return read_mem(REG_PC);
}

void mos6502core::set_nmi(unsigned int nmi_val)
{
    //TODO: 6502 NMI
}

void mos6502core::set_int(unsigned int int_val)
{
    //TODO: 6502 INT
}

unsigned int mos6502core::execute()
{
    return 0;
}
