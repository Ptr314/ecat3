#include <QDebug>
#include <QException>

#include "cpu_utils.h"
#include "z80core.h"

using namespace Z80;

#define CARRY       (context.registers.regs.F & F_CARRY)
#define HALF_CARRY  (context.registers.regs.F & F_HALF_CARRY)

#define REG_A       context.registers.regs.A
#define REG_B       context.registers.regs.B
#define REG_C       context.registers.regs.C
#define REG_D       context.registers.regs.D
#define REG_E       context.registers.regs.E
#define REG_H       context.registers.regs.H
#define REG_L       context.registers.regs.L
#define REG_IXH     context.registers.regs.IXH
#define REG_IXL     context.registers.regs.IXL
#define REG_IYH     context.registers.regs.IYH
#define REG_IYL     context.registers.regs.IYL

#define REG_F       context.registers.regs.F
#define REG_I       context.registers.regs.I
#define REG_R       context.registers.regs.R

#define REG_AF      context.registers.reg_pairs.AF
#define REG_BC      context.registers.reg_pairs.BC
#define REG_DE      context.registers.reg_pairs.DE
#define REG_HL      context.registers.reg_pairs.HL
#define REG_IX      context.registers.reg_pairs.IX
#define REG_IY      context.registers.reg_pairs.IY

#define REG_SP      context.registers.regs.SP
#define REG_PC      context.registers.regs.PC

#define REG_AF_     context.registers.regs.AF_
#define REG_BC_     context.registers.regs.BC_
#define REG_DE_     context.registers.regs.DE_
#define REG_HL_     context.registers.regs.HL_

const uint32_t FLAGS_35 = F_B3 + F_B5;


//Register id to array index
static const unsigned int REGISTERS8[8] = {1, 0, 3, 2, 5, 4, 6, 7};

static const uint8_t PARITY[256] = {
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // 00-0F
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // 10-1F
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // 20-2F
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // 30-3F
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // 40-4F
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // 50-5F
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // 60-6F
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // 70-7F
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // 80-8F
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // 90-9F
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // A0-AF
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // B0-BF
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,     // C0-CF
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // D0-DF
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,     // E0-EF
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4};    // F0-FF

static const uint8_t ZERO_SIGN[256] = {
    F_ZERO, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,     				  // 00-0F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,          				  // 10-1F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,          				  // 20-2F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,         				  // 30-3F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 						  // 40-4F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    					  // 50-5F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,          				  // 60-6F
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,          				  // 70-7F
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // 80-8F
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // 90-9F
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // A0-AF
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // B0-BF
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // C0-CF
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // D0-DF
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,  // E0-EF
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,
    F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN,F_SIGN}; // F0-FF

static const uint8_t TIMING[256][2] = {
    {4, 4},   {10, 10}, {7, 7},   {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {11, 11}, {7, 7},   {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 00-0F
    {8, 13},  {10, 10}, {7, 7},   {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {12, 12}, {11, 11}, {7, 7},   {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 10-1F
    {7, 12},  {10, 10}, {16, 16}, {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {7, 12},  {11, 11}, {20, 20}, {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 20-2F
    {7, 12},  {10, 10}, {13, 13}, {6, 6},   {11, 11}, {11, 11}, {10, 10}, {4, 4},
    {7, 12},  {11, 11}, {13, 13}, {6, 6},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 30-3F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 40-4F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 50-5F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 60-6F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 70-7F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 80-8F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 90-9F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // A0-AF
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // B0-BF
    {5, 11},  {10, 10}, {10, 10}, {10, 10}, {10, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {10, 10}, {10, 10}, {4, 4},   {10, 17}, {17, 17}, {7, 7},   {11, 11},      // C0-CF
    {5, 11},  {10, 10}, {10, 10}, {10, 10}, {10, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {4, 4},   {10, 10}, {11, 11}, {10, 17}, {4, 4},   {7, 7},   {11, 11},      // D0-DF
    {5, 11},  {10, 10}, {10, 10}, {19, 19}, {10, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {4, 4},   {10, 10}, {4, 4},   {10, 17}, {4, 4},   {7, 7},   {11, 11},      // E0-EF
    {5, 11},  {10, 10}, {10, 10}, {4, 4},   {10, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {6, 6},   {10, 10}, {4, 4},   {10, 17}, {4, 4},   {7, 7},   {11, 11}};  	 // F0-FF

static const uint8_t CONDITIONS[8][2] = {
    {F_ZERO, 0},		 	//NOT ZERO
    {F_ZERO, F_ZERO},		//ZERO
    {F_CARRY, 0},	 		//NOT CARRY
    {F_CARRY, F_CARRY},		//CARRY
    {F_PARITY, 0},	 		//ODD
    {F_PARITY, F_PARITY},   //NOT ODD
    {F_SIGN, 0},            //POSITIVE
    {F_SIGN, F_SIGN}        //NEGATIVE
};

//static const uint8_t z80LENGTHS[256] = {
//    1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,     // 00-0F
//    1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,     // 10-1F
//    1,3,3,1,1,1,2,1,1,1,3,1,1,1,2,1,     // 20-2F
//    1,3,3,1,1,1,2,1,1,1,3,1,1,1,2,1,     // 30-3F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 40-4F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 50-5F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 60-6F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 70-7F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 80-8F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 90-9F
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // A0-AF
//    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // B0-BF
//    1,1,3,3,3,1,2,1,1,1,3,1,3,3,2,1,     // C0-CF
//    1,1,3,2,3,1,2,1,1,1,3,2,3,1,2,1,     // D0-DF
//    1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,     // E0-EF
//    1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1};    // F0-FF


z80core::z80core()
{
    //TODO: z80 core constructor
    context.registers.regs.PC = 0;
    context.halted = false;
    context.int_enable = 0;
}

inline uint8_t z80core::next_byte()
{
    return read_mem(context.registers.regs.PC++);
}

inline uint8_t z80core::read_command()
{
    //TODO: flipping M1
    return next_byte();
}

//inline uint8_t z80core::calc_base_flags(uint32_t value)
//{

//    return F_BASE_8080
//           | ( (value >> 8) & F_CARRY )
//           | ZERO_SIGN[(uint8_t)value]
//           | PARITY[(uint8_t)value];
//}

inline uint8_t z80core::calc_z80_flags(
                        uint32_t value,         //Value for standard flags
                        uint32_t value35,       //Value for flags 3 and 5
                        uint32_t flags_set,     //Flags to be set
                        uint32_t flags_reset,   //Flags to be reset
                        uint32_t flags_chg      //Flags to be calculated, others are unchanged
                    )
{
    uint32_t result;
    uint32_t mask_reset = ~(flags_reset + flags_chg);

    result = (REG_F & mask_reset) | flags_set;      //Set and reset selected flags, reset flags to be changed
    result |= ( ( (value >> 8) & F_CARRY )          //0x100 as CARRY
                | (value35 & FLAGS_35)              //3 and 5
                | ZERO_SIGN[(uint8_t)value]         //ZERO and SIGN
                | PARITY[(uint8_t)value]            //PARITY
               ) & flags_chg;                       //Leave only flags to be changed

    REG_F = result;
    return result;

}

inline uint8_t z80core::calc_overflow(uint32_t a, uint32_t b, uint32_t result, uint32_t position)
{
    if (((a ^ b) & (a ^ result) & position) != 0)
        return F_OVERFLOW;
    else
        return 0;
}

inline uint8_t z80core::calc_half_carry(uint8_t v1, uint8_t v2, uint8_t c)
{

    return (LO4(v1) + LO4(v2) + c) & F_HALF_CARRY;
}


inline void z80core::do_ret()
{
    PartsRecLE T;
    T.b.L = read_mem(context.registers.regs.SP);
    T.b.H = read_mem(static_cast<uint16_t>(context.registers.regs.SP+1));
    context.registers.regs.PC = T.w;
    context.registers.regs.SP += 2;
}

inline void z80core::do_jump()
{
    PartsRecLE T;
    T.b.L = next_byte();
    T.b.H = next_byte();
    context.registers.regs.PC = T.w;
}

inline void z80core::do_call()
{
    PartsRecLE T;
    T.b.L = next_byte();
    T.b.H = next_byte();
    context.registers.regs.SP -= 2;
    write_mem(context.registers.regs.SP, context.registers.regs.PC & 0xFF);
    write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), context.registers.regs.PC >> 8);
    context.registers.regs.PC = T.w;
}

inline unsigned int z80core::do_jr(unsigned int command, bool cond)
{
    uint8_t e = next_byte();
    if (cond) {
        REG_PC += (int8_t)e;
        return TIMING[command][1];
    } else {
        return TIMING[command][0];
    }
}

inline uint8_t z80core::do_rlc(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L | (T.b.H & 0x01);
    //REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);
    calc_z80_flags(
                    T.w,                            //Value
                    result,                         //3&5
                    0,                              //Set
                    F_HALF_CARRY+F_SUB,             //Reset
                    F_B5+F_B3+F_CARRY               //To change
                    );


    return result;
}

inline uint8_t z80core::do_rrc(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    //REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.L & 0x01);
    T.w = T.w << 7;
    uint8_t result =  T.b.H | (T.b.L & 0x80);

    calc_z80_flags(
        T.w << 1,                       //Value
        result,                         //3&5
        0,                              //Set
        F_HALF_CARRY+F_SUB,             //Reset
        F_B5+F_B3+F_CARRY               //To change
        );

    return result;
}

inline uint8_t z80core::do_rl(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L | (REG_F & F_CARRY);
    //REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    calc_z80_flags(
                    T.w,                            //Value
                    result,                         //3&5
                    0,                              //Set
                    F_HALF_CARRY+F_SUB,             //Reset
                    F_B5+F_B3+F_CARRY               //To change
                    );

    return result;
}

inline uint8_t z80core::do_rr(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 7;
    uint8_t result  = T.b.H | ((REG_F & F_CARRY)?0x80:0);
    //REG_F = (REG_F & (F_ALL - F_CARRY)) | ((T.b.L >> 7)?F_CARRY:0);

    calc_z80_flags(
        T.w << 1,                       //Value
        result,                         //3&5
        0,                              //Set
        F_HALF_CARRY+F_SUB,             //Reset
        F_B5+F_B3+F_CARRY               //To change
        );

    return result;
}

inline uint8_t z80core::do_sla(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L;
    //REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    calc_z80_flags(
        T.w,                            //Value
        result,                         //3&5
        0,                              //Set
        F_HALF_CARRY+F_SUB,             //Reset
        F_B5+F_B3+F_CARRY               //To change
        );

    return result;
}

inline uint8_t z80core::do_sra(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.L & 0x01);
    T.w = T.w << 7;
    uint8_t result =  T.b.H | ((T.b.H & 0x40) << 1);

    calc_z80_flags(
        T.w << 1,                       //Value
        result,                         //3&5
        0,                              //Set
        F_HALF_CARRY+F_SUB,             //Reset
        F_B5+F_B3+F_CARRY               //To change
        );

    return result;
}

inline uint8_t z80core::do_srl(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.L & 0x01);
    T.w = T.w << 7;
    uint8_t result =  T.b.H;

    calc_z80_flags(
        T.w << 1,                       //Value
        result,                         //3&5
        0,                              //Set
        F_HALF_CARRY+F_SUB,             //Reset
        F_B5+F_B3+F_CARRY               //To change
        );

    return result;
}

inline uint8_t z80core::do_sll(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L | 0x01;
    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    calc_z80_flags(
        T.w,                            //Value
        result,                         //3&5
        0,                              //Set
        F_HALF_CARRY+F_SUB,             //Reset
        F_B5+F_B3+F_CARRY               //To change
        );

    return result;
}

inline uint8_t z80core::do_bit(unsigned int bit, uint8_t v)
{
    uint8_t result = v & (1 << bit);
    calc_z80_flags(
        result,                                         //Value
        result,                                         //For 3&5
        F_HALF_CARRY,                                   //Set HC
        F_SIGN + F_PARITY + F_SUB,                      //Reset N, prepare S&P for below
        F_ZERO+F_B5+F_B3                                //To change
        );
    REG_F |= (result == 0)?F_PARITY:0;                  //PV = Z
    REG_F |= ((bit == 7) && ((v & 0x80) != 0))?F_SIGN:0;
    return result;
}

inline uint8_t z80core::do_res(unsigned int bit, uint8_t v)
{
    uint8_t result = v & ~(1 << bit);
    return result;
}

inline uint8_t z80core::do_set(unsigned int bit, uint8_t v)
{
    uint8_t result = v | (1 << bit);
    return result;
}

inline void z80core::do_DD_FD_CB(unsigned int prefix, unsigned int * cycles)
{
    unsigned int XX, YYY, ZZZ, address;

    PartsRecLE T, D;

    uint8_t d = next_byte();    //+4T
    uint8_t c = next_byte();    //+4T

    address = ((prefix == 0xDD)?REG_IX:REG_IY) + static_cast<int16_t>(d);

    T.b.L = read_mem(address);  //+4T

    XX = c >> 6;
    YYY = (c >> 3) & 0x07;
    ZZZ = c & 0x07;

    switch (XX) {
    case 0:
        switch (YYY) {
        case 0:
            //RLC
            D.b.L = do_rlc(T.b.L);
            break;
        case 1:
            //RRC
            D.b.L = do_rrc(T.b.L);
            break;
        case 2:
            //RL
            D.b.L = do_rl(T.b.L);
            break;
        case 3:
            //RR
            D.b.L = do_rr(T.b.L);
            break;
        case 4:
            //SLA
            D.b.L = do_sla(T.b.L);
            break;
        case 5:
            //SRA
            D.b.L = do_sra(T.b.L);
            break;
        case 6:
            //*SL1
            D.b.L = do_sll(T.b.L);
            break;
        default:
            //SRL
            D.b.L = do_srl(T.b.L);
            break;
        }
        break;
    case 1:
        D.b.L = do_bit(YYY, T.b.L);
        break;
    case 2:
        D.b.L = do_res(YYY, T.b.L);
        break;
    default: //3
        D.b.L = do_set(YYY, T.b.L);
        break;
    }

    if (XX != 1) {
        write_mem(address, D.b.L);  //+3T
        *cycles += 12+3;
    } else {
        *cycles += 12;
    }

    if (ZZZ != 0b110) {
        context.registers.reg_array_8[REGISTERS8[ZZZ]] = D.b.L;
    }
}


void z80core::reset()
{
    context.registers.regs.PC = 0;
    context.halted = false;
    context.int_enable = 0;

    context.global_prefix = 0;
    context.index8_inc = 0;
    context.index16_inc = 0;

    context.IM = 0;
    context.IFF1 = 0;
    context.IFF2 = 0;

    inte_changed(context.int_enable);
}

z80context * z80core::get_context()
{
    return &context;
}

uint8_t z80core::read_port(uint16_t address){
    return 0xFF;
}

void z80core::write_port(uint16_t address, uint8_t value)
{}

void z80core::inte_changed(unsigned int inte)
{}

inline uint32_t z80core::get_first_16()
{
    switch (context.global_prefix) {
        case 0:    return REG_HL; break;
        case 0xDD: return REG_IX; break;
        case 0xFD: return REG_IY; break;
        default:
            qDebug() << "Incorrect prefix value";
            throw QException();
        break;
    }
}

inline uint32_t z80core::get_second_16(uint32_t PP)
{
    switch (PP) {
    case 3: return REG_SP; break;                   //SP
    case 2: return get_first_16(); break;     //HL, IX, IY
    default:
        return context.registers.reg_array_16[PP];
        break;
    }
}

inline void z80core::store_value_16(uint32_t value)
{
    switch (context.global_prefix) {
    case 0:    REG_HL = static_cast<uint16_t>(value); break;
    case 0xDD: REG_IX = static_cast<uint16_t>(value); break;
    case 0xFD: REG_IY = static_cast<uint16_t>(value); break;
    default:
        qDebug() << "Incorrect prefix value";
        throw QException();
        break;
    }
}

inline uint8_t z80core::get_first_8(unsigned int YYY, uint32_t * address, unsigned int * cycles)
{
    if (context.global_prefix == 0) {
        if (YYY == 6)
            return read_mem(context.registers.reg_pairs.HL);
        else
            return context.registers.reg_array_8[REGISTERS8[YYY]];
    } else {
        switch (YYY) {
        case 6:
            *cycles += 4;
            *address = get_first_16() + static_cast<int>(next_byte());
            return read_mem(*address);
            break;
        case 4:
        case 5:
            return context.registers.reg_array_8[REGISTERS8[YYY]+context.index8_inc];
            break;
        default:
            return context.registers.reg_array_8[REGISTERS8[YYY]];
            break;
        }
    }
}

inline void z80core::store_value_8(unsigned int YYY, uint32_t address, uint8_t value, unsigned int * cycles)
{
    if (context.global_prefix == 0) {
        if (YYY == 6)
            write_mem(context.registers.reg_pairs.HL, value);
        else
            context.registers.reg_array_8[REGISTERS8[YYY]] = value;
    } else {
        switch (YYY) {
        case 6:
            *cycles += 4;
            write_mem(address, value);
            break;
        case 4:
        case 5:
            context.registers.reg_array_8[REGISTERS8[YYY]+context.index8_inc] = value;
            break;
        default:
            context.registers.reg_array_8[REGISTERS8[YYY]] = value;
            break;
        }
    }
}

inline uint8_t z80core::do_add8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) + b;
    calc_z80_flags(
                    D.w,                                //Value
                    D.b.L,                              //For 3&5
                    0,                                  //Set none
                    F_HALF_CARRY + F_OVERFLOW + F_SUB,  //Reset HC and V for below, N=0
                    F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
                    );
    REG_F |= calc_half_carry(a, b, 0);
    REG_F |= calc_overflow(a, ~b, D.b.L, 0x80);
    return D.b.L;
}

inline uint8_t z80core::do_adc8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) + b + CARRY;
    calc_z80_flags(
                    D.w,                                //Value
                    D.b.L,                              //For 3&5
                    0,                                  //Set none
                    F_HALF_CARRY + F_OVERFLOW + F_SUB,  //Reset HC and V for below, N=0
                    F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
                    );
    REG_F |= calc_half_carry(a, b, CARRY);
    REG_F |= calc_overflow(a, ~b, D.b.L, 0x80);
    return D.b.L;
}

inline uint8_t z80core::do_sub8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) - b;
    calc_z80_flags(
        D.w,                                //Value
        D.b.L,                              //For 3&5
        F_SUB,                              //Set N
        F_HALF_CARRY + F_OVERFLOW,          //Reset HC and V for below
        F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
        );
    REG_F |= calc_half_carry(a, ~b, 1);
    REG_F |= calc_overflow(a, b, D.b.L, 0x80);
    return D.b.L;
}

inline uint8_t z80core::do_sbc8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) - b - CARRY;
    calc_z80_flags(
        D.w,                                //Value
        D.b.L,                              //For 3&5
        F_SUB,                              //Set N
        F_HALF_CARRY + F_OVERFLOW,          //Reset HC and V for below
        F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
        );
    REG_F |= calc_half_carry(a, ~b, !CARRY);
    REG_F |= calc_overflow(a, b, D.b.L, 0x80);
    return D.b.L;
}

inline uint8_t z80core::do_and8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) & b;
    calc_z80_flags(
        D.w,                                //Value
        D.b.L,                              //For 3&5
        F_HALF_CARRY,                       //Set HC
        F_SUB + F_CARRY,                    //Reset N=0, CY=0
        F_SIGN+F_ZERO+F_B5+F_B3+F_PARITY    //To change
        );
    return D.b.L;
}

inline uint8_t z80core::do_xor8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) ^ b;
    calc_z80_flags(
        D.w,                                //Value
        D.b.L,                              //For 3&5
        0,                                  //Set HC
        F_HALF_CARRY + F_SUB + F_CARRY,     //Reset HC=0, N=0, CY=0
        F_SIGN+F_ZERO+F_B5+F_B3+F_PARITY    //To change
        );
    return D.b.L;
}

inline uint8_t z80core::do_or8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) | b;
    calc_z80_flags(
        D.w,                                //Value
        D.b.L,                              //For 3&5
        0,                                  //Set HC
        F_HALF_CARRY + F_SUB + F_CARRY,     //Reset HC=0, N=0, CY=0
        F_SIGN+F_ZERO+F_B5+F_B3+F_PARITY    //To change
        );
    return D.b.L;
}

inline void z80core::do_cp8(uint8_t a, uint8_t b)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(a) - b;
    calc_z80_flags(
        D.w,                                //Value
        b,                                  //For 3&5 we take an operand
        F_SUB,                              //Set N
        F_HALF_CARRY + F_OVERFLOW,          //Reset HC and V for below
        F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
        );
    REG_F |= calc_half_carry(a, ~b, 1);
    REG_F |= calc_overflow(a, b, D.b.L, 0x80);
}

inline void z80core::do_cpi_cpd(int16_t hlinc)
{
    PartsRecLE T, D;
    T.b.L = read_mem(REG_HL);
    REG_HL += hlinc;
    REG_BC--;
    D.w = static_cast<uint16_t>(REG_A) - T.b.L;
    calc_z80_flags(
        D.w,                                //Value
        0,                                  //3&5 we calc later
        F_SUB,                              //Set N
        F_HALF_CARRY+F_OVERFLOW+F_B3+F_B5,  //Reset HC and V for below
        F_SIGN+F_ZERO                       //To change
        );
    uint8_t HC = calc_half_carry(REG_A, ~T.b.L, 1);
    uint8_t tmp = D.w - HC;

    REG_F |= HC;
    REG_F |= (REG_BC != 0)?F_OVERFLOW:0;

    REG_F |= tmp & F_B3;
    REG_F |= ((tmp & 0x02) != 0)?F_B5:0;
}

inline void z80core::do_ldi_ldd(int16_t hlinc)
{
    PartsRecLE T, D;
    T.b.L = read_mem(REG_HL);
    write_mem(REG_DE, T.b.L);
    REG_HL += hlinc;
    REG_DE += hlinc;
    REG_BC--;
    D.w = static_cast<uint16_t>(REG_A) + T.b.L;
    calc_z80_flags(
                    0,                                        //Value
                    0,                                          //3&5 we calc later
                    0,                                          //Set
                    F_HALF_CARRY+F_OVERFLOW+F_B3+F_B5+F_SUB,    //Reset
                    0                                           //To change
                    );
    REG_F |= (REG_BC != 0)?F_OVERFLOW:0;

    REG_F |= D.b.L & F_B3;
    REG_F |= ((D.b.L & 0x02) != 0)?F_B5:0;
}


inline void z80core::do_ini_ind(int16_t hlinc)
{
    PartsRecLE T, D;
    T.b.L = read_port(REG_BC);
    write_mem(REG_HL, T.b.L);
    REG_HL += hlinc;
    REG_B--;
    calc_z80_flags(
        REG_B,                          //Value
        REG_B,                          //3&5
        0,                              //Set
        0,                              //Reset
        F_SIGN+F_ZERO+F_B5+F_B3         //To change
        );
}

inline void z80core::do_outi_outd(int16_t hlinc)
{
    PartsRecLE T, D;
    T.b.L = read_mem(REG_HL);
    write_port(REG_BC, T.b.L);
    REG_HL += hlinc;
    REG_B--;
    calc_z80_flags(
        REG_B,                          //Value
        REG_B,                          //3&5
        0,                              //Set
        0,                              //Reset
        F_SIGN+F_ZERO+F_B5+F_B3         //To change
        );
}

inline void z80core::do_daa()
{
    PartsRecLE T, D;
    uint32_t increment, result, carry;

    increment = 0;
    result = REG_A;
    carry = REG_F & F_CARRY;

    if (((REG_F & F_HALF_CARRY) != 0) || ((result & 0x0F) > 0x09)) increment |= 0x06;

    if ((carry != 0) || (result > 0x9F)) increment |= 0x60;

    if ((result > 0x8F) && ((result & 0x0F) > 9)) increment |= 0x60;

    if (result > 0x99) carry = F_CARRY;

    if ((REG_F & F_SUB) != 0)
        REG_A = do_sub8(REG_A, increment);
    else
        REG_A = do_add8(REG_A, increment);

    calc_z80_flags(
        (carry << 8) + REG_A,               //Value
        0,                                  //For 3&5
        0,                                  //Set none
        0,                                  //Reset
        F_CARRY+F_PARITY                    //To change
        );
}

unsigned int z80core::execute()
{
    uint8_t command, command2;
    uint16_t port;
    uint32_t tmp_carry;
    unsigned int XX, YYY, ZZZ, PP, Q, XX2, YYY2, ZZZ2, PP2;
    PartsRecLE T, D, T1, T2;
    unsigned int cycles;
    unsigned int address = 0;

    bool process_ints = true;

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
            switch (YYY) {
            case 0:
                //00_000_000
                //NOP
                break;
            case 1:
                //00_001_000
                //EX AF,AF’
                T.w = REG_AF; REG_AF = REG_AF_; REG_AF_ = T.w;
                break;
            case 2:
                //00_010_000
                //DJNZ (PC+$e)
                REG_B--;
                if (REG_B != 0) {
                    T.b.L = next_byte();
                    REG_PC += (int8_t)T.b.L;
                    cycles = TIMING[command][1];
                } else {
                    T.b.L = next_byte();
                    T.b.H = next_byte();
                }
                break;
            case 3:
                //00_011_000
                //JR (PC+$e)
                do_jr(command, true);
                break;
            case 4:
                //00_100_000
                //JR NZ,(PC+$e)
                cycles = do_jr(command, (REG_F & F_ZERO) == 0);
                break;
            case 5:
                //00_101_000
                //JR Z,(PC+$e)
                cycles = do_jr(command, (REG_F & F_ZERO) != 0);
                break;
            case 6:
                //00_110_000
                //JR NC,(PC+$e)
                cycles = do_jr(command, (REG_F & F_CARRY) == 0);
                break;
            case 7:
                //00_111_000
                //JR C,(PC+$e)
                cycles = do_jr(command, (REG_F & F_CARRY) != 0);
                break;
            };
            break;
        case 1:
            //00_YYY_001
            switch (Q) {
            case 0:
                //00_RP0_001
                //LD RP, nn
                T.b.L = next_byte();
                T.b.H = next_byte();
                switch (PP) {
                    case 3: REG_SP = T.w; break;                                //SP
                    case 2: store_value_16(T.w); break;  //HL, IX, IY
                    default:
                        context.registers.reg_array_16[PP] = T.w;               //DC, DE
                        break;
                }
                break;
            default:
                //00_RP1_001
                //ADD HL (IX, IY), RP
                T1.dw = get_first_16();
                T2.dw = get_second_16(PP);
                D.dw = T1.dw + T2.dw;
                store_value_16(D.dw);

                calc_z80_flags(
                                    D.dw >> 8,              //For carry
                                    D.b.H,                  //For 3&5
                                    0,                      //Set none
                                    F_SUB + F_HALF_CARRY,   //Reset SUB, HC for below
                                    F_B5 + F_B3 + F_CARRY   //To change
                    );
                REG_F |= calc_half_carry(                                                           //HC is taken from adding higher bytes
                                            T1.b.H,
                                            T2.b.H,
                                            ((static_cast<uint32_t>(T1.b.L) + T2.b.L) & 0x100) >> 8 //Carry from adding lower bytes
                                        );
                break;
            }
            break;
        case 2:
            //00_YYY_010
            switch (YYY) {
            case 0:
            case 2:
                //00_0R0_010
                //LD [R], A
                write_mem(context.registers.reg_array_16[PP & 0x01], context.registers.regs.A);
                break;
            case 1:
            case 3:
                //00_0R1_010
                //LD A, [R]
                context.registers.regs.A = read_mem(context.registers.reg_array_16[PP & 0x01]);
                break;
            case 4:
                //00_100_010
                //LD [nn], HL
                T.b.L = next_byte();
                T.b.H = next_byte();
                switch (context.global_prefix) {
                    case 0:
                            write_mem(T.w, REG_L);
                            write_mem(static_cast<uint16_t>(T.w+1), REG_H);
                            break;
                    case 0xDD:
                            write_mem(T.w, REG_IXL);
                            write_mem(static_cast<uint16_t>(T.w+1), REG_IXH);
                            break;
                    case 0xFD:
                            write_mem(T.w, REG_IYL);
                            write_mem(static_cast<uint16_t>(T.w+1), REG_IYH);
                            break;
                }
                break;
            case 5:
                //00_101_010
                //LD HL, [nn]
                T.b.L = next_byte();
                T.b.H = next_byte();
                D.b.L = read_mem(T.w);
                D.b.H = read_mem(static_cast<uint16_t>(T.w+1));
                store_value_16(D.w);
                break;
            case 6:
                //00_110_010
                //LD [nn], A
                T.b.L = next_byte();
                T.b.H = next_byte();
                write_mem(T.w, context.registers.regs.A);
                break;
            case 7:
                //00_111_010
                //LD A, [nn]
                T.b.L = next_byte();
                T.b.H = next_byte();
                context.registers.regs.A = read_mem(T.w);
            }
            break;
        case 3:
            //00_YYY_011
            switch (Q) {
            case 0:
                //00_RP0_011
                //INC RP
                switch (PP) {
                    case 3: context.registers.regs.SP++; break;         //SP
                    case 2:                                             //HL, IX, IY
                        D.dw = get_first_16() + 1;
                        store_value_16(D.dw);
                        break;
                    default:
                        context.registers.reg_array_16[PP]++;           //BC, DE
                        break;
                }
                break;
            default:
                //00_RP1_011
                //DEC RP
                switch (PP) {
                    case 3: context.registers.regs.SP--; break;         //SP
                    case 2:                                             //HL, IX, IY
                        D.dw = get_first_16() - 1;
                        store_value_16(D.dw);
                        break;
                    default:
                        context.registers.reg_array_16[PP]--;          //BC, DE
                        break;
                }
            }
            break;
        case 4:
            //00_YYY_100
            //INC SSS
            T.b.L = get_first_8(YYY, &address, &cycles);
            D.dw = static_cast<uint32_t>(T.b.L) + 1;
            store_value_8(YYY, address, D.b.L, &cycles);
            calc_z80_flags(                                     //All are affected except CY
                            D.w,                                //Value
                            D.b.L,                              //For 3&5
                            0,                                  //Set none
                            F_HALF_CARRY + F_OVERFLOW + F_SUB,  //Reset HC and V for below, N=0
                            F_SIGN+F_ZERO+F_B5+F_B3             //To change
                );
            REG_F |= calc_half_carry(T.b.L, 1, 0);
            REG_F |= calc_overflow(T.b.L, ~1, D.b.L, 0x80);
            break;
        case 5:
            //00_YYY_101
            //DEC SSS
            T.b.L = get_first_8(YYY, &address, &cycles);
            D.dw = static_cast<uint32_t>(T.b.L) - 1;
            store_value_8(YYY, address, D.b.L, &cycles);
            calc_z80_flags(                                     //All are affected except CY
                            D.w,                                //Value
                            D.b.L,                              //For 3&5
                            F_SUB,                              //Set N
                            F_HALF_CARRY + F_OVERFLOW,          //Reset HC and V for below
                            F_SIGN+F_ZERO+F_B5+F_B3             //To change
                            );
            REG_F |= calc_half_carry(T.b.L, 0x0F, 0);
            REG_F |= calc_overflow(T.b.L, 1, D.b.L, 0x80);
            break;
        case 6:
            //00_YYY_110
            //LD DDD, d
            get_first_8(YYY, &address, &cycles);        //Just to get an address for IX/IY + d
            T.b.L = next_byte();
            store_value_8(YYY, address, T.b.L, &cycles);
            break;
        default: //7
            //00_YYY_111
            switch (YYY) {
            case 0:
                //00_000_111
                //RLCA
                REG_A = do_rlc(REG_A);
                break;
            case 1:
                //00_001_111
                //RRCA
                REG_A = do_rrc(REG_A);
                break;
            case 2:
                //00_010_111
                //RLA
                REG_A = do_rl(REG_A);
                break;
            case 3:
                //00_011_111
                //RRA
                REG_A = do_rr(REG_A);
                break;
            case 4:
                //00_100_111
                //DAA
                do_daa();
                break;
            case 5:
                //00_101_111
                //CPL
                context.registers.regs.A  = ~context.registers.regs.A;
                break;
            case 6:
                //00_110_111
                //SCF
                context.registers.regs.F |= F_CARRY;
                break;
            default: //7
                //00_111_111
                //CCF
                context.registers.regs.F ^= F_CARRY;
                break;
            }
            break;
        }
        break;
    case 1:
        //01_YYY_ZZZ
        if (command == 0x76)
        {
            //HALT
            context.halted = true;
        } else {
            //LD DDD, SSS
            T.b.L = get_first_8(ZZZ, &address, &cycles);
            store_value_8(YYY, address, T.b.L, &cycles);
        }
        break;
    case 2:
        //10_YYY_ZZZ

        T.w = get_first_8(ZZZ, &address, &cycles);

        switch (YYY) {
        case 0:
            //ADD A, ZZZ
            REG_A = do_add8(REG_A, T.b.L);
            break;
        case 1:
            //ADC A, ZZZ
            REG_A = do_adc8(REG_A, T.b.L);
            break;
        case 2:
            //SUB A, ZZZ
            REG_A = do_sub8(REG_A, T.b.L);
            break;
        case 3:
            //SBC A, ZZZ
            REG_A = do_sbc8(REG_A, T.b.L);
            break;
        case 4:
            //AND A, ZZZ
            REG_A = do_and8(REG_A, T.b.L);
            break;
        case 5:
            //XOR A, ZZZ
            REG_A = do_xor8(REG_A, T.b.L);
            break;
        case 6:
            //OR A, ZZZ
            REG_A = do_or8(REG_A, T.b.L);
            break;
        default: //7
            //CP ZZZ
            do_cp8(REG_A, T.b.L);
            break;
        }
        break;
    default: //3
        //11_YYY_ZZZ
        switch (ZZZ) {
        case 0:
            //11_YYY_000
            //RET IF YYY
            if ( ((context.registers.regs.F & CONDITIONS[YYY][0]) ^ CONDITIONS[YYY][1]) == 0 )
            {
                do_ret();
                cycles = TIMING[command][1];
            }
            break;
        case 1:
            //11_YYY_001
            switch (Q) {
            case 0:
                //POP RP
                T.b.L = read_mem(context.registers.regs.SP);
                T.b.H = read_mem(static_cast<uint16_t>(context.registers.regs.SP+1));
                switch (PP) {
                    case 3:                                          //PSW
                        REG_F = T.b.L;
                        REG_A = T.b.H;
                        break;
                    case 2:                                          //HL, IX, IY
                        store_value_16(T.w);
                        break;
                    default:
                        context.registers.reg_array_16[PP] = T.w;    //BC, DE
                        break;
                }
                REG_SP += 2;
                break;
            default: //1
                switch (PP) {
                case 0:
                    //11_001_001
                    //RET
                    do_ret();
                    break;
                case 1:
                    //11_011_001
                    //EXX
                    T.w = REG_BC; REG_BC = REG_BC_; REG_BC_ = T.w;
                    T.w = REG_DE; REG_DE = REG_DE_; REG_DE_ = T.w;
                    T.w = REG_HL; REG_HL = REG_BC_; REG_HL_ = T.w;
                    break;
                case 2:
                    //11_101_001
                    //JP [HL]
                    context.registers.regs.PC = get_first_16();
                    break;
                default: //3
                    //11_111_001
                    //LD SP,HL
                    context.registers.regs.SP = get_first_16();
                    break;
                }
                break;
            }
            break;
        case 2:
            //11_YYY_010
            //JUMP IF YYY
            if ( ((context.registers.regs.F & CONDITIONS[YYY][0]) ^ CONDITIONS[YYY][1]) == 0 )
            {
                do_jump();
                cycles = TIMING[command][1];
            } else {
                T.b.L = next_byte();
                T.b.H = next_byte();
            }
            break;
        case 3:
            switch (YYY) {
            case 0:
                //11_000_011
                //JP ADDR16
                do_jump();
                break;
            case 1:
                //11_001_011
                //Prefix CB
                command2 = next_byte(); cycles += 4;
                //XX YYY ZZZ
                XX2 = command2 >> 6;
                YYY2 = (command2 >> 3) & 0x07;
                ZZZ2 = command2 & 0x07;

                if (ZZZ2 == 6) {
                    T.b.L = read_mem(REG_HL); cycles += 4;
                } else
                    T.b.L = context.registers.reg_array_8[REGISTERS8[ZZZ2]];

                switch (XX2) {
                case 0:
                    // CB 00_rot_SSS
                    switch (YYY2) {
                    case 0:
                        //RLC
                        D.b.L = do_rlc(T.b.L);
                        break;
                    case 1:
                        //RRC
                        D.b.L = do_rrc(T.b.L);
                        break;
                    case 2:
                        //RL
                        D.b.L = do_rl(T.b.L);
                        break;
                    case 3:
                        //RR
                        D.b.L = do_rr(T.b.L);
                        break;
                    case 4:
                        //SLA
                        D.b.L = do_sla(T.b.L);
                        break;
                    case 5:
                        //SRA
                        D.b.L = do_sra(T.b.L);
                        break;
                    case 6:
                        //*SL1
                        D.b.L = do_sll(T.b.L);
                        break;
                    case 7:
                        //SRL
                        D.b.L = do_srl(T.b.L);
                        break;
                    default:
                        D.b.L = 0;  //Never happens, just to avoid warnings
                        break;
                    }
                    break;
                case 1:
                    // CB 01_bit_SSS
                    //BIT bit, SSS
                    D.b.L = do_bit(YYY2, T.b.L);    //Should not be stored back
                    break;
                case 2:
                    // CB 10 bit SSS
                    //RES bit, SSS
                    D.b.L = do_res(YYY2, T.b.L);
                    break;
                case 3:
                    // CB 11 bit SSS
                    //SET bit, SSS
                    D.b.L = do_set(YYY2, T.b.L);
                    break;
                default:
                    D.b.L = 0;      //Never happens, just to avoid warnings
                    break;
                }

                if (XX2 != 1)
                {
                    if (ZZZ2 == 6) {
                        write_mem(REG_HL, D.b.L); cycles += 3;
                    } else
                        context.registers.reg_array_8[REGISTERS8[ZZZ2]] = D.b.L;
                }

                break;
            case 2:
                //11_010_011
                //OUT (d),A
                port = next_byte();
                write_port(port + (port << 8), context.registers.regs.A);
                break;
            case 3:
                //11_011_011
                //IN A, (d)
                port = next_byte();
                context.registers.regs.A = read_port(port + (port << 8));
                break;
            case 4:
                //11_100_011
                //EX (SP),HL
                T.b.L = read_mem(context.registers.regs.SP);
                T.b.H = read_mem(static_cast<uint16_t>(context.registers.regs.SP+1));
                D.w = get_first_16();
                write_mem(context.registers.regs.SP, D.b.L);
                write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), D.b.H);
                store_value_16(T.w);
                break;
            case 5:
                //11_101_011
                //EX DE,HL
                T.w = context.registers.reg_pairs.HL;
                context.registers.reg_pairs.HL = context.registers.reg_pairs.DE;
                context.registers.reg_pairs.DE = T.w;
                break;
            case 6:
                //11_110_011
                //DI
                context.IFF1 = 0;
                context.IFF2 = 0;

                //TODO: remove old code
                context.int_enable = 0;
                inte_changed(context.int_enable);
                break;
            default: //7
                //11_111_011
                //EI
                context.IFF1 = 1;
                context.IFF2 = 1;
                process_ints = false;

                //TODO: remove old code
                context.int_enable = 1;
                inte_changed(context.int_enable);
                break;
            }
            break;
        case 4:
            //11_YYY_100
            //CALL IF YYY
            if ( ((context.registers.regs.F & CONDITIONS[YYY][0]) ^ CONDITIONS[YYY][1]) == 0 )
            {
                do_call();
                cycles = TIMING[command][1];
            } else {
                T.b.L = next_byte();
                T.b.H = next_byte();
            }
            break;
        case 5:
            //11_YYY_101
            switch (Q) {
            case 0:
                //11_RP0_101
                //PUSH RP
                switch (PP) {
                case 3: T.w = REG_AF; break;                    //PSW
                case 2: T.w = get_first_16(); break;            //HL, IX, IY
                default:
                    T.w = context.registers.reg_array_16[PP];   //BC, DE
                    break;
                }
                context.registers.regs.SP -= 2;
                write_mem(context.registers.regs.SP, T.b.L);
                write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), T.b.H);
                break;
            default: //1
                //11_PP1_101
                switch (PP) {
                case 0:
                    //11_001_101
                    //CALL ADDR16
                    do_call();
                    break;
                case 2:
                    //11_101_101
                    //Prefix ED
                    command2 = next_byte(); cycles += 4;
                    //XX YYY ZZZ
                    XX2 = command2 >> 6;
                    YYY2 = (command2 >> 3) & 0x07;
                    ZZZ2 = command2 & 0x07;
                    switch (XX2) {
                    case 0:
                        //ED 00 YYY ZZZ
                        //*NOP/NONI
                        break;
                    case 1:
                        //ED 01 YYY ZZZ
                        switch (ZZZ2) {
                        case 0:
                            // ED 01 YYY 000
                            // IN YYY, [C]
                            D.b.L = read_port(REG_BC); cycles += 4;
                            if (YYY2 != 0b110)                      //110: only flags are set (undoc)
                                context.registers.reg_array_8[REGISTERS8[YYY2]] = D.b.L;
                            calc_z80_flags(
                                D.b.L,                               //Value
                                D.b.L,                               //For 3&5
                                0,                                   //Set none
                                F_HALF_CARRY + F_SUB,                //Reset HC & N
                                F_SIGN+F_ZERO+F_B5+F_B3+F_PARITY     //To change
                                );
                            break;
                        case 1:
                            // ED 01 YYY 001
                            if (YYY2 == 0b110) {
                                // ED 01 110 001
                                // *OUT [C], 0
                                write_port(REG_BC, 0);  cycles += 4;
                            } else {
                                // ED 01 YYY 001
                                // OUT [C], YYY
                                write_port(REG_BC, context.registers.reg_array_8[REGISTERS8[YYY2]]);  cycles += 4;
                            }
                            break;
                        case 2:
                            // ED 01 YYY 010
                            PP2 = (YYY2 >> 1) & 0x03;
                            if ((YYY2 & 0x01)==0) {
                                // ED 01 RP0 010
                                // SBC HL, RP
                                T1.dw = REG_HL;
                                T2.dw = (PP2==3)?REG_SP:context.registers.reg_array_16[PP2];
                                tmp_carry = CARRY;
                                D.dw = T1.dw - T2.dw - CARRY;
                                REG_HL = D.w;

                                calc_z80_flags(
                                    D.dw >> 8,                              //For carry and sign
                                    D.b.H,                                  //For 3&5
                                    0,                                      //Set none
                                    F_ZERO+F_SUB+F_HALF_CARRY+F_OVERFLOW ,  //Reset SUB; Z, HC & V for below
                                    F_SIGN+F_B5+F_B3+F_CARRY                //To change
                                    );
                                REG_F |= (D.w == 0)?F_ZERO:0;
                                REG_F |= calc_half_carry(                   //HC is taken from substracting higher bytes
                                    T1.b.H,
                                    ~T2.b.H,
                                    !(((static_cast<uint32_t>(T1.b.L) - T2.b.L - tmp_carry) & 0x100) >> 8) //Carry from substracting lower bytes
                                    );
                                REG_F |= calc_overflow(T1.w, T2.w, D.w, 0x8000);
                                cycles += 7;
                            } else {
                                // ED 01 RP1 010
                                // ADC HL, RP
                                T1.dw = REG_HL;
                                T2.dw = (PP2==3)?REG_SP:context.registers.reg_array_16[PP2];
                                tmp_carry = CARRY;
                                D.dw = T1.dw + T2.dw + CARRY;
                                REG_HL = D.w;

                                calc_z80_flags(
                                    D.dw >> 8,                                  //For carry  and sign
                                    D.b.H,                                      //For 3&5
                                    0,                                          //Set none
                                    F_ZERO+F_SUB + F_HALF_CARRY + F_OVERFLOW,   //Reset SUB; Z, HC & V for below
                                    F_SIGN+F_B5+F_B3+F_CARRY                    //To change
                                    );
                                REG_F |= (D.w == 0)?F_ZERO:0;
                                REG_F |= calc_half_carry(                       //HC is taken from adding higher bytes
                                    T1.b.H,
                                    T2.b.H,
                                    ((static_cast<uint32_t>(T1.b.L) + T2.b.L + tmp_carry) & 0x100) >> 8 //Carry from adding lower bytes
                                    );
                                REG_F |= calc_overflow(T1.w, ~T2.w, D.w, 0x8000);
                                cycles += 7;
                            }
                            break;
                        case 3:
                            // ED 01 YYY 011
                            PP2 = (YYY2 >> 1) & 0x03;
                            if ((YYY2 & 0x01)==0) {
                                // ED 01 RP0 011
                                // LD [ADDR16], RP
                                T.b.L = next_byte();
                                T.b.H = next_byte();
                                if (PP2 == 3) {
                                    write_mem(T.w, LO8(REG_SP));
                                    write_mem(T.w + 1, HI8(REG_SP));
                                } else {
                                    write_mem(T.w, LO8(context.registers.reg_array_16[PP2]));
                                    write_mem(T.w + 1, HI8(context.registers.reg_array_16[PP2]));
                                }
                                cycles += 12;
                            } else {
                                // ED 01 RP1 011
                                // LD RP, [ADDR16]
                                T.b.L = next_byte();
                                T.b.H = next_byte();
                                D.b.L = read_mem(T.w);
                                if (PP2 == 3)
                                    REG_SP = D.w;
                                else
                                    context.registers.reg_array_16[PP2] = D.w;
                                cycles += 12;
                            }
                            break;
                        case 4:
                            // ED 01 YYY 100
                            // NEG/*NEG
                            REG_A = do_sub8(0, REG_A);
                            break;
                        case 5:
                            // ED 01 YYY 101
                            // RETI/*RETN
                            context.IFF1 = context.IFF2;
                            do_ret();
                            cycles += 6;
                            break;
                        case 6:
                            // ED 01 YYY 110
                            switch (YYY2) {
                            case 0:
                            case 4:
                                // ED 01 X00 110
                                // IM 0/*IM 0
                                context.IM = 0;
                                break;
                            case 1:
                            case 5:
                                // ED 01 X01 110
                                // *IM 0
                                context.IM = 0;
                                break;
                            case 2:
                            case 6:
                                // ED 01 X10 110
                                // IM 1/*IM 1
                                context.IM = 1;
                                break;
                            case 3:
                            case 7:
                                // ED 01 X11 110
                                // IM 2/*IM 2
                                context.IM = 2;
                                break;
                            }
                            break;
                        case 7:
                            // ED 01 YYY 111
                            switch (YYY2) {
                            case 0:
                                // ED 01 000 111
                                // LD I, A
                                REG_I = REG_A;
                                cycles += 1;
                                break;
                            case 1:
                                // ED 01 001 111
                                // LD R, A
                                REG_R = REG_A;
                                cycles += 1;
                                break;
                            case 2:
                                // ED 01 010 111
                                // LD A, I
                                REG_A = REG_I;
                                if (context.IFF2 == 1)
                                    REG_F |= F_PARITY;
                                else
                                    REG_F &= ~F_PARITY;
                                cycles += 1;
                                break;
                            case 3:
                                // ED 01 011 111
                                // LD A, R
                                //TODO: LD A, R - check which R should be taken
                                REG_A = REG_R;
                                if (context.IFF2 == 1)
                                    REG_F |= F_PARITY;
                                else
                                    REG_F &= ~F_PARITY;
                                cycles += 1;
                                break;
                            case 4:
                                // ED 01 100 111
                                // RRD
                                T.b.L = read_mem(REG_HL);
                                D.b.L = (LO4(REG_A) << 4) | HI4(T.b.L);
                                REG_A = (REG_A & 0xF0) | LO4(T.b.L);
                                write_mem(REG_HL, D.b.L);
                                calc_z80_flags(
                                    REG_A,                              //Value
                                    REG_A,                              //For 3&5
                                    0,                                  //Set none
                                    F_SUB+F_HALF_CARRY,                 //Reset SUB & HC
                                    F_SIGN+F_ZERO+F_B5+F_B3+F_PARITY    //To change
                                    );
                                cycles += 10;
                                break;
                            case 5:
                                // ED 01 101 111
                                // RLD
                                T.b.L = read_mem(REG_HL);
                                D.b.L = (LO4(T.b.L) << 4) | LO4(REG_A);
                                REG_A = (REG_A & 0xF0) | HI4(T.b.L);
                                write_mem(REG_HL, D.b.L);
                                calc_z80_flags(
                                    REG_A,                              //Value
                                    REG_A,                              //For 3&5
                                    0,                                  //Set none
                                    F_SUB+F_HALF_CARRY,                 //Reset SUB & HC
                                    F_SIGN+F_ZERO+F_B5+F_B3+F_PARITY    //To change
                                    );
                                cycles += 10;
                                break;
                            case 6:
                            case 7:
                                // ED 01 11X 111
                                // *NOP
                                break;
                            }
                            break;
                        }
                        break;
                    case 2:
                        //ED 10 YYY ZZZ
                        switch (YYY2) {
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                            // ED 10 0YY ZZZ
                            // *NOP
                            break;
                        case 4:
                            switch (ZZZ2) {
                            case 0:
                                // ED 10 100 000
                                // LDI
                                do_ldi_ldd(1);
                                cycles += 8;
                                break;
                            case 1:
                                // ED 10 100 001
                                // CPI
                                do_cpi_cpd(1);
                                cycles += 8;
                                break;
                            case 2:
                                // ED 10 100 010
                                // INI
                                do_ini_ind(1);
                                cycles += 8;
                                break;
                            case 3:
                                // ED 10 100 011
                                // OUTI
                                do_outi_outd(1);
                                cycles += 8;
                                break;
                            case 4:
                            case 5:
                            case 6:
                            case 7:
                                // ED 10 100 1ZZ
                                // *NOP
                                break;
                            }
                            break;
                        case 5:
                            // ED 10 101 ZZZ
                            switch (ZZZ2) {
                            case 0:
                                // ED 10 101 000
                                // LDD
                                do_ldi_ldd(-1);
                                cycles += 8;
                                break;
                            case 1:
                                // ED 10 101 001
                                // CPD
                                do_cpi_cpd(-1);
                                cycles += 8;
                                break;
                            case 2:
                                // ED 10 101 010
                                // IND
                                do_ini_ind(-1);
                                cycles += 8;
                                break;
                            case 3:
                                // ED 10 101 011
                                // OUTD
                                do_outi_outd(-1);
                                cycles += 8;
                                break;
                            case 4:
                            case 5:
                            case 6:
                            case 7:
                                // ED 10 101 1ZZ
                                // *NOP
                                break;
                            }
                            break;
                        case 6:
                            // ED 10 110 ZZZ
                            switch (ZZZ2) {
                            case 0:
                                // ED 10 110 000
                                // LDIR
                                do_ldi_ldd(1);
                                if (REG_BC == 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 1:
                                // ED 10 110 001
                                // CPIR
                                do_cpi_cpd(1);
                                if ((REG_F & F_ZERO) != 0 || REG_BC == 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 2:
                                // ED 10 110 010
                                // INIR
                                do_ini_ind(1);
                                if ((REG_F & F_ZERO) != 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 3:
                                // ED 10 110 011
                                // OTIR
                                do_outi_outd(1);
                                if ((REG_F & F_ZERO) != 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 4:
                            case 5:
                            case 6:
                            case 7:
                                // ED 10 110 1ZZ
                                // *NOP
                                break;
                            }
                            break;
                        case 7:
                            // ED 10 111 ZZZ
                            switch (ZZZ2) {
                            case 0:
                                // ED 10 111 000
                                // LDDR
                                do_ldi_ldd(-1);
                                if (REG_BC == 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 1:
                                // ED 10 111 001
                                // CPDR
                                do_cpi_cpd(-1);
                                if ((REG_F & F_ZERO) != 0 || REG_BC == 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 2:
                                // ED 10 111 010
                                // INDR
                                do_ini_ind(-1);
                                if ((REG_F & F_ZERO) != 0 || REG_BC == 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 3:
                                // ED 10 111 011
                                // OTDR
                                do_outi_outd(-1);
                                if ((REG_F & F_ZERO) != 0) {
                                    cycles = 16;
                                } else {
                                    cycles = 21;
                                    REG_PC -= 2;
                                }
                                break;
                            case 4:
                            case 5:
                            case 6:
                            case 7:
                                // ED 10 111 1ZZ
                                // *NOP
                                break;
                            }
                            break;
                        }
                        break;
                    case 3:
                        //ED 11 YYY ZZZ
                        // *NOP
                        break;
                    }
                    break;
                case 1: //11_011_101 (DD)
                case 3: //11_111_101 (FD)
                    //Prefix DD/FD
                    process_ints = false;
                    command2 = next_byte();
                    switch (command2) {
                    case 0xDD:
                    case 0xED:
                    case 0xFD:
                        // Prefixes chain
                        //NONI
                        REG_PC -= 1;
                        break;
                    case 0xCB:
                        // DD/FD CB commands
                        cycles += 4;
                        do_DD_FD_CB(command, &cycles);
                        command = command2;     // For correct prefix arter-processing
                        process_ints = true;
                        break;
                    default:
                        REG_PC -= 1;
                        context.global_prefix = command;
                        if (command == 0xDD) {
                            context.index8_inc = 4;     //L, H -> IXL, IXH
                            context.index16_inc = 2;    //HL -> IX
                        } else {
                            context.index8_inc = 6;     //L, H -> IYL, IYH
                            context.index16_inc = 3;    //HL -> IY
                        }
                    }
                    break;
                }
                break;
            }
            break;
        case 6:
            //11_YYY_110
            T.w = next_byte();
            switch (YYY) {
            case 0:
                //ADD A, d
                REG_A = do_add8(REG_A, T.b.L);
                break;
            case 1:
                //ADC A, d
                REG_A = do_adc8(REG_A, T.b.L);
                break;
            case 2:
                //SUB A, d
                REG_A = do_sub8(REG_A, T.b.L);
                break;
            case 3:
                //SBC A, d
                REG_A = do_sbc8(REG_A, T.b.L);
                break;
            case 4:
                //AND A, d
                REG_A = do_and8(REG_A, T.b.L);
                break;
            case 5:
                //XOR A, d
                REG_A = do_xor8(REG_A, T.b.L);
                break;
            case 6:
                //OR A, d
                REG_A = do_or8(REG_A, T.b.L);
                break;
            default: //7
                //CP A, d
                do_cp8(REG_A, T.b.L);
                break;
            }
            break;
        default: //7
            //11_YYY_111
            //RST
            T.w = context.registers.regs.PC;
            context.registers.regs.SP -= 2;
            write_mem(context.registers.regs.SP, T.b.L);
            write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), T.b.H);
            context.registers.regs.PC = YYY << 3;
            break;
        }
        break;
    }

    if ((command != 0xDD) && (command != 0xFD) && (context.global_prefix !=0))
    {
        // Reset prefix after a command excluding prefix chains
        context.global_prefix = 0;
        context.index8_inc = 0;
        context.index16_inc = 0;
    }

    if (process_ints) {
        //TODO: process ints
    };

    return cycles;

}
