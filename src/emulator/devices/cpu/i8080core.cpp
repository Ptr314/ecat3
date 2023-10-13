#include <QDebug>

#include "cpu_utils.h"
#include "i8080core.h"

using namespace I8080;

#define CARRY       (context.registers.regs.F & F_CARRY)
#define HALF_CARRY  (context.registers.regs.F & F_HALF_CARRY)

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

//static const uint8_t I8080LENGTHS[256] = {
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


i8080core::i8080core()
{
    //TODO: 8080 core constructor
    context.registers.regs.PC = 0;
    context.halted = false;
    context.int_enable = 0;
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

inline uint8_t i8080core::calc_base_flags(uint32_t value)
{

    return F_BASE_8080
           | ( (value >> 8) & F_CARRY )
           | ZERO_SIGN[(uint8_t)value]
           | PARITY[(uint8_t)value];
}

inline uint8_t i8080core::calc_half_carry(uint8_t v1, uint8_t v2, uint8_t c)
{

    return (LO4(v1) + LO4(v2) + c) & F_HALF_CARRY;
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
    inte_changed(context.int_enable);
}

i8080context * i8080core::get_context()
{
    return &context;
}

uint8_t i8080core::read_port(uint16_t address){
    return 0xFF;
}

void i8080core::write_port(uint16_t address, uint8_t value)
{}

void i8080core::inte_changed(unsigned int inte)
{}

unsigned int i8080core::execute()
{
    uint8_t command;
    uint16_t port;
    unsigned int XX, YYY, ZZZ, PP, Q;
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
                //STAX [R], A
                write_mem(context.registers.reg_array_16[PP & 0x01], context.registers.regs.A);
                break;
            case 1:
            case 3:
                //00_0R1_010
                //LDAX A, [R]
                context.registers.regs.A = read_mem(context.registers.reg_array_16[PP & 0x01]);
                break;
            case 4:
                //00_100_010
                //SHLD ADDR16
                T.b.L = next_byte();
                T.b.H = next_byte();
                write_mem(T.w, context.registers.regs.L);
                write_mem(static_cast<uint16_t>(T.w+1), context.registers.regs.H);
                break;
            case 5:
                //00_101_010
                //LHLD ADDR16
                T.b.L = next_byte();
                T.b.H = next_byte();
                context.registers.regs.L = read_mem(T.w);
                context.registers.regs.H = read_mem(static_cast<uint16_t>(T.w+1));
                break;
            case 6:
                //00_110_010
                //STA ADDR16
                T.b.L = next_byte();
                T.b.H = next_byte();
                write_mem(T.w, context.registers.regs.A);
                break;
            case 7:
                //00_111_010
                //LDA ADDR16
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
                //INX RP
                if (PP == 3)
                    context.registers.regs.SP++;
                else
                    context.registers.reg_array_16[PP]++;
                break;
            default:
                //00_RP1_011
                //DCX RP
                if (PP == 3)
                    context.registers.regs.SP--;
                else
                    context.registers.reg_array_16[PP]--;
            }
            break;
        case 4:
            //00_YYY_100
            //INR SSS
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
            //DCR SSS
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
            //MVI DDD, DATA8
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
                //RLC
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 1;
                context.registers.regs.A = T.b.L | (T.b.H & 0x01);
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((T.b.H & 0x01)?F_CARRY:0);
                break;
            case 1:
                //00_001_111
                //RRC
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((context.registers.regs.A & 0x01)?F_CARRY:0);
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 7;
                context.registers.regs.A = T.b.H | (T.b.L & 0x80);
                break;
            case 2:
                //00_010_111
                //RAL
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 1;
                context.registers.regs.A = T.b.L | ((context.registers.regs.F & F_CARRY)?1:0);
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | ((T.b.H & 0x01)?F_CARRY:0);
                break;
            case 3:
                //00_011_111
                //RAR
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
                //CMA
                context.registers.regs.A  = ~context.registers.regs.A;
                break;
            case 6:
                //00_110_111
                //STC
                context.registers.regs.F |= F_CARRY;
                break;
            default: //7
                //00_111_111
                //CMC
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
            //MOV DDD, SSS
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
            //ADD
            D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, 0);
            context.registers.regs.A = D.b.L;
            break;
        case 1:
            //ADC
            D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w + CARRY;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, CARRY);
            context.registers.regs.A = D.b.L;
            break;
        case 2:
            //SUB
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), 1);
            context.registers.regs.A = D.b.L;
            break;
        case 3:
            //SBB
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w - CARRY;
            context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), !CARRY);
            context.registers.regs.A = D.b.L;
            break;
        case 4:
            //ANA
            D.w = static_cast<uint16_t>(context.registers.regs.A) & T.w;
            context.registers.regs.F = calc_base_flags(D.b.L) | ((((context.registers.regs.A | T.b.L) & 0x08) != 0 )?F_HALF_CARRY:0); //CY=0!
            context.registers.regs.A = D.b.L;
            break;
        case 5:
            //XRA
            D.w = static_cast<uint16_t>(context.registers.regs.A) ^ T.w;
            context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
            context.registers.regs.A = D.b.L;
            break;
        case 6:
            //ORA
            D.w = static_cast<uint16_t>(context.registers.regs.A) | T.w;
            context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
            context.registers.regs.A = D.b.L;
            break;
        default: //7
            //CMP
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
            //RET IF
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
                    //RET (undoc?)
                    do_ret();
                    break;
                case 2:
                    //11_101_001
                    //PCHL
                    context.registers.regs.PC = context.registers.reg_pairs.HL;
                    break;
                default: //3
                    //11_111_001
                    //SPHL
                    context.registers.regs.SP = context.registers.reg_pairs.HL;
                    break;
                }
                break;
            }
            break;
        case 2:
            //11_YYY_010
            //JUMP IF
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
                //JMP ADDR16
                do_jump();
                break;
            case 1:
                //11_001_011
                //CB (8080: Undoc JMP)
                do_jump();
                break;
            case 2:
                //11_010_011
                //OUT port
                port = next_byte();
                write_port(port + (port << 8), context.registers.regs.A);
                break;
            case 3:
                //11_011_011
                //IN port
                port = next_byte();
                context.registers.regs.A = read_port(port + (port << 8));
                break;
            case 4:
                //11_100_011
                //XTHL
                T.b.L = read_mem(context.registers.regs.SP);
                T.b.H = read_mem(static_cast<uint16_t>(context.registers.regs.SP+1));
                write_mem(context.registers.regs.SP, context.registers.regs.L);
                write_mem(static_cast<uint16_t>(context.registers.regs.SP+1), context.registers.regs.H);
                context.registers.reg_pairs.HL = T.w;
                break;
            case 5:
                //11_101_011
                //XCHG
                T.w = context.registers.reg_pairs.HL;
                context.registers.reg_pairs.HL = context.registers.reg_pairs.DE;
                context.registers.reg_pairs.DE = T.w;
                break;
            case 6:
                //11_110_011
                //DI
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
            //CALL IF
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
                //CALL (undoc?)
                do_call();
                break;
            }
            break;
        case 6:
            //11_YYY_110
            T.w = next_byte();
            switch (YYY) {
            case 0:
                //ADI
                D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, 0);
                context.registers.regs.A = D.b.L;
                break;
            case 1:
                //ACI
                D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w + CARRY;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, T.b.L, CARRY);
                context.registers.regs.A = D.b.L;
                break;
            case 2:
                //SUI
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), 1);
                context.registers.regs.A = D.b.L;
                break;
            case 3:
                //SBI
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w - CARRY;
                context.registers.regs.F = calc_base_flags(D.w) | calc_half_carry(context.registers.regs.A, ~(T.b.L), !CARRY);
                context.registers.regs.A = D.b.L;
                break;
            case 4:
                //ANI
                D.w = static_cast<uint16_t>(context.registers.regs.A) & T.w;
                context.registers.regs.F = calc_base_flags(D.b.L) | ((((context.registers.regs.A | T.b.L) & 0x08) != 0 )?F_HALF_CARRY:0); //CY=0
                context.registers.regs.A = D.b.L;
                break;
            case 5:
                //XRI
                D.w = static_cast<uint16_t>(context.registers.regs.A) ^ T.w;
                context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
                context.registers.regs.A = D.b.L;
                break;
            case 6:
                //ORI
                D.w = static_cast<uint16_t>(context.registers.regs.A) | T.w;
                context.registers.regs.F = calc_base_flags(D.b.L); //CY=0, HC=0!
                context.registers.regs.A = D.b.L;
                break;
            default: //7
                //CPI
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
