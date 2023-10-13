#ifndef Z80_CONTEXT_H
#define Z80_CONTEXT_H

#include <cstdint>

#pragma pack(1)

struct z80context
{
    union {
        struct{
            uint8_t  C,B,E,D,L,H,F,A,IXL,IXH,IYL,IYH,I,R;
            uint16_t SP, PC, AF_, BC_, DE_, HL_;
        } regs;
        struct {
            uint16_t BC, DE, HL, AF, IX, IY;
        } reg_pairs;
        uint8_t reg_array_8[8];
        uint16_t reg_array_16[3];
    } registers;
    bool halted;
    //TODO: Z80 int system
    unsigned int int_enable;
    unsigned int debug_mode;
};

#pragma pack()

namespace Z80
{
    //TODO: right flags
    const uint8_t F_BASE_8080  = 0x02; //This bit is always set

    const uint8_t F_CARRY      = 1;
    const uint8_t F_PARITY     = (1 << 2); //0x04
    const uint8_t F_B3         = (1 << 3); //0x08
    const uint8_t F_HALF_CARRY = (1 << 4); //0x10
    const uint8_t F_B5         = (1 << 5); //0x20
    const uint8_t F_ZERO       = (1 << 6); //0x40
    const uint8_t F_SIGN       = (1 << 7); //0x80
    const uint8_t F_ALL        = (F_BASE_8080 + F_CARRY + F_PARITY + F_HALF_CARRY + F_ZERO + F_SIGN);
}

#endif // Z80_CONTEXT_H
