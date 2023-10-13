#ifndef I8080_CONTEXT_H
#define I8080_CONTEXT_H

#include <cstdint>

#pragma pack(1)
struct i8080context
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
    //TODO: i8080 maybe it should be bool
    unsigned int int_enable;
    unsigned int debug_mode;
};

#pragma pack()

namespace I8080
{
    const uint8_t F_BASE_8080 = 0x02; //This bit is always set

    const uint8_t F_CARRY      =  0x01;
    const uint8_t F_PARITY     =  0x04;
    const uint8_t F_HALF_CARRY =  0x10;
    const uint8_t F_ZERO       =  0x40;
    const uint8_t F_SIGN       =  0x80;
    const uint8_t F_ALL        =  (F_BASE_8080 + F_CARRY + F_PARITY + F_HALF_CARRY + F_ZERO + F_SIGN);
}

#endif // I8080_CONTEXT_H
