#include <QDebug>

#include "i8080core.h"

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

    //Store PC for debug purposes
    context.registers.regs.PC2 = context.registers.regs.PC;
    //qDebug() << Qt::hex << context.registers.regs.PC;

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
            context.registers.regs.F &= F_CARRY;                                            //Clear flags except carry
            context.registers.regs.F |= calc_flags(T.b.L, 0, D.dw) & ((F_ALL - F_CARRY));   //Then set other
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
            context.registers.regs.F &= F_CARRY;                                            //Clear flags except carry
            context.registers.regs.F |= calc_flags(T.b.L, 0xFF, D.dw) & (F_ALL - F_CARRY);  //Then set other
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
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | (T.b.H & 0x01)?F_CARRY:0;
                break;
            case 1:
                //00_001_111
                //RRC
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | (context.registers.regs.A & 0x01)?F_CARRY:0;
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 7;
                context.registers.regs.A = T.b.H | (T.b.L & 0x80);
                break;
            case 2:
                //00_010_111
                //RAL
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 1;
                context.registers.regs.A = T.b.L | (context.registers.regs.F & F_CARRY)?1:0;
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | (T.b.H & 0x01)?F_CARRY:0;
                break;
            case 3:
                //00_011_111
                //RAR
                T.w = static_cast<uint16_t>(context.registers.regs.A) << 7;
                context.registers.regs.A = T.b.H | (context.registers.regs.F & F_CARRY)?0x80:0;
                context.registers.regs.F = (context.registers.regs.F & (F_ALL - F_CARRY)) | (T.b.L >> 7)?F_CARRY:0;
                break;
            case 4:
                //00_100_111
                //DAA
                T.w = static_cast<uint16_t>(context.registers.regs.A);
                D.w = 0;
                if (((T.b.L & 0x0F) > 9) || ((context.registers.regs.F & F_HALF_CARRY) != 0))
                    D.b.L += 0x06;
                if (((T.b.L & 0xF0) > 0x90) or ((context.registers.regs.F & F_CARRY) != 0))
                    D.b.L += 0x60;
                T.w += D.w;
                context.registers.regs.A = T.b.L;
                context.registers.regs.F = calc_flags(T.b.L, D.b.L, T.w);
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
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        case 1:
            //ADC
            T.w += (context.registers.regs.F & F_CARRY)?1:0;
            D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        case 2:
            //SUB
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        case 3:
            //SBB
            //A = A - (B + CARRY)
            T.w += (context.registers.regs.F & F_CARRY)?1:0;
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        case 4:
            //ANA
            D.w = static_cast<uint16_t>(context.registers.regs.A) & T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        case 5:
            //XRA
            D.w = static_cast<uint16_t>(context.registers.regs.A) ^ T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        case 6:
            //ORA
            D.w = static_cast<uint16_t>(context.registers.regs.A) | T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
            context.registers.regs.A = D.b.L;
            break;
        default: //7
            //CMP
            D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
            context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
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
                    context.registers.regs.F = T.b.L;
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
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            case 1:
                //ACI
                T.w += (context.registers.regs.F & F_CARRY)?1:0;
                D.w = static_cast<uint16_t>(context.registers.regs.A) + T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            case 2:
                //SUI
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            case 3:
                //SBI
                //A = A - (B + CARRY)
                T.w += (context.registers.regs.F & F_CARRY)?1:0;
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            case 4:
                //ANI
                D.w = static_cast<uint16_t>(context.registers.regs.A) & T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            case 5:
                //XRI
                D.w = static_cast<uint16_t>(context.registers.regs.A) ^ T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            case 6:
                //ORI
                D.w = static_cast<uint16_t>(context.registers.regs.A) | T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
                context.registers.regs.A = D.b.L;
                break;
            default: //7
                //CPI
                D.w = static_cast<uint16_t>(context.registers.regs.A) - T.w;
                context.registers.regs.F = calc_flags(context.registers.regs.A, T.w, D.w);
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
