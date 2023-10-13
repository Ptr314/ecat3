#ifndef Z80_CONTEXT_H
#define Z80_CONTEXT_H

#include <cstdint>

#pragma pack(1)
struct z80context
{
    union {
        struct{
            uint8_t  C,B,E,D,L,H,
                m,
                A, F;
            uint16_t SP, PC;
        } regs;
        struct {
            uint16_t BC, DE, HL;
        } reg_pairs;
        uint8_t reg_array_8[8];
        uint16_t reg_array_16[3];
    } registers;
    bool halted;
    //TODO: z80 maybe it should be bool
    unsigned int int_enable;
    unsigned int debug_mode;
};

#pragma pack()

#define F_BASE_8080     0x02 //This bit is always set

#define F_CARRY         0x01
#define F_PARITY        0x04
#define F_HALF_CARRY    0x10
#define F_ZERO          0x40
#define F_SIGN          0x80
#define F_ALL           (F_BASE_8080 + F_CARRY + F_PARITY + F_HALF_CARRY + F_ZERO + F_SIGN)

#endif // Z80_CONTEXT_H
