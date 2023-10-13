#include <QDebug>

#include "cpu_utils.h"
#include "z80core.h"

using namespace Z80;

#define CARRY       (context.registers.regs.F & F_CARRY)
#define HALF_CARRY  (context.registers.regs.F & F_HALF_CARRY)

#define REG_A       context.registers.regs.A
#define REG_B       context.registers.regs.B
#define REG_F       context.registers.regs.F
#define REG_AF      context.registers.reg_pairs.AF
#define REG_BC      context.registers.reg_pairs.BC
#define REG_DE      context.registers.reg_pairs.DE
#define REG_HL      context.registers.reg_pairs.HL

#define REG_PC      context.registers.regs.PC

#define REG_AF_      context.registers.regs.AF_
#define REG_BC_      context.registers.regs.BC_
#define REG_DE_      context.registers.regs.DE_
#define REG_HL_      context.registers.regs.HL_

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

inline void z80core::do_bit(unsigned int bit, uint8_t v)
{
    uint8_t result = v & (1 << bit);
    //TODO: BIT set flags
}

inline uint8_t z80core::do_res(unsigned int bit, uint8_t v)
{
    uint8_t result = v & ~(1 << bit);
    //TODO: BIT set flags
    return result;
}

inline uint8_t z80core::do_set(unsigned int bit, uint8_t v)
{
    uint8_t result = v | (1 << bit);
    //TODO: BIT set flags
    return result;
}


void z80core::reset()
{
    context.registers.regs.PC = 0;
    context.halted = false;
    context.int_enable = 0;
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

unsigned int z80core::execute()
{
    uint8_t command, command2;
    uint16_t port;
    unsigned int XX, YYY, ZZZ, PP, Q, XX2, YYY2, ZZZ2;
    PartsRecLE T, D;
    unsigned int cycles;

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
                if (PP == 3)
                    context.registers.regs.SP = T.w;
                else
                    context.registers.reg_array_16[PP] = T.w;
                break;
            default:
                //00_RP1_001
                //ADD HL, RP
                if (PP == 3)
                    T.dw = static_cast<uint32_t>(context.registers.reg_pairs.HL) + context.registers.regs.SP;
                else
                    T.dw = static_cast<uint32_t>(context.registers.reg_pairs.HL) + context.registers.reg_array_16[PP];
                context.registers.reg_pairs.HL = T.w;
                //Carry
                if ((T.dw & 0x10000) != 0)
                    context.registers.regs.F |= F_CARRY;
                else
                    context.registers.regs.F &= (F_ALL - F_CARRY);
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
                write_mem(T.w, context.registers.regs.L);
                write_mem(static_cast<uint16_t>(T.w+1), context.registers.regs.H);
                break;
            case 5:
                //00_101_010
                //LD HL, [nn]
                T.b.L = next_byte();
                T.b.H = next_byte();
                context.registers.regs.L = read_mem(T.w);
                context.registers.regs.H = read_mem(static_cast<uint16_t>(T.w+1));
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
                if (PP == 3)
                    context.registers.regs.SP++;
                else
                    context.registers.reg_array_16[PP]++;
                break;
            default:
                //00_RP1_011
                //DEC RP
                if (PP == 3)
                    context.registers.regs.SP--;
                else
                    context.registers.reg_array_16[PP]--;
            }
            break;
        case 4:
            //00_YYY_100
            //INC SSS
            if (YYY == 6)
                T.b.L = read_mem(context.registers.reg_pairs.HL);
            else
                T.b.L = context.registers.reg_array_8[REGISTERS8[YYY]];
            D.dw = static_cast<uint32_t>(T.b.L) + 1;
            context.registers.regs.F &= F_CARRY;                                    //Clear flags except carry & HC
            context.registers.regs.F |= calc_base_flags(D.dw) & (F_ALL - F_CARRY);  //Then set other
            context.registers.regs.F |= calc_half_carry(T.b.L, 1, 0);               //HC
            if (YYY == 6)
                write_mem(context.registers.reg_pairs.HL, D.b.L);
            else
                context.registers.reg_array_8[REGISTERS8[YYY]] = D.b.L;
            break;
        case 5:
            //00_YYY_101
            //DEC SSS
            if (YYY == 6)
                T.b.L = read_mem(context.registers.reg_pairs.HL);
            else
                T.b.L = context.registers.reg_array_8[REGISTERS8[YYY]];
            D.dw = static_cast<uint32_t>(T.b.L) - 1;
            context.registers.regs.F &= F_CARRY;                                    //Clear flags except carry & HC
            context.registers.regs.F |= calc_base_flags(D.dw) & (F_ALL - F_CARRY);  //Then set other
            context.registers.regs.F |= calc_half_carry(T.b.L, 0x0F, 0);            //HC
            if (YYY == 6)
                write_mem(context.registers.reg_pairs.HL, D.b.L);
            else
                context.registers.reg_array_8[REGISTERS8[YYY]] = D.b.L;
            break;
        case 6:
            //00_YYY_110
            //LD DDD, d
            T.b.L = next_byte();
            if (YYY == 6)
                write_mem(context.registers.reg_pairs.HL, T.b.L);
            else
                context.registers.reg_array_8[REGISTERS8[YYY]] = T.b.L;
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
            if (ZZZ == 6)
                T.b.L = read_mem(context.registers.reg_pairs.HL);
            else
                T.b.L = context.registers.reg_array_8[REGISTERS8[ZZZ]];
            if (YYY == 6)
                write_mem(context.registers.reg_pairs.HL, T.b.L);
            else
                context.registers.reg_array_8[REGISTERS8[YYY]] = T.b.L;
        }
        break;
    case 2:
        //10_YYY_ZZZ

        if (ZZZ == 6)
            T.w = static_cast<uint16_t>(read_mem(context.registers.reg_pairs.HL));
        else
            T.w = static_cast<uint16_t>(context.registers.reg_array_8[REGISTERS8[ZZZ]]);
        switch (YYY) {
        case 0:
            //ADD A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, 0);
            context.registers.regs.A = D.b.L;
            break;
        case 1:
            //ADC A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w + CARRY;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, CARRY);
            context.registers.regs.A = D.b.L;
            break;
        case 2:
            //SUB A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), 1);
            context.registers.regs.A = D.b.L;
            break;
        case 3:
            //SBC A, ZZZ
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w - CARRY;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), !CARRY);
            context.registers.regs.A = D.b.L;
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
                if (PP == 3)
                {
                    context.registers.regs.F = T.b.L | F_BASE_8080;
                    context.registers.regs.A = T.b.H;
                } else
                    context.registers.reg_array_16[PP] = T.w;
                context.registers.regs.SP += 2;
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
                    context.registers.regs.PC = context.registers.reg_pairs.HL;
                    break;
                default: //3
                    //11_111_001
                    //LD SP,HL
                    context.registers.regs.SP = context.registers.reg_pairs.HL;
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
                switch (XX2) {
                case 0:
                    // CB 00_rot_SSS

                    if (ZZZ2 == 6)
                        T.b.L = read_mem(REG_HL);
                    else
                        T.b.L = context.registers.reg_array_8[REGISTERS8[ZZZ2]];

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
                    }

                    if (ZZZ2 == 6)
                        write_mem(REG_HL, D.b.L);
                    else
                        context.registers.reg_array_8[REGISTERS8[ZZZ2]] = D.b.L;

                    break;
                case 1:
                    // CB 01_bit_SSS
                    //BIT bit, SSS
                    if (ZZZ2 == 6)
                        T.b.L = read_mem(REG_HL);
                    else
                        T.b.L = context.registers.reg_array_8[REGISTERS8[ZZZ2]];

                    do_bit(YYY2, T.b.L);

                    break;
                case 2:
                    // CB 10 bit SSS
                    //RES bit, SSS
                    if (ZZZ2 == 6)
                        T.b.L = read_mem(REG_HL);
                    else
                        T.b.L = context.registers.reg_array_8[REGISTERS8[ZZZ2]];

                    D.b.L = do_res(YYY2, T.b.L);

                    if (ZZZ2 == 6)
                        write_mem(REG_HL, D.b.L);
                    else
                        context.registers.reg_array_8[REGISTERS8[ZZZ2]] = D.b.L;

                    break;
                case 3:
                    // CB 11 bit SSS
                    //SET bit, SSS
                    if (ZZZ2 == 6)
                        T.b.L = read_mem(REG_HL);
                    else
                        T.b.L = context.registers.reg_array_8[REGISTERS8[ZZZ2]];

                    D.b.L = do_set(YYY2, T.b.L);

                    if (ZZZ2 == 6)
                        write_mem(REG_HL, D.b.L);
                    else
                        context.registers.reg_array_8[REGISTERS8[ZZZ2]] = D.b.L;
                    break;
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
                write_mem(context.registers.regs.SP, context.registers.regs.L);
                write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), context.registers.regs.H);
                context.registers.reg_pairs.HL = T.w;
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
                if (PP == 3)
                {
                    T.b.L = context.registers.regs.F;
                    T.b.H = context.registers.regs.A;
                } else
                    T.w = context.registers.reg_array_16[PP];
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
                case 1:
                    //11_011_101
                    //Prefix DD
                    //TODO: Prefix DD
                    break;
                case 2:
                    //11_101_101
                    //Prefix ED
                    //TODO: Prefix ED
                    break;
                case 3:
                    //11_111_101
                    //Prefix FD
                    //TODO: Prefix FD
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

    return cycles;

}
