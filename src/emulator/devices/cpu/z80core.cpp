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

//TODO: Z80 timings
static const uint8_t TIMING[256][2] = {
    {4, 4},   {10, 10}, {7, 7},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},
    {4, 4},   {10, 10}, {7, 7},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},        // 00-0F
    {4, 4},   {10, 10}, {7, 7},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},
    {4, 4},   {10, 10}, {7, 7},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},        // 10-1F
    {4, 4},   {10, 19}, {16, 16}, {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},
    {4, 4},   {10, 10}, {16, 16}, {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},        // 20-2F
    {4, 4},   {10, 10}, {13, 13}, {5, 5},   {10, 10}, {10, 10}, {10, 10}, {4, 4},
    {4, 4},   {10, 10}, {13, 13}, {5, 5},   {5, 5},   {5, 5},   {7, 7},   {4, 4},        // 30-3F
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},        // 40-4F
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},        // 50-5F
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},        // 60-6F
    {7, 7},   {7, 7},   {7, 7},   {7, 7},   {7, 7},   {7, 7},   {4, 4},   {7, 7},
    {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {5, 5},   {7, 7},   {5, 5},        // 70-7F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 80-8F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // 90-9F
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // A0-AF
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},
    {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {4, 4},   {7, 7},   {4, 4},        // B0-BF
    {5, 11},  {11, 11}, {10, 10}, {10, 10}, {11, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {10, 10}, {10, 10}, {10, 10}, {11, 17}, {17, 17}, {7, 7},   {11, 11},      // C0-CF
    {5, 11},  {11, 11}, {10, 10}, {10, 10}, {11, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {10, 10}, {10, 10}, {10, 10}, {11, 17}, {17, 17}, {7, 7},   {11, 11},      // D0-DF
    {5, 11},  {11, 11}, {10, 10}, {18, 18}, {11, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {5, 5},   {10, 10}, {4, 4},   {11, 17}, {17, 17}, {7, 7},   {11, 11},      // E0-EF
    {5, 11},  {11, 11}, {10, 10}, {4, 4},   {11, 17}, {11, 11}, {7, 7},   {11, 11},
    {5, 11},  {5, 5},   {10, 10}, {4, 4},   {11, 17}, {17, 17}, {7, 7},   {11, 11}};  	 // F0-FF

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
    //TODO: 8080 core constructor
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

inline uint8_t z80core::calc_base_flags(uint32_t value)
{

    return F_BASE_8080
           | ( (value >> 8) & F_CARRY )
           | ZERO_SIGN[(uint8_t)value]
           | PARITY[(uint8_t)value];
}

inline uint8_t z80core::calc_z80_flags(
                        uint32_t value,         //Value for standard flags
                        uint32_t value35,       //Value for flags 3 and 5
                        uint32_t flags_set,     //Flags to be set
                        uint32_t flags_reset,   //Flags to be reset
                        uint32_t flags_chg      //Flags to be calculated
                    )
{
    uint32_t result;
    uint32_t mask_reset = ~(flags_reset + flags_chg);

    result = (REG_F & mask_reset) | flags_set;      //Set and reset selected flags, reset flags to be changed
    result |= ( ( (value >> 8) & F_CARRY )              //CARRY
                | (value35 & FLAGS_35)                  //3 and 5
                | ZERO_SIGN[(uint8_t)value]             //ZERO and SIGN
                | PARITY[(uint8_t)value]                //PARITY
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
    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    return result;
}

inline uint8_t z80core::do_rrc(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.L & 0x01);
    T.w = T.w << 7;
    uint8_t result =  T.b.H | (T.b.L & 0x80);

    return result;
}

inline uint8_t z80core::do_rl(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L | (REG_F & F_CARRY);
    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    return result;
}

inline uint8_t z80core::do_rr(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 7;
    uint8_t result  = T.b.H | ((REG_F & F_CARRY)?0x80:0);
    REG_F = (REG_F & (F_ALL - F_CARRY)) | ((T.b.L >> 7)?F_CARRY:0);

    return result;
}

inline uint8_t z80core::do_sla(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L;
    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    return result;
}

inline uint8_t z80core::do_sra(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.L & 0x01);
    T.w = T.w << 7;
    uint8_t result =  T.b.H | ((T.b.H & 0x40) << 1);

    return result;
}

inline uint8_t z80core::do_srl(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.L & 0x01);
    T.w = T.w << 7;
    uint8_t result =  T.b.H;

    return result;
}

inline uint8_t z80core::do_sll(uint8_t v)
{
    PartsRecLE T;
    T.w = v;

    T.w = T.w << 1;
    uint8_t result = T.b.L | 0x01;
    REG_F = (REG_F & (F_ALL - F_CARRY)) | (T.b.H & 0x01);

    return result;
}

inline uint8_t z80core::do_bit(unsigned int bit, uint8_t v)
{
    uint8_t result = v & (1 << bit);
    //TODO: BIT set flags
    return result;
}

inline uint8_t z80core::do_res(unsigned int bit, uint8_t v)
{
    uint8_t result = v & ~(1 << bit);
    //TODO: BIT reset
    return result;
}

inline uint8_t z80core::do_set(unsigned int bit, uint8_t v)
{
    uint8_t result = v | (1 << bit);
    //TODO: BIT set
    return result;
}

inline uint8_t z80core::do_DD_FD_CB(unsigned int prefix)
{
    unsigned int cycles = 0;
    // TODO: do_DD_FD_CB
    return cycles;
}


void z80core::reset()
{
    context.registers.regs.PC = 0;
    context.halted = false;
    context.int_enable = 0;

    context.global_prefix = 0;
    context.index8_inc = 0;
    context.index16_inc = 0;

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

unsigned int z80core::execute()
{
    uint8_t command, command2;
    uint16_t port;
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
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 1;
                context.registers.regs.A = T.b.L | (T.b.H & 0x01);
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((T.b.H & 0x01)?F_CARRY:0);
                break;
            case 1:
                //00_001_111
                //RRCA
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((context.registers.regs.A & 0x01)?F_CARRY:0);
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 7;
                context.registers.regs.A = T.b.H | (T.b.L & 0x80);
                break;
            case 2:
                //00_010_111
                //RLA
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 1;
                context.registers.regs.A = T.b.L | ((context.registers.regs.F & F_CARRY)?1:0);
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((T.b.H & 0x01)?F_CARRY:0);
                break;
            case 3:
                //00_011_111
                //RRA
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 7;
                context.registers.regs.A = T.b.H | ((context.registers.regs.F & F_CARRY)?0x80:0);
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((T.b.L >> 7)?F_CARRY:0);
                break;
            case 4:
                //00_100_111
                //DAA
                //TODO: DAA
                T.w = static_cast<uint16_t>(context.registers.regs.A);
                if ((LO4(T.b.L) > 9) || (HALF_CARRY != 0))
                {
                    context.registers.regs.A += 0x06;
                    context.registers.regs.F &= ~F_HALF_CARRY;
                    context.registers.regs.F |= calc_half_carry(T.b.L, 0x06, 0);
                }
                if ( (CARRY != 0) || (HI4(T.b.L) > 9) || ( (HI4(T.b.L) == 9) && (LO4(T.b.L) > 9) ) )
                {
                    context.registers.regs.A += 0x60;
                    context.registers.regs.F |= F_CARRY;
                }
                context.registers.regs.F = calc_base_flags(static_cast<uint16_t>(context.registers.regs.A) + (CARRY << 8)) | (context.registers.regs.F & F_HALF_CARRY); //Keep CY & HC
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
            D.w = static_cast<uint16_t>(REG_A) + T.w;
            //context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, 0);
            calc_z80_flags(
                            D.w,                                //Value
                            D.b.L,                              //For 3&5
                            0,                                  //Set none
                            F_HALF_CARRY + F_OVERFLOW + F_SUB,  //Reset HC and V for below, N=0
                            F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
                            );
            REG_F |= calc_half_carry(REG_A, T.b.L, 0);
            REG_F |= calc_overflow(REG_A, ~T.b.L, D.b.L, 0x80);
            REG_A = D.b.L;
            break;
        case 1:
            //ADC A, ZZZ
            D.w = static_cast<uint16_t>(REG_A) + T.w + CARRY;
            calc_z80_flags(
                            D.w,                                //Value
                            D.b.L,                              //For 3&5
                            0,                                  //Set none
                            F_HALF_CARRY + F_OVERFLOW + F_SUB,  //Reset HC and V for below, N=0
                            F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
                            );
            REG_F |= calc_half_carry(REG_A, T.b.L, CARRY);
            REG_F |= calc_overflow(REG_A, ~T.b.L, D.b.L, 0x80);
            REG_A = D.b.L;
            break;
        case 2:
            //SUB A, ZZZ
            D.w = static_cast<uint16_t>(REG_A) - T.w;
            calc_z80_flags(
                            D.w,                                //Value
                            D.b.L,                              //For 3&5
                            F_SUB,                              //Set N
                            F_HALF_CARRY + F_OVERFLOW,          //Reset HC and V for below
                            F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
                        );
            REG_F |= calc_half_carry(REG_A, ~(T.b.L), 1);
            REG_F |= calc_overflow(REG_A, T.b.L, D.b.L, 0x80);
            REG_A = D.b.L;
            break;
        case 3:
            //SBC A, ZZZ
            D.w = static_cast<uint16_t>(REG_A) - T.w - CARRY;
            calc_z80_flags(
                            D.w,                                //Value
                            D.b.L,                              //For 3&5
                            F_SUB,                              //Set N
                            F_HALF_CARRY + F_OVERFLOW,          //Reset HC and V for below
                            F_SIGN+F_ZERO+F_B5+F_B3+F_CARRY     //To change
                            );
            REG_F |= calc_half_carry(REG_A, ~(T.b.L), !CARRY);
            REG_F |= calc_overflow(REG_A, T.b.L, D.b.L, 0x80);
            REG_A = D.b.L;
            break;
        case 4:
            //AND A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) & T.w;
            context.registers.regs.F = calc_base_flags(D.b.L) | ((((context.registers.regs.A | T.b.L) & 0x08) != 0 )?F_HALF_CARRY:0); //CY=0!
            context.registers.regs.A = D.b.L;
            break;
        case 5:
            //XOR A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) ^ T.w;
            context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
            context.registers.regs.A = D.b.L;
            break;
        case 6:
            //OR A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) | T.w;
            context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
            context.registers.regs.A = D.b.L;
            break;
        default: //7
            //CP ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), 1);
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
                command2 = next_byte();
                //XX YYY ZZZ
                XX2 = command2 >> 6;
                YYY2 = (command2 >> 3) & 0x07;
                ZZZ2 = command2 & 0x07;

                if (ZZZ2 == 6)
                    T.b.L = read_mem(REG_HL);
                else
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
                    if (ZZZ2 == 6)
                        write_mem(REG_HL, D.b.L);
                    else
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
                //TODO: DI & EI
                context.int_enable = 0;
                inte_changed(context.int_enable);
                break;
            default: //7
                //11_111_011
                //EI
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
                    command2 = next_byte();
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
                            if (YYY2 == 0b110) {
                                // ED 01 110 000
                                // *IN F, [C]
                                // TODO: *IN F, [C]
                            } else {
                                // ED 01 YYY 000
                                // IN YYY, [C]
                                // TODO: IN YYY, [C]
                            }
                            break;
                        case 1:
                            // ED 01 YYY 001
                            if (YYY2 == 0b110) {
                                // ED 01 110 001
                                // *OUT [C], 0
                                // TODO: *OUT [C], 0
                            } else {
                                // ED 01 YYY 001
                                // OUT [C], YYY
                                // TODO: OUT [C], YYY
                            }
                            break;
                        case 2:
                            // ED 01 YYY 010
                            PP2 = (YYY2 >> 1) & 0x03;
                            if ((YYY2 & 0x01)==0) {
                                // ED 01 RP0 010
                                // SBC HL, RP
                                // TODO: SBC HL, RP
                            } else {
                                // ED 01 RP1 010
                                // ADC HL, RP
                                // TODO: ADC HL, RP
                            }
                            break;
                        case 3:
                            // ED 01 YYY 011
                            PP2 = (YYY2 >> 1) & 0x03;
                            if ((YYY2 & 0x01)==0) {
                                // ED 01 RP0 011
                                // LD [ADDR16], RP
                                // TODO: LD [ADDR16], RP
                            } else {
                                // ED 01 RP1 011
                                // LD RP, [ADDR16]
                                // TODO: LD RP, [ADDR16]
                            }
                            break;
                        case 4:
                            // ED 01 YYY 100
                            // NEG/*NEG
                            //TODO: NEG
                            break;
                        case 5:
                            // ED 01 YYY 101
                            // RETI/*RETN
                            // TODO: RETI
                            break;
                        case 6:
                            // ED 01 YYY 110
                            switch (YYY2) {
                            case 0:
                            case 4:
                                // ED 01 X00 110
                                // IM 0/*IM 0
                                // TODO: IM 0
                                break;
                            case 1:
                            case 5:
                                // ED 01 X01 110
                                // *IM 0
                                // TODO: *IM 0
                                break;
                            case 2:
                            case 6:
                                // ED 01 X10 110
                                // IM 1/*IM 1
                                // TODO: IM 1
                                break;
                            case 3:
                            case 7:
                                // ED 01 X11 110
                                // IM 2/*IM 2
                                // TODO: IM 2
                                break;
                            }
                            break;
                        case 7:
                            // ED 01 YYY 111
                            switch (YYY2) {
                            case 0:
                                // ED 01 000 111
                                // LD I, A
                                // TODO: LD I, A
                                break;
                            case 1:
                                // ED 01 001 111
                                // LD R, A
                                // TODO: LD R, A
                                break;
                            case 2:
                                // ED 01 010 111
                                // LD A, I
                                // TODO: LD A, I
                                break;
                            case 3:
                                // ED 01 011 111
                                // LD A, R
                                // TODO: LD A, R
                                break;
                            case 4:
                                // ED 01 100 111
                                // RRD
                                // TODO: RRD
                                break;
                            case 5:
                                // ED 01 101 111
                                // RLD
                                // TODO: RLD
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
                                // TODO: LDI
                                break;
                            case 1:
                                // ED 10 100 001
                                // CPI
                                // TODO: CPI
                                break;
                            case 2:
                                // ED 10 100 010
                                // INI
                                // TODO: INI
                                break;
                            case 3:
                                // ED 10 100 011
                                // OUTI
                                // TODO: OUTI
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
                                // TODO: LDD
                                break;
                            case 1:
                                // ED 10 101 001
                                // CPD
                                // TODO: CPD
                                break;
                            case 2:
                                // ED 10 101 010
                                // IND
                                // TODO: IND
                                break;
                            case 3:
                                // ED 10 101 011
                                // OUTD
                                // TODO: OUTD
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
                                // TODO: LDIR
                                break;
                            case 1:
                                // ED 10 110 001
                                // CPIR
                                // TODO: CPIR
                                break;
                            case 2:
                                // ED 10 110 010
                                // INIR
                                // TODO: INIR
                                break;
                            case 3:
                                // ED 10 110 011
                                // OTIR
                                // TODO: OTIR
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
                                // TODO: LDDR
                                break;
                            case 1:
                                // ED 10 111 001
                                // CPDR
                                // TODO: CPDR
                                break;
                            case 2:
                                // ED 10 111 010
                                // INDR
                                // TODO: INDR
                                break;
                            case 3:
                                // ED 10 111 011
                                // OTDR
                                // TODO: OTDR
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
                        cycles += do_DD_FD_CB(command);
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
                D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, 0);
                context.registers.regs.A = D.b.L;
                break;
            case 1:
                //ADC A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w + CARRY;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, CARRY);
                context.registers.regs.A = D.b.L;
                break;
            case 2:
                //SUB A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), 1);
                context.registers.regs.A = D.b.L;
                break;
            case 3:
                //SBC A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w - CARRY;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), !CARRY);
                context.registers.regs.A = D.b.L;
                break;
            case 4:
                //AND A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) & T.w;
                context.registers.regs.F = calc_base_flags(D.b.L) | ((((context.registers.regs.A | T.b.L) & 0x08) != 0 )?F_HALF_CARRY:0); //CY=0
                context.registers.regs.A = D.b.L;
                break;
            case 5:
                //XOR A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) ^ T.w;
                context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
                context.registers.regs.A = D.b.L;
                break;
            case 6:
                //OR A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) | T.w;
                context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
                context.registers.regs.A = D.b.L;
                break;
            default: //7
                //CP A, d
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), 1);
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
    }

    return cycles;

}
