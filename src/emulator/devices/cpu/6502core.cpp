#include "6502core.h"
#include "cpu_utils.h"

using namespace MOS6502;

#define REG_A context.A
#define REG_X context.X
#define REG_Y context.Y
#define REG_P context.P
#define REG_S context.S

#define REG_PC context.r16.PC
#define REG_PCH context.r8.PCH
#define REG_PCL context.r8.PCL

#define FLAG_B (REG_P & F_B)
#define FLAG_C (REG_P & F_C)
#define FLAG_D (REG_P & F_D)
#define FLAG_Z (REG_P & F_Z)
#define FLAG_P (REG_P & F_P)
#define FLAG_N (REG_P & F_N)
#define FLAG_V (REG_P & F_V)
#define FLAG_I (REG_P & F_I)


static const uint8_t MOS6502_TIMES[256] = {
                       7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6,
                       2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                       6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6,
                       2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                       6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6,
                       2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                       6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6,
                       2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                       2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,
                       2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
                       2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,
                       2,5,2,5,4,4,4,4,2,4,2,5,4,4,4,4,
                       2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,
                       2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                       2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,
                       2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7
                    };

//TODO: fill values
static const uint8_t WDC65c02_TIMES[256] = {
                        7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6,
                        2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                        6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6,
                        2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                        6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6,
                        2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                        6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6,
                        2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                        2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,
                        2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
                        2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,
                        2,5,2,5,4,4,4,4,2,4,2,5,4,4,4,4,
                        2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,
                        2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7,
                        2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,
                        2,5,2,8,4,4,6,6,2,4,2,7,5,5,7,7
};

static const uint8_t ZERO_SIGN[256] = {
                        F_Z,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    // 00-0F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 10-1F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 20-2F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 30-3F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 40-4F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 50-5F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 60-6F */
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,        // 70-7F */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // 80-8F */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // 90-9F */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // A0-AF */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // B0-BF */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // C0-CF */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // D0-DF */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,        // E0-EF */
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N,
                        F_N,F_N,F_N,F_N,F_N,F_N,F_N,F_N         // F0-FF */
};

mos6502core::mos6502core(int family_type)
{
    memset(&context, 0, sizeof(context));
    context.type = family_type;
    switch (context.type) {
    case MOS_6502_FAMILY_BASIC:
        init_commands_6502();
        break;
    case MOS_6502_FAMILY_65C02:
        init_commands_65c02();
        break;
    default:
        break;
    }
    calc_flags(0, F_ALL);
    context.is_irq = context.is_nmi = false;
    context.stop = context.wait = false;
}

void mos6502core::reset()
{
    REG_PCL = read_mem(0xFFFC);
    REG_PCH = read_mem(0xFFFD);
    REG_S = 0xFF;
    context.stop = context.wait = false;
    if (context.type == MOS_6502_FAMILY_65C02)
        set_flag(F_D, 0);

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

void mos6502core::set_nmi(bool nmi_val)
{
    context.is_nmi = nmi_val;
}

void mos6502core::set_irq(bool irq_val)
{
    context.is_irq = irq_val;
}

void mos6502core::init_commands_6502()
{
    commands[0x00] = &mos6502core::_BRK;
    commands[0x01] = &mos6502core::_ORA;
    commands[0x02] = &mos6502core::__KILL;
    commands[0x03] = &mos6502core::__SLO;
    commands[0x04] = &mos6502core::__NOP;
    commands[0x05] = &mos6502core::_ORA;
    commands[0x06] = &mos6502core::_ASL;
    commands[0x07] = &mos6502core::__SLO;
    commands[0x08] = &mos6502core::_PHP;
    commands[0x09] = &mos6502core::_ORA;
    commands[0x0A] = &mos6502core::_ASL;
    commands[0x0B] = &mos6502core::__ANC;
    commands[0x0C] = &mos6502core::__NOP;
    commands[0x0D] = &mos6502core::_ORA;
    commands[0x0E] = &mos6502core::_ASL;
    commands[0x0F] = &mos6502core::__SLO;

    commands[0x10] = &mos6502core::_BRANCH;
    commands[0x11] = &mos6502core::_ORA;
    commands[0x12] = &mos6502core::__KILL;
    commands[0x13] = &mos6502core::__SLO;
    commands[0x14] = &mos6502core::__NOP;
    commands[0x15] = &mos6502core::_ORA;
    commands[0x16] = &mos6502core::_ASL;
    commands[0x17] = &mos6502core::__SLO;
    commands[0x18] = &mos6502core::_CLC;
    commands[0x19] = &mos6502core::_ORA;
    commands[0x1A] = &mos6502core::__NOP;
    commands[0x1B] = &mos6502core::__SLO;
    commands[0x1C] = &mos6502core::__NOP;
    commands[0x1D] = &mos6502core::_ORA;
    commands[0x1E] = &mos6502core::_ASL;
    commands[0x1F] = &mos6502core::__SLO;

    commands[0x20] = &mos6502core::_JSR;
    commands[0x21] = &mos6502core::_AND;
    commands[0x22] = &mos6502core::__KILL;
    commands[0x23] = &mos6502core::__RLA;
    commands[0x24] = &mos6502core::_BIT;
    commands[0x25] = &mos6502core::_AND;
    commands[0x26] = &mos6502core::_ROL;
    commands[0x27] = &mos6502core::__RLA;
    commands[0x28] = &mos6502core::_PLP;
    commands[0x29] = &mos6502core::_AND;
    commands[0x2A] = &mos6502core::_ROL;
    commands[0x2B] = &mos6502core::__ANC2;
    commands[0x2C] = &mos6502core::_BIT;
    commands[0x2D] = &mos6502core::_AND;
    commands[0x2E] = &mos6502core::_ROL;
    commands[0x2F] = &mos6502core::__RLA;

    commands[0x30] = &mos6502core::_BRANCH;
    commands[0x31] = &mos6502core::_AND;
    commands[0x32] = &mos6502core::__KILL;
    commands[0x33] = &mos6502core::__RLA;
    commands[0x34] = &mos6502core::__NOP;
    commands[0x35] = &mos6502core::_AND;
    commands[0x36] = &mos6502core::_ROL;
    commands[0x37] = &mos6502core::__RLA;
    commands[0x38] = &mos6502core::_SEC;
    commands[0x39] = &mos6502core::_AND;
    commands[0x3A] = &mos6502core::__NOP;
    commands[0x3B] = &mos6502core::__RLA;
    commands[0x3C] = &mos6502core::__NOP;
    commands[0x3D] = &mos6502core::_AND;
    commands[0x3E] = &mos6502core::_ROL;
    commands[0x3F] = &mos6502core::__RLA;

    commands[0x40] = &mos6502core::_RTI;
    commands[0x41] = &mos6502core::_EOR;
    commands[0x42] = &mos6502core::__KILL;
    commands[0x43] = &mos6502core::__SRE;
    commands[0x44] = &mos6502core::__NOP;
    commands[0x45] = &mos6502core::_EOR;
    commands[0x46] = &mos6502core::_LSR;
    commands[0x47] = &mos6502core::__SRE;
    commands[0x48] = &mos6502core::_PHA;
    commands[0x49] = &mos6502core::_EOR;
    commands[0x4A] = &mos6502core::_LSR;
    commands[0x4B] = &mos6502core::__ASR;
    commands[0x4C] = &mos6502core::_JMP;
    commands[0x4D] = &mos6502core::_EOR;
    commands[0x4E] = &mos6502core::_LSR;
    commands[0x4F] = &mos6502core::__SRE;

    commands[0x50] = &mos6502core::_BRANCH;
    commands[0x51] = &mos6502core::_EOR;
    commands[0x52] = &mos6502core::__KILL;
    commands[0x53] = &mos6502core::__SRE;
    commands[0x54] = &mos6502core::__NOP;
    commands[0x55] = &mos6502core::_EOR;
    commands[0x56] = &mos6502core::_LSR;
    commands[0x57] = &mos6502core::__SRE;
    commands[0x58] = &mos6502core::_CLI;
    commands[0x59] = &mos6502core::_EOR;
    commands[0x5A] = &mos6502core::__NOP;
    commands[0x5B] = &mos6502core::__SRE;
    commands[0x5C] = &mos6502core::__NOP;
    commands[0x5D] = &mos6502core::_EOR;
    commands[0x5E] = &mos6502core::_LSR;
    commands[0x5F] = &mos6502core::__SRE;

    commands[0x60] = &mos6502core::_RTS;
    commands[0x61] = &mos6502core::_ADC;
    commands[0x62] = &mos6502core::__KILL;
    commands[0x63] = &mos6502core::__RRA;
    commands[0x64] = &mos6502core::__NOP;
    commands[0x65] = &mos6502core::_ADC;
    commands[0x66] = &mos6502core::_ROR;
    commands[0x67] = &mos6502core::__RRA;
    commands[0x68] = &mos6502core::_PLA;
    commands[0x69] = &mos6502core::_ADC;
    commands[0x6A] = &mos6502core::_ROR;
    commands[0x6B] = &mos6502core::__ARR;
    commands[0x6C] = &mos6502core::_JMP;
    commands[0x6D] = &mos6502core::_ADC;
    commands[0x6E] = &mos6502core::_ROR;
    commands[0x6F] = &mos6502core::__RRA;

    commands[0x70] = &mos6502core::_BRANCH;
    commands[0x71] = &mos6502core::_ADC;
    commands[0x72] = &mos6502core::__KILL;
    commands[0x73] = &mos6502core::__RRA;
    commands[0x74] = &mos6502core::__NOP;
    commands[0x75] = &mos6502core::_ADC;
    commands[0x76] = &mos6502core::_ROR;
    commands[0x77] = &mos6502core::__RRA;
    commands[0x78] = &mos6502core::_SEI;
    commands[0x79] = &mos6502core::_ADC;
    commands[0x7A] = &mos6502core::__NOP;
    commands[0x7B] = &mos6502core::__RRA;
    commands[0x7C] = &mos6502core::__NOP;
    commands[0x7D] = &mos6502core::_ADC;
    commands[0x7E] = &mos6502core::_ROR;
    commands[0x7F] = &mos6502core::__RRA;

    commands[0x80] = &mos6502core::__NOP;
    commands[0x81] = &mos6502core::_STA;
    commands[0x82] = &mos6502core::__NOP;
    commands[0x83] = &mos6502core::__SAX;
    commands[0x84] = &mos6502core::_STY;
    commands[0x85] = &mos6502core::_STA;
    commands[0x86] = &mos6502core::_STX;
    commands[0x87] = &mos6502core::__SAX;
    commands[0x88] = &mos6502core::_DEY;
    commands[0x89] = &mos6502core::__NOP;
    commands[0x8A] = &mos6502core::_TXA;
    commands[0x8B] = &mos6502core::__ANE;
    commands[0x8C] = &mos6502core::_STY;
    commands[0x8D] = &mos6502core::_STA;
    commands[0x8E] = &mos6502core::_STX;
    commands[0x8F] = &mos6502core::__SAX;

    commands[0x90] = &mos6502core::_BRANCH;
    commands[0x91] = &mos6502core::_STA;
    commands[0x92] = &mos6502core::__KILL;
    commands[0x93] = &mos6502core::__SHA;
    commands[0x94] = &mos6502core::_STY;
    commands[0x95] = &mos6502core::_STA;
    commands[0x96] = &mos6502core::_STX;
    commands[0x97] = &mos6502core::__SAX;
    commands[0x98] = &mos6502core::_TYA;
    commands[0x99] = &mos6502core::_STA;
    commands[0x9A] = &mos6502core::_TXS;
    commands[0x9B] = &mos6502core::__SHS;
    commands[0x9C] = &mos6502core::__SHY;
    commands[0x9D] = &mos6502core::_STA;
    commands[0x9E] = &mos6502core::__SHX;
    commands[0x9F] = &mos6502core::__SHA;

    commands[0xA0] = &mos6502core::_LDY;
    commands[0xA1] = &mos6502core::_LDA;
    commands[0xA2] = &mos6502core::_LDX;
    commands[0xA3] = &mos6502core::__LAX;
    commands[0xA4] = &mos6502core::_LDY;
    commands[0xA5] = &mos6502core::_LDA;
    commands[0xA6] = &mos6502core::_LDX;
    commands[0xA7] = &mos6502core::__LAX;
    commands[0xA8] = &mos6502core::_TAY;
    commands[0xA9] = &mos6502core::_LDA;
    commands[0xAA] = &mos6502core::_TAX;
    commands[0xAB] = &mos6502core::__LXA;
    commands[0xAC] = &mos6502core::_LDY;
    commands[0xAD] = &mos6502core::_LDA;
    commands[0xAE] = &mos6502core::_LDX;
    commands[0xAF] = &mos6502core::__LAX;

    commands[0xB0] = &mos6502core::_BRANCH;
    commands[0xB1] = &mos6502core::_LDA;
    commands[0xB2] = &mos6502core::__KILL;
    commands[0xB3] = &mos6502core::__LAX;
    commands[0xB4] = &mos6502core::_LDY;
    commands[0xB5] = &mos6502core::_LDA;
    commands[0xB6] = &mos6502core::_LDX;
    commands[0xB7] = &mos6502core::__LAX;
    commands[0xB8] = &mos6502core::_CLV;
    commands[0xB9] = &mos6502core::_LDA;
    commands[0xBA] = &mos6502core::_TSX;
    commands[0xBB] = &mos6502core::__LAS;
    commands[0xBC] = &mos6502core::_LDY;
    commands[0xBD] = &mos6502core::_LDA;
    commands[0xBE] = &mos6502core::_LDX;
    commands[0xBF] = &mos6502core::__LAX;

    commands[0xC0] = &mos6502core::_CPY;
    commands[0xC1] = &mos6502core::_CMP;
    commands[0xC2] = &mos6502core::__NOP;
    commands[0xC3] = &mos6502core::__DCP;
    commands[0xC4] = &mos6502core::_CPY;
    commands[0xC5] = &mos6502core::_CMP;
    commands[0xC6] = &mos6502core::_DEC;
    commands[0xC7] = &mos6502core::__DCP;
    commands[0xC8] = &mos6502core::_INY;
    commands[0xC9] = &mos6502core::_CMP;
    commands[0xCA] = &mos6502core::_DEX;
    commands[0xCB] = &mos6502core::__SBX;
    commands[0xCC] = &mos6502core::_CPY;
    commands[0xCD] = &mos6502core::_CMP;
    commands[0xCE] = &mos6502core::_DEC;
    commands[0xCF] = &mos6502core::__DCP;

    commands[0xD0] = &mos6502core::_BRANCH;
    commands[0xD1] = &mos6502core::_CMP;
    commands[0xD2] = &mos6502core::__KILL;
    commands[0xD3] = &mos6502core::__DCP;
    commands[0xD4] = &mos6502core::__NOP;
    commands[0xD5] = &mos6502core::_CMP;
    commands[0xD6] = &mos6502core::_DEC;
    commands[0xD7] = &mos6502core::__DCP;
    commands[0xD8] = &mos6502core::_CLD;
    commands[0xD9] = &mos6502core::_CMP;
    commands[0xDA] = &mos6502core::__NOP;
    commands[0xDB] = &mos6502core::__DCP;
    commands[0xDC] = &mos6502core::__NOP;
    commands[0xDD] = &mos6502core::_CMP;
    commands[0xDE] = &mos6502core::_DEC;
    commands[0xDF] = &mos6502core::__DCP;

    commands[0xE0] = &mos6502core::_CPX;
    commands[0xE1] = &mos6502core::_SBC;
    commands[0xE2] = &mos6502core::__NOP;
    commands[0xE3] = &mos6502core::__ISB;
    commands[0xE4] = &mos6502core::_CPX;
    commands[0xE5] = &mos6502core::_SBC;
    commands[0xE6] = &mos6502core::_INC;
    commands[0xE7] = &mos6502core::__ISB;
    commands[0xE8] = &mos6502core::_INX;
    commands[0xE9] = &mos6502core::_SBC;
    commands[0xEA] = &mos6502core::_NOP;
    commands[0xEB] = &mos6502core::__SBC;
    commands[0xEC] = &mos6502core::_CPX;
    commands[0xED] = &mos6502core::_SBC;
    commands[0xEE] = &mos6502core::_INC;
    commands[0xEF] = &mos6502core::__ISB;

    commands[0xF0] = &mos6502core::_BRANCH;
    commands[0xF1] = &mos6502core::_SBC;
    commands[0xF2] = &mos6502core::__KILL;
    commands[0xF3] = &mos6502core::__ISB;
    commands[0xF4] = &mos6502core::__NOP;
    commands[0xF5] = &mos6502core::_SBC;
    commands[0xF6] = &mos6502core::_INC;
    commands[0xF7] = &mos6502core::__ISB;
    commands[0xF8] = &mos6502core::_SED;
    commands[0xF9] = &mos6502core::_SBC;
    commands[0xFA] = &mos6502core::__NOP;
    commands[0xFB] = &mos6502core::__ISB;
    commands[0xFC] = &mos6502core::__NOP;
    commands[0xFD] = &mos6502core::_SBC;
    commands[0xFE] = &mos6502core::_INC;
    commands[0xFF] = &mos6502core::__ISB;
}

void mos6502core::init_commands_65c02()
{
    commands[0x00] = &mos6502core::_BRK;
    commands[0x01] = &mos6502core::_ORA;
    commands[0x02] = &mos6502core::_NOPc02; //
    commands[0x03] = &mos6502core::_NOPc02; //
    commands[0x04] = &mos6502core::_TSB;
    commands[0x05] = &mos6502core::_ORA;
    commands[0x06] = &mos6502core::_ASL;
    commands[0x07] = &mos6502core::_RMB; //
    commands[0x08] = &mos6502core::_PHP;
    commands[0x09] = &mos6502core::_ORA;
    commands[0x0A] = &mos6502core::_ASL;
    commands[0x0B] = &mos6502core::_NOPc02; //
    commands[0x0C] = &mos6502core::_TSB;
    commands[0x0D] = &mos6502core::_ORA;
    commands[0x0E] = &mos6502core::_ASL;
    commands[0x0F] = &mos6502core::_BBR; //

    commands[0x10] = &mos6502core::_BRANCH;
    commands[0x11] = &mos6502core::_ORA;
    commands[0x12] = &mos6502core::_ORAc02; //
    commands[0x13] = &mos6502core::_NOPc02; //
    commands[0x14] = &mos6502core::_TRB; //
    commands[0x15] = &mos6502core::_ORA;
    commands[0x16] = &mos6502core::_ASL;
    commands[0x17] = &mos6502core::_RMB; //
    commands[0x18] = &mos6502core::_CLC;
    commands[0x19] = &mos6502core::_ORA;
    commands[0x1A] = &mos6502core::_INAc02; //
    commands[0x1B] = &mos6502core::_NOPc02; //
    commands[0x1C] = &mos6502core::_TRB; //
    commands[0x1D] = &mos6502core::_ORA;
    commands[0x1E] = &mos6502core::_ASL;
    commands[0x1F] = &mos6502core::_BBR; //

    commands[0x20] = &mos6502core::_JSR;
    commands[0x21] = &mos6502core::_AND;
    commands[0x22] = &mos6502core::_NOPc02; //
    commands[0x23] = &mos6502core::_NOPc02; //
    commands[0x24] = &mos6502core::_BIT;
    commands[0x25] = &mos6502core::_AND;
    commands[0x26] = &mos6502core::_ROL;
    commands[0x27] = &mos6502core::_RMB; //
    commands[0x28] = &mos6502core::_PLP;
    commands[0x29] = &mos6502core::_AND;
    commands[0x2A] = &mos6502core::_ROL;
    commands[0x2B] = &mos6502core::_NOPc02; //
    commands[0x2C] = &mos6502core::_BIT;
    commands[0x2D] = &mos6502core::_AND;
    commands[0x2E] = &mos6502core::_ROL;
    commands[0x2F] = &mos6502core::_BBR; //

    commands[0x30] = &mos6502core::_BRANCH;
    commands[0x31] = &mos6502core::_AND;
    commands[0x32] = &mos6502core::_ANDc02; //
    commands[0x33] = &mos6502core::_NOPc02; //
    commands[0x34] = &mos6502core::_BIT; //
    commands[0x35] = &mos6502core::_AND;
    commands[0x36] = &mos6502core::_ROL;
    commands[0x37] = &mos6502core::_RMB; //
    commands[0x38] = &mos6502core::_SEC;
    commands[0x39] = &mos6502core::_AND;
    commands[0x3A] = &mos6502core::_DEAc02; //
    commands[0x3B] = &mos6502core::_NOPc02; //
    commands[0x3C] = &mos6502core::_BIT; //
    commands[0x3D] = &mos6502core::_AND;
    commands[0x3E] = &mos6502core::_ROL;
    commands[0x3F] = &mos6502core::_BBR; //

    commands[0x40] = &mos6502core::_RTI;
    commands[0x41] = &mos6502core::_EOR;
    commands[0x42] = &mos6502core::_NOPc02; //
    commands[0x43] = &mos6502core::_NOPc02; //
    commands[0x44] = &mos6502core::_NOPc02; //
    commands[0x45] = &mos6502core::_EOR;
    commands[0x46] = &mos6502core::_LSR;
    commands[0x47] = &mos6502core::_RMB; //
    commands[0x48] = &mos6502core::_PHA;
    commands[0x49] = &mos6502core::_EOR;
    commands[0x4A] = &mos6502core::_LSR;
    commands[0x4B] = &mos6502core::_NOPc02; //
    commands[0x4C] = &mos6502core::_JMP;
    commands[0x4D] = &mos6502core::_EOR;
    commands[0x4E] = &mos6502core::_LSR;
    commands[0x4F] = &mos6502core::_BBR; //

    commands[0x50] = &mos6502core::_BRANCH;
    commands[0x51] = &mos6502core::_EOR;
    commands[0x52] = &mos6502core::_EORc02; //
    commands[0x53] = &mos6502core::_NOPc02; //
    commands[0x54] = &mos6502core::_NOPc02; //
    commands[0x55] = &mos6502core::_EOR;
    commands[0x56] = &mos6502core::_LSR;
    commands[0x57] = &mos6502core::_RMB; //
    commands[0x58] = &mos6502core::_CLI;
    commands[0x59] = &mos6502core::_EOR;
    commands[0x5A] = &mos6502core::_PHY; //
    commands[0x5B] = &mos6502core::_NOPc02; //
    commands[0x5C] = &mos6502core::_NOPc02; //
    commands[0x5D] = &mos6502core::_EOR;
    commands[0x5E] = &mos6502core::_LSR;
    commands[0x5F] = &mos6502core::_BBR; //

    commands[0x60] = &mos6502core::_RTS;
    commands[0x61] = &mos6502core::_ADC;
    commands[0x62] = &mos6502core::_NOPc02; //
    commands[0x63] = &mos6502core::_NOPc02; //
    commands[0x64] = &mos6502core::_STZ; //
    commands[0x65] = &mos6502core::_ADC;
    commands[0x66] = &mos6502core::_ROR;
    commands[0x67] = &mos6502core::_RMB; //
    commands[0x68] = &mos6502core::_PLA;
    commands[0x69] = &mos6502core::_ADC;
    commands[0x6A] = &mos6502core::_ROR;
    commands[0x6B] = &mos6502core::_NOPc02; //
    commands[0x6C] = &mos6502core::_JMP;
    commands[0x6D] = &mos6502core::_ADC;
    commands[0x6E] = &mos6502core::_ROR;
    commands[0x6F] = &mos6502core::_BBR; //

    commands[0x70] = &mos6502core::_BRANCH;
    commands[0x71] = &mos6502core::_ADC;
    commands[0x72] = &mos6502core::_ADCc02; //
    commands[0x73] = &mos6502core::_NOPc02; //
    commands[0x74] = &mos6502core::_STZ; //
    commands[0x75] = &mos6502core::_ADC;
    commands[0x76] = &mos6502core::_ROR;
    commands[0x77] = &mos6502core::_RMB; //
    commands[0x78] = &mos6502core::_SEI;
    commands[0x79] = &mos6502core::_ADC;
    commands[0x7A] = &mos6502core::_PLY; //
    commands[0x7B] = &mos6502core::_NOPc02; //
    commands[0x7C] = &mos6502core::_JMP; //
    commands[0x7D] = &mos6502core::_ADC;
    commands[0x7E] = &mos6502core::_ROR;
    commands[0x7F] = &mos6502core::_BBR; //

    commands[0x80] = &mos6502core::_BRA; //
    commands[0x81] = &mos6502core::_STA;
    commands[0x82] = &mos6502core::_NOPc02; //
    commands[0x83] = &mos6502core::_NOPc02; //
    commands[0x84] = &mos6502core::_STY;
    commands[0x85] = &mos6502core::_STA;
    commands[0x86] = &mos6502core::_STX;
    commands[0x87] = &mos6502core::_SMB; //
    commands[0x88] = &mos6502core::_DEY;
    commands[0x89] = &mos6502core::_BIT; //
    commands[0x8A] = &mos6502core::_TXA;
    commands[0x8B] = &mos6502core::_NOPc02; //
    commands[0x8C] = &mos6502core::_STY;
    commands[0x8D] = &mos6502core::_STA;
    commands[0x8E] = &mos6502core::_STX;
    commands[0x8F] = &mos6502core::_BBS; //

    commands[0x90] = &mos6502core::_BRANCH;
    commands[0x91] = &mos6502core::_STA;
    commands[0x92] = &mos6502core::_STAc02; //
    commands[0x93] = &mos6502core::_NOPc02; //
    commands[0x94] = &mos6502core::_STY;
    commands[0x95] = &mos6502core::_STA;
    commands[0x96] = &mos6502core::_STX;
    commands[0x97] = &mos6502core::_SMB; //
    commands[0x98] = &mos6502core::_TYA;
    commands[0x99] = &mos6502core::_STA;
    commands[0x9A] = &mos6502core::_TXS;
    commands[0x9B] = &mos6502core::_NOPc02; //
    commands[0x9C] = &mos6502core::_STZ; //
    commands[0x9D] = &mos6502core::_STA;
    commands[0x9E] = &mos6502core::_STZ; //
    commands[0x9F] = &mos6502core::_BBS; //

    commands[0xA0] = &mos6502core::_LDY;
    commands[0xA1] = &mos6502core::_LDA;
    commands[0xA2] = &mos6502core::_LDX;
    commands[0xA3] = &mos6502core::_NOPc02; //
    commands[0xA4] = &mos6502core::_LDY;
    commands[0xA5] = &mos6502core::_LDA;
    commands[0xA6] = &mos6502core::_LDX;
    commands[0xA7] = &mos6502core::_SMB; //
    commands[0xA8] = &mos6502core::_TAY;
    commands[0xA9] = &mos6502core::_LDA;
    commands[0xAA] = &mos6502core::_TAX;
    commands[0xAB] = &mos6502core::_NOPc02; //
    commands[0xAC] = &mos6502core::_LDY;
    commands[0xAD] = &mos6502core::_LDA;
    commands[0xAE] = &mos6502core::_LDX;
    commands[0xAF] = &mos6502core::_BBS; //

    commands[0xB0] = &mos6502core::_BRANCH;
    commands[0xB1] = &mos6502core::_LDA;
    commands[0xB2] = &mos6502core::_LDAc02; //
    commands[0xB3] = &mos6502core::_NOPc02; //
    commands[0xB4] = &mos6502core::_LDY;
    commands[0xB5] = &mos6502core::_LDA;
    commands[0xB6] = &mos6502core::_LDX;
    commands[0xB7] = &mos6502core::_SMB; //
    commands[0xB8] = &mos6502core::_CLV;
    commands[0xB9] = &mos6502core::_LDA;
    commands[0xBA] = &mos6502core::_TSX;
    commands[0xBB] = &mos6502core::_NOPc02; //
    commands[0xBC] = &mos6502core::_LDY;
    commands[0xBD] = &mos6502core::_LDA;
    commands[0xBE] = &mos6502core::_LDX;
    commands[0xBF] = &mos6502core::_BBS; //

    commands[0xC0] = &mos6502core::_CPY;
    commands[0xC1] = &mos6502core::_CMP;
    commands[0xC2] = &mos6502core::_NOPc02; //
    commands[0xC3] = &mos6502core::_NOPc02; //
    commands[0xC4] = &mos6502core::_CPY;
    commands[0xC5] = &mos6502core::_CMP;
    commands[0xC6] = &mos6502core::_DEC;
    commands[0xC7] = &mos6502core::_SMB; //
    commands[0xC8] = &mos6502core::_INY;
    commands[0xC9] = &mos6502core::_CMP;
    commands[0xCA] = &mos6502core::_DEX;
    commands[0xCB] = &mos6502core::_WAI;
    commands[0xCC] = &mos6502core::_CPY;
    commands[0xCD] = &mos6502core::_CMP;
    commands[0xCE] = &mos6502core::_DEC;
    commands[0xCF] = &mos6502core::_BBS; //

    commands[0xD0] = &mos6502core::_BRANCH;
    commands[0xD1] = &mos6502core::_CMP;
    commands[0xD2] = &mos6502core::_CMPc02; //
    commands[0xD3] = &mos6502core::_NOPc02; //
    commands[0xD4] = &mos6502core::_NOPc02; //
    commands[0xD5] = &mos6502core::_CMP;
    commands[0xD6] = &mos6502core::_DEC;
    commands[0xD7] = &mos6502core::_SMB; //
    commands[0xD8] = &mos6502core::_CLD;
    commands[0xD9] = &mos6502core::_CMP;
    commands[0xDA] = &mos6502core::_PHX; //
    commands[0xDB] = &mos6502core::_STP; //
    commands[0xDC] = &mos6502core::_NOPc02; //
    commands[0xDD] = &mos6502core::_CMP;
    commands[0xDE] = &mos6502core::_DEC;
    commands[0xDF] = &mos6502core::_BBS; //

    commands[0xE0] = &mos6502core::_CPX;
    commands[0xE1] = &mos6502core::_SBC;
    commands[0xE2] = &mos6502core::_NOPc02; //
    commands[0xE3] = &mos6502core::_NOPc02; //
    commands[0xE4] = &mos6502core::_CPX;
    commands[0xE5] = &mos6502core::_SBC;
    commands[0xE6] = &mos6502core::_INC;
    commands[0xE7] = &mos6502core::_SMB; //
    commands[0xE8] = &mos6502core::_INX;
    commands[0xE9] = &mos6502core::_SBC;
    commands[0xEA] = &mos6502core::_NOP;
    commands[0xEB] = &mos6502core::_NOPc02; //
    commands[0xEC] = &mos6502core::_CPX;
    commands[0xED] = &mos6502core::_SBC;
    commands[0xEE] = &mos6502core::_INC;
    commands[0xEF] = &mos6502core::_BBS; //

    commands[0xF0] = &mos6502core::_BRANCH;
    commands[0xF1] = &mos6502core::_SBC;
    commands[0xF2] = &mos6502core::_SBCc02; //;
    commands[0xF3] = &mos6502core::_NOPc02; //
    commands[0xF4] = &mos6502core::_NOPc02; //
    commands[0xF5] = &mos6502core::_SBC;
    commands[0xF6] = &mos6502core::_INC;
    commands[0xF7] = &mos6502core::_SMB; //
    commands[0xF8] = &mos6502core::_SED;
    commands[0xF9] = &mos6502core::_SBC;
    commands[0xFA] = &mos6502core::_PLX; //
    commands[0xFB] = &mos6502core::_NOPc02; //
    commands[0xFC] = &mos6502core::_NOPc02; //
    commands[0xFD] = &mos6502core::_SBC;
    commands[0xFE] = &mos6502core::_INC;
    commands[0xFF] = &mos6502core::_BBS; //
}

inline uint8_t mos6502core::get_operand(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    uint8_t result;

    unsigned int mode = (command >> 2) & 0x07;
    switch (mode) {
    case 0:
        //ind, x
        T.w = (next_byte() + REG_X) & 0xFF;
        D.b.L = read_mem(T.w);
        if (T.b.L == 0xFF)
            D.b.H = read_mem(0);
        else
            D.b.H = read_mem(T.w+1);
        result = read_mem(D.w);
        break;
    case 1:
        //zp
        T.w = next_byte();
        result = read_mem(T.w);
        break;
    case 2:
        //imm
        result = next_byte();
        break;
    case 3:
        //abs
        T.b.L = next_byte();
        T.b.H = next_byte();
        result = read_mem(T.w);
        break;
    case 4:
        //ind, y
        T.w = next_byte();
        D.b.L = read_mem(T.w);
        if (T.b.L == 0xFF)
            D.b.H = read_mem(0);
        else
            D.b.H = read_mem(T.w+1);
        D.w += REG_Y;
        result = read_mem(D.w);
        //TODO: 6502 add 1 cycle when crossing a page border
        break;
    case 5:
        //zp, x
        T.w = (next_byte() + REG_X) & 0xFF;
        result = read_mem(T.w);
        break;
    case 6:
        //abs, y
        T.b.L = next_byte();
        T.b.H = next_byte();
        T.w += REG_Y;
        result = read_mem(T.w);
        //TODO: 6502 add 1 cycle when crossing a page border
        break;
    default:
        //abs, x
        T.b.L = next_byte();
        T.b.H = next_byte();
        T.w += REG_X;
        result = read_mem(T.w);
        //TODO: 6502 add 1 cycle when crossing a page border
        break;
    }
    return result;
}

inline uint16_t mos6502core::get_address(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    uint16_t result;
    unsigned int mode = (command >> 2) & 0x07;
    switch (mode) {
    case 0:
        //ind, x
        T.w = (next_byte() + REG_X) & 0xFF;
        D.b.L = read_mem(T.w);
        if (T.b.L == 0xFF)
            D.b.H = read_mem(0);
        else
            D.b.H = read_mem(T.w+1);
        result = D.w;
        break;
    case 1:
        //zp
        result = next_byte();
        break;
    case 3:
        //abs
        T.b.L = next_byte();
        T.b.H = next_byte();
        result = T.w;
        break;
    case 4:
        //ind, y
        T.w = next_byte();
        D.b.L = read_mem(T.w);
        if (T.b.L == 0xFF)
            D.b.H = read_mem(0);
        else
            D.b.H = read_mem(T.w+1);
        D.w += REG_Y;
        result = D.w;
        //TODO: 6502 add 1 cycle when crossing a page border
        break;
    case 5:
        //zp, x
        T.w = (next_byte() + REG_X) & 0xFF;
        result = T.w;
        break;
    case 6:
        //abs, y
        T.b.L = next_byte();
        T.b.H = next_byte();
        T.w += REG_Y;
        result = T.w;
        //TODO: 6502 add 1 cycle when crossing a page border
        break;
    case 7:
        //abs, x
        T.b.L = next_byte();
        T.b.H = next_byte();
        T.w += REG_X;
        result = T.w;
        //TODO: 6502 add 1 cycle when crossing a page border
        break;
    default:
        // TODO: 6502 generate an error?
        result = 0;
        break;
    }
    return result;

}

inline void mos6502core::calc_flags(uint32_t value, uint32_t mask)
{
    uint8_t F = ZERO_SIGN[value & 0xFF] | ((value & 0x100) >> 8);
    REG_P = (REG_P & ~mask) | (F & mask) | F_P5;
}

inline void mos6502core::set_flag(uint8_t flag, uint8_t value)
{
    REG_P = (REG_P & ~flag) | (value & flag);
}

inline void mos6502core::doADC(uint8_t v)
{
    PartsRecLE D;
    D.w = REG_A + v + FLAG_C;
    if (FLAG_D == 0) {
        // Binary mode
        calc_flags(D.w, F_NZC);
        calc_flags(D.w, F_NZC);
        set_flag(F_V, ((REG_A ^ D.b.L) & (v ^ D.b.L)) >> 1);
        REG_A = D.b.L;
    } else {
        uint8_t AL = (REG_A & 0x0F) + (v & 0x0F) + FLAG_C;
        if (AL >= 0x0A) AL = ((AL + 0x06) & 0x0F) + 0x10;
        uint16_t A = (REG_A & 0xF0) + (v & 0xF0) + AL;
        set_flag(F_N, A & 0x80);
        set_flag(F_V, ((REG_A ^ A) & (v ^ A)) >> 1);
        if (A >= 0xA0) A += 0x60;
        REG_A = A & 0xFF;
        set_flag(F_C, (A>=0x100)?F_C:0);
        if (context.type == MOS_6502_FAMILY_BASIC)
            set_flag(F_Z, (D.b.L==0)?F_Z:0);
        else {
            set_flag(F_Z, (REG_A==0)?F_Z:0);
            set_flag(F_N, REG_A);
        }
    }
}

void mos6502core::_ADC(uint8_t command, unsigned int & cycles)
{
    doADC(get_operand(command, cycles));
}

inline void mos6502core::doAND(uint8_t v)
{
    REG_A &= v;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_AND(uint8_t command, unsigned int & cycles)
{
    doAND(get_operand(command, cycles));
}

void mos6502core::_ASL(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;

    if (command==0x0A) {
        //ASL A
        D.w = REG_A << 1;
        calc_flags(D.w, F_NZC);
        REG_A = D.b.L;
    } else {
        //ASL mem
        T.w = get_address(command, cycles);
        D.w = read_mem(T.w) << 1;
        calc_flags(D.w, F_NZC);
        write_mem(T.w, D.b.L);
    };
}

void mos6502core::_BRANCH(uint8_t command, unsigned int & cycles)
{
    bool branch = false;
    switch (command) {
    case 0xD0: branch = FLAG_Z == 0; break;	//BNE
    case 0xF0: branch = FLAG_Z != 0; break;	//BEQ
    case 0x90: branch = FLAG_C == 0; break;	//BCC
    case 0xB0: branch = FLAG_C != 0; break;	//BCS
    case 0x10: branch = FLAG_N == 0; break;	//BPL
    case 0x30: branch = FLAG_N != 0; break; //BMI
    case 0x50: branch = FLAG_V == 0; break;	//BVC
    case 0x70: branch = FLAG_V != 0; break; //BVS
    default:
        break;
    }
    int8_t offset = static_cast<int8_t>(next_byte());
    if (branch)
        REG_PC = REG_PC + offset;
}

void mos6502core::_BIT(uint8_t command, unsigned int & cycles)
{
    PartsRecLE D;
    D.b.L = get_operand(command, cycles);
    calc_flags(REG_A & D.b.L, F_Z);
    if (command != 0x89)                    // 65c02 BIT imm doesn't affect V&N
        set_flag(F_V+F_N, D.b.L);
}

void mos6502core::_BRK(uint8_t command, unsigned int & cycles)
{
    REG_PC++;
    write_mem(0x100+REG_S--, REG_PCH);
    write_mem(0x100+REG_S--, REG_PCL);
    write_mem(0x100+REG_S--, REG_P | F_B);
    set_flag(F_B+F_I, F_B+F_I);
    if (context.type == MOS_6502_FAMILY_65C02)
        set_flag(F_D, 0);
    REG_PCL = read_mem(0xFFFE);
    REG_PCH = read_mem(0xFFFF);
}

void mos6502core::_CLC(uint8_t command, unsigned int & cycles)
{
    set_flag(F_C, 0);
}

void mos6502core::_CLD(uint8_t command, unsigned int & cycles)
{
    set_flag(F_D, 0);
}

void mos6502core::_CLI(uint8_t command, unsigned int & cycles)
{
    set_flag(F_I, 0);
}

void mos6502core::_CLV(uint8_t command, unsigned int & cycles)
{
    set_flag(F_V, 0);
}

inline void mos6502core::doCMP(uint8_t v)
{
    calc_flags(static_cast<uint16_t>(REG_A) + 0x100 - v, F_NZC);
}

void mos6502core::_CMP(uint8_t command, unsigned int & cycles)
{
    doCMP(get_operand(command, cycles));
}

void mos6502core::_CPX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    switch(command) {
        case 0xE0:
            // imm
            T.b.L = next_byte();
            break;
        default:
            // zp, abs
            T.b.L =  get_operand(command, cycles);
            break;
    }
    D.w = static_cast<uint16_t>(REG_X) + 0x100 - T.b.L;
    calc_flags(D.w, F_NZC);
}

void mos6502core::_CPY(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    switch(command) {
    case 0xC0:
        // imm
        T.b.L = next_byte();
        break;
    default:
        // zp, abs
        T.b.L =  get_operand(command, cycles);
        break;
    }
    D.w = static_cast<uint16_t>(REG_Y) + 0x100 - T.b.L;
    calc_flags(D.w, F_NZC);
}

void mos6502core::_DEC(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w) - 1;
    calc_flags(D.w, F_NZ);
    write_mem(T.w, D.b.L);
}

void mos6502core::_DEX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_X) - 1;
    calc_flags(D.w, F_NZ);
    REG_X = D.b.L;
}

void mos6502core::_DEY(uint8_t command, unsigned int & cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_Y) - 1;
    calc_flags(D.w, F_NZ);
    REG_Y = D.b.L;
}

inline void mos6502core::doEOR(uint8_t v)
{
    REG_A ^= v;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_EOR(uint8_t command, unsigned int & cycles)
{
    doEOR(get_operand(command, cycles));
}

void mos6502core::_INC(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w) + 1;
    calc_flags(D.w, F_NZ);
    write_mem(T.w, D.b.L);
}

void mos6502core::_INX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_X) + 1;
    calc_flags(D.w, F_NZ);
    REG_X = D.b.L;
}

void mos6502core::_INY(uint8_t command, unsigned int & cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_Y) + 1;
    calc_flags(D.w, F_NZ);
    REG_Y = D.b.L;
}

void mos6502core::_JMP(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    T.b.L = next_byte();
    T.b.H = next_byte();
    switch (command) {
    case 0x4C:
        // JMP abs
        D.w = T.w;
        break;
    case 0x6C:
        // JMP (abs)
        D.b.L = read_mem(T.w);
        if (T.b.L == 0xFF && context.type == MOS_6502_FAMILY_BASIC)
            D.b.H = read_mem(T.w & 0xFF00);
        else
            D.b.H = read_mem(T.w+1);
        break;
    case 0x7C:
        // 65c02 JMP (abs,X)
        T.w += REG_X;
        D.b.L = read_mem(T.w);
        D.b.H = read_mem(T.w+1);
        break;
    default:
        // TODO: 6502 generate an error
        break;
    }
    REG_PC = D.w;
}

void mos6502core::_JSR(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T;
    T.b.L = next_byte();
    uint16_t ret_PC = REG_PC;
    T.b.H = next_byte();
    write_mem(REG_S+0x100, ret_PC >> 8);
    REG_S--;
    write_mem(REG_S+0x100, ret_PC & 0xFF);
    REG_S--;
    REG_PC = T.w;
}

inline void mos6502core::doLDA(uint8_t v)
{
    REG_A = v;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_LDA(uint8_t command, unsigned int & cycles)
{
    doLDA(get_operand(command, cycles));
}

void mos6502core::_LDX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T;

    switch (command) {
    case 0xA2:
        //LDX #imm
        REG_X = next_byte();
        break;
    case 0xB6:
        //LDX ZP,Y
        T.w = (next_byte() + REG_Y) & 0xFF;
        REG_X = read_mem(T.w);
        break;
    case 0xBE:
        //LDX ABS,Y
        T.b.L = next_byte();
        T.b.H = next_byte();
        T.w += REG_Y;
        REG_X = read_mem(T.w);
        break;
    default:
        REG_X = get_operand(command, cycles);
        break;
    }
    calc_flags(REG_X, F_NZ);
}

void mos6502core::_LDY(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T;

    switch (command) {
    case 0xA0:
        //LDY #imm
        REG_Y = next_byte();
        break;
    default:
        REG_Y = get_operand(command, cycles);
        break;
    }
    calc_flags(REG_Y, F_NZ);
}

void mos6502core::_LSR(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    if (command == 0x4A) {
        //LSR A
        set_flag(F_C, REG_A);
        REG_A >>= 1;
        calc_flags(REG_A, F_NZ);
    } else {
        //LSR mem
        T.w = get_address(command, cycles);
        D.b.L = read_mem(T.w);
        set_flag(F_C, D.b.L);
        D.b.L >>= 1;
        calc_flags(D.b.L, F_NZ);
        write_mem(T.w, D.b.L);
    }
}

void mos6502core::_NOP(uint8_t command, unsigned int & cycles)
{
    // Chilling here a bit
}

inline void mos6502core::doORA(uint8_t v)
{
    REG_A |= v;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_ORA(uint8_t command, unsigned int & cycles)
{
    doORA(get_operand(command, cycles));
}

void mos6502core::_PHA(uint8_t command, unsigned int & cycles)
{
    write_mem(0x100+REG_S--, REG_A);
}

void mos6502core::_PHP(uint8_t command, unsigned int & cycles)
{
    //https://www.nesdev.org/the%20'B'%20flag%20&%20BRK%20instruction.txt
    write_mem(REG_S+0x100, REG_P | F_B);
    REG_S--;
}

void mos6502core::_PLA(uint8_t command, unsigned int & cycles)
{
    REG_A = read_mem(++REG_S+0x100);
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_PLP(uint8_t command, unsigned int & cycles)
{
    //https://www.nesdev.org/the%20'B'%20flag%20&%20BRK%20instruction.txt
    REG_S++;
    REG_P = (read_mem(REG_S+0x100) & ~F_B) | FLAG_B | F_P5;
}

void mos6502core::_ROL(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    if (command == 0x2A) {
        //ROL A
        D.w = (static_cast<uint16_t>(REG_A) << 1) | FLAG_C;
        set_flag(F_C, D.b.H);
        REG_A = D.b.L;
        calc_flags(REG_A, F_NZ);
    } else {
        //ROL mem
        T.w = get_address(command, cycles);
        D.w = (static_cast<uint16_t>(read_mem(T.w)) << 1) | FLAG_C;
        set_flag(F_C, D.b.H);
        calc_flags(D.b.L, F_NZ);
        write_mem(T.w, D.b.L);
    }
}

void mos6502core::_ROR(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    if (command == 0x6A) {
        //ROR A
        D.b.H = FLAG_C;
        D.b.L = REG_A;
        set_flag(F_C, D.b.L);
        REG_A = (D.w >> 1) & 0xFF;
        calc_flags(REG_A, F_NZ);
    } else {
        //ROR mem
        T.w = get_address(command, cycles);
        D.b.H = FLAG_C;
        D.b.L = read_mem(T.w);
        set_flag(F_C, D.b.L);
        D.b.L = (D.w >> 1) & 0xFF;
        calc_flags(D.b.L, F_NZ);
        write_mem(T.w, D.b.L);
    }
}

void mos6502core::_RTI(uint8_t command, unsigned int & cycles)
{
    REG_P = (read_mem(++REG_S+0x100) & ~F_B) | FLAG_B | F_P5;
    REG_PCL = read_mem(++REG_S+0x100);
    REG_PCH = read_mem(++REG_S+0x100);
}

void mos6502core::_RTS(uint8_t command, unsigned int & cycles)
{
    REG_PCL = read_mem(++REG_S+0x100);
    REG_PCH = read_mem(++REG_S+0x100);
    REG_PC++;
}

inline void mos6502core::doSBC(uint8_t v)
{
    PartsRecLE T, D;
    T.w = v;
    if (FLAG_D == 0) {
        // Binary mode
        T.w ^= 0xFF;
        D.w = REG_A + T.b.L + FLAG_C;
        calc_flags(D.w, F_NZC);
        set_flag(F_V, ((REG_A ^ D.b.L) & (T.b.L ^ D.b.L)) >> 1);
        REG_A = D.b.L;
    } else {
        int16_t A;
        int8_t AL = static_cast<int8_t>(REG_A & 0x0F) - static_cast<int8_t>(T.b.L & 0x0F) + static_cast<int8_t>(FLAG_C) - 1;
        if (context.type == MOS_6502_FAMILY_BASIC) {
            if (AL < 0) AL = ((AL - 0x06) & 0x0F) - 0x10;
            A = (REG_A & 0xF0) - (T.b.L & 0xF0) + AL;
            if (A < 0) A = A - 0x60;
        } else {
            A = (REG_A & 0xF0) - (T.b.L & 0xF0) + AL;
            if (A < 0) A -= 0x60;
            if (AL < 0) A -= 0x06;
        }
        T.w ^= 0xFF;
        D.w = REG_A + T.b.L + FLAG_C;
        calc_flags(D.w, F_C);
        set_flag(F_V, ((REG_A ^ D.b.L) & (T.b.L ^ D.b.L)) >> 1);
        REG_A = A & 0xFF;
        if (context.type == MOS_6502_FAMILY_BASIC)
            calc_flags(D.w, F_NZ);
        else
            calc_flags(REG_A, F_NZ);
    }

}

void mos6502core::_SBC(uint8_t command, unsigned int & cycles)
{
    doSBC(get_operand(command, cycles));
}

void mos6502core::_SEC(uint8_t command, unsigned int & cycles)
{
    set_flag(F_C, F_C);
}

void mos6502core::_SED(uint8_t command, unsigned int & cycles)
{
    set_flag(F_D, F_D);
}

void mos6502core::_SEI(uint8_t command, unsigned int & cycles)
{
    set_flag(F_I, F_I);
}

inline void mos6502core::doSTA(uint16_t address)
{
    write_mem(address, REG_A);
}

void mos6502core::_STA(uint8_t command, unsigned int & cycles)
{
    doSTA(get_address(command, cycles));
}

void mos6502core::_STX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T;

    switch (command) {
    case 0x96:
        //STX ZP,Y
        T.w = (next_byte() + REG_Y) & 0xFF;
        break;
    default:
        T.w = get_address(command, cycles);
        break;
    }
    write_mem(T.w, REG_X);
}

void mos6502core::_STY(uint8_t command, unsigned int & cycles)
{
    write_mem(get_address(command, cycles), REG_Y);
}

void mos6502core::_TAX(uint8_t command, unsigned int & cycles)
{
    REG_X = REG_A;
    calc_flags(REG_X, F_NZ);
}

void mos6502core::_TAY(uint8_t command, unsigned int & cycles)
{
    REG_Y = REG_A;
    calc_flags(REG_Y, F_NZ);
}

void mos6502core::_TSX(uint8_t command, unsigned int & cycles)
{
    REG_X = REG_S;
    calc_flags(REG_X, F_NZ);
}

void mos6502core::_TXA(uint8_t command, unsigned int & cycles)
{
    REG_A = REG_X;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_TXS(uint8_t command, unsigned int & cycles)
{
    REG_S = REG_X;
}

void mos6502core::_TYA(uint8_t command, unsigned int & cycles)
{
    REG_A = REG_Y;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__ANE(uint8_t command, unsigned int & cycles)
{
    REG_A &= next_byte();
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__ANC(uint8_t command, unsigned int & cycles)
{
    __ANE(0x8B, cycles);
    _ASL(0x0A, cycles);
}

void mos6502core::__ANC2(uint8_t command, unsigned int & cycles)
{
    __ANE(0x8B, cycles);
    _ROL(0x2A, cycles);
}

void mos6502core::__ARR(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    T.w = get_operand(command, cycles);
    if (FLAG_B == 0) {
        // Binary mode
        __ANE(0x8B, cycles);
        _ROR(0x6A, cycles);
        set_flag(F_N + F_V, REG_A);
        calc_flags(REG_A, F_Z);
        set_flag(F_C, ((REG_A  >> 1) ^ REG_A) >> 5);
    } else {
        // BCD mode
        //TODO: 6502 BCD ADC
    }
}

void mos6502core::__ASR(uint8_t command, unsigned int & cycles)
{
    __ANE(0x8B, cycles);
    _LSR(0x4A, cycles);
}

void mos6502core::__DCP(uint8_t command, unsigned int & cycles)
{
    //DEC
    PartsRecLE T, D, S;
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w);
    write_mem(T.w, D.b.L - 1);

    //CMP
    S.w = static_cast<uint16_t>(REG_A) - T.w;
    calc_flags(S.w, F_NZC);
}

void mos6502core::__ISB(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D, S;
    //INC
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w);
    write_mem(T.w, D.b.L + 1);

    //SBC
    if (FLAG_B == 0) {
        // Binary mode
        S.w = REG_A - D.b.L - FLAG_C;
        calc_flags(S.w, F_NZC);
        set_flag(F_V, (REG_A ^ D.b.L ^ S.b.L) >> 1);
        REG_A = S.b.L;
    } else {
        // BCD mode
        //TODO: 6502 BCD ADC
    }
}

void mos6502core::__LAS(uint8_t command, unsigned int & cycles)
{
    PartsRecLE A;
    A.b.L = next_byte();
    A.b.H = next_byte();
    REG_A = read_mem(A.w) & REG_S;
    REG_S = REG_A;
    REG_X = REG_A;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__LAX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    switch(command) {
        case 0xB7:
            //zp, Y !!!!!!!!!
            T.w = (static_cast<uint16_t>(next_byte()) + REG_Y) & 0xFF;
            D.b.L = read_mem(T.w);
            break;
        case 0xBF:
            //abs, Y !!!!!!!!!
            T.b.L = next_byte();
            T.b.H = next_byte();
            T.w += REG_Y;
            D.b.L = read_mem(T.w);
            break;
        default:
            D.b.L = get_operand(command, cycles);
            break;
    }
    REG_A = REG_X = D.b.L;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__LXA(uint8_t command, unsigned int & cycles)
{
    REG_A = REG_X = next_byte();
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__RLA(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D, S;
    // ROL mem
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w);
    S.w = (D.w << 1) | FLAG_C;
    set_flag(F_C, S.b.H);
    calc_flags(S.b.L, F_NZ);
    write_mem(T.w, S.b.L);

    //AND mem
    REG_A &= D.b.L;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__RRA(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D, S;
    //ROR mem
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w);
    S.b.H = FLAG_C;
    S.b.L = D.b.L;
    set_flag(F_C, S.b.L);
    S.b.L = (S.w >> 1) & 0xFF;
    calc_flags(S.b.L, F_NZ);
    write_mem(T.w, S.b.L);

    //ADC mem
    S.w = REG_A + D.w + FLAG_C;                 //TODO: check which C should be here
    calc_flags(S.w, F_NZC);
    set_flag(F_V, (REG_A ^ D.b.L ^ S.b.L) >> 1);
    REG_A = S.b.L;
}

void mos6502core::__SAX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE A;
    switch (command) {
        case 0x97:
            //SAX zp,Y
            A.w = (next_byte() + REG_Y) & 0xFF;
            break;
        default:
            A.w = get_address(command, cycles);
            break;
    };
    write_mem(A.w, REG_A & REG_X);
}

void mos6502core::__SBC(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    T.w = next_byte();
    D.w = REG_A - T.b.L - FLAG_C;
    calc_flags(REG_A, F_NZC);
    set_flag(F_V, (REG_A ^ T.b.L ^ D.b.L) >> 1);
    REG_A = D.b.L;
}


void mos6502core::__SBX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE D;
    uint8_t v = next_byte();
    D.w = (REG_A & REG_X) - v;
    REG_X = D.b.L;
    calc_flags(D.w, F_NZC);
}

void mos6502core::__SHA(uint8_t command, unsigned int & cycles)
{
    PartsRecLE A;
    switch (command) {
    case 0x9F:
        //SHA abs,Y
        A.b.L = next_byte();
        A.b.H = next_byte();
        A.w += REG_Y;
        break;
    default:
        A.w = get_address(command, cycles);
        break;
    };
    write_mem(A.w, REG_A & REG_X & (A.b.H + 1));
}

void mos6502core::__SHS(uint8_t command, unsigned int & cycles)
{
    PartsRecLE A;
    A.w = get_address(command, cycles);
    write_mem(A.w, REG_S & (A.b.H + 1));
    REG_S = REG_A & REG_Y;
}

void mos6502core::__SHX(uint8_t command, unsigned int & cycles)
{
    PartsRecLE A;
    A.b.L = next_byte();
    A.b.H = next_byte();
    A.w += REG_Y;
    write_mem(A.w, REG_X & (A.b.H + 1));
}

void mos6502core::__SHY(uint8_t command, unsigned int & cycles)
{
    PartsRecLE A;
    A.w = get_address(command, cycles);
    write_mem(A.w, REG_Y & (A.b.H + 1)); //TODO: check the right index register
}

void mos6502core::__SLO(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D, S;
    // ASL mem
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w);
    S.w = D.w << 1;
    calc_flags(S.w, F_NZC);
    write_mem(T.w, S.b.L);

    //ORA
    REG_A |= D.b.L;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__SRE(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D, S;
    //LSR mem
    T.w = get_address(command, cycles);
    D.b.L = read_mem(T.w);
    set_flag(F_C, D.b.L);
    S.b.L = D.b.L >> 1;
    calc_flags(S.b.L, F_NZ);
    write_mem(T.w, S.b.L);

    //XOR
    REG_A ^= D.b.L;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::__NOP(uint8_t command, unsigned int & cycles)
{
    get_operand(command, cycles);
}

void mos6502core::__KILL(uint8_t command, unsigned int & cycles)
{
    //TODO: 6502 Incorrect, hanging up
}

void mos6502core::_IRQ(uint8_t command, unsigned int & cycles)
{
    write_mem(0x100+REG_S--, REG_PCH);
    write_mem(0x100+REG_S--, REG_PCL);
    write_mem(0x100+REG_S--, REG_P & ~F_B);
    set_flag(F_I, F_I);
    if (context.type == MOS_6502_FAMILY_65C02)
        set_flag(F_D, 0);
    REG_PCL = read_mem(0xFFFE);
    REG_PCH = read_mem(0xFFFF);
    cycles += 7;
}

void mos6502core::_NMI(uint8_t command, unsigned int & cycles)
{
    write_mem(0x100+REG_S--, REG_PCH);
    write_mem(0x100+REG_S--, REG_PCL);
    write_mem(0x100+REG_S--, REG_P & ~F_B);
    set_flag(F_I, F_I);
    if (context.type == MOS_6502_FAMILY_65C02)
        set_flag(F_D, 0);
    REG_PCL = read_mem(0xFFFA);
    REG_PCH = read_mem(0xFFFB);
    cycles += 7;
}

//65c02

void mos6502core::_NOPc02(uint8_t command, unsigned int & cycles)
{
    switch(command){
        case 0x5C:
        case 0xDC:
        case 0xFC:
            // 3 byte NOPs
            next_byte();
            next_byte();
            break;
        case 0x02:
        case 0x22:
        case 0x42:
        case 0x62:
        case 0x82:
        case 0xC2:
        case 0xE2:
        case 0x44:
        case 0x54:
        case 0xD4:
        case 0xF4:
            // 2 byte NOPs
            next_byte();
            break;
        default:
            // 1 byte NOPs
            break;
    }
}

void mos6502core::_ADCc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 ADC (zp)
    doADC(read_mem(next_byte()));
}

void mos6502core::_ANDc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 AND (zp)
    doAND(read_mem(next_byte()));
}

void mos6502core::_CMPc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 CMP (zp)
    doCMP(read_mem(next_byte()));
}

void mos6502core::_DEAc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 DEA
    REG_A -= 1;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_INAc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 INA
    REG_A += 1;
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_EORc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 EOR (zp)
    doEOR(read_mem(next_byte()));
}

void mos6502core::_LDAc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 LDA (zp)
    doLDA(read_mem(next_byte()));
}

void mos6502core::_ORAc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 ORA (zp)
    doORA(read_mem(next_byte()));
}

void mos6502core::_SBCc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 SBC (zp)
    doSBC(read_mem(next_byte()));
}

void mos6502core::_STAc02(uint8_t command, unsigned int & cycles)
{
    // 65c02 STA (zp)
    PartsRecLE T, D;
    T.w = next_byte();
    D.b.L = read_mem(T.w);
    if (T.b.L == 0xFF)
        D.b.H = read_mem(0);
    else
        D.b.H = read_mem(T.w+1);
    doSTA(D.w);
}

void mos6502core::_BRA(uint8_t command, unsigned int & cycles)
{
    REG_PC += static_cast<int8_t>(next_byte());
}

void mos6502core::_PHX(uint8_t command, unsigned int & cycles)
{
    write_mem(0x100+REG_S--, REG_X);
}

void mos6502core::_PLX(uint8_t command, unsigned int & cycles)
{
    REG_X = read_mem(++REG_S+0x100);
    calc_flags(REG_X, F_NZ);
}

void mos6502core::_PHY(uint8_t command, unsigned int & cycles)
{
    write_mem(0x100+REG_S--, REG_Y);
}

void mos6502core::_PLY(uint8_t command, unsigned int & cycles)
{
    REG_Y = read_mem(++REG_S+0x100);
    calc_flags(REG_Y, F_NZ);
}

void mos6502core::_STZ(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T;
    switch (command) {
    case 0x9C:
        T.w = get_address(3 << 2, cycles); // abs
        break;
    case 0x9E:
        T.w = get_address(7 << 2, cycles); // abs, X
        break;
    default:
        T.w = get_address(command, cycles); // zp; zp,x
        break;
    }
    write_mem(T.w, 0);
}

void mos6502core::_TRB(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    if (command == 0x14) {
        // zp
        T.w = next_byte();
    } else
    if (command == 0x1C) {
        // abs
        T.b.L = next_byte();
        T.b.H = next_byte();
    } else {
        // TODO: Error
        T.w = 0;    // To avoid warnings
    }
    D.b.L = read_mem(T.w);
    write_mem(T.w, D.b.L & ~REG_A);
    calc_flags(D.b.L & REG_A, F_Z);
}

void mos6502core::_TSB(uint8_t command, unsigned int & cycles)
{
    PartsRecLE T, D;
    if (command == 0x04) {
        // zp
        T.w = next_byte();
    } else
    if (command == 0x0C) {
        // abs
        T.b.L = next_byte();
        T.b.H = next_byte();
    } else {
        // TODO: Error
        T.w = 0;    // To avoid warnings
    }
    D.b.L = read_mem(T.w);
    write_mem(T.w, D.b.L | REG_A);
    calc_flags(D.b.L & REG_A, F_Z);
}

void mos6502core::_BBR(uint8_t command, unsigned int & cycles)
{
    uint8_t bit = (command >> 4) & 0x07;
    uint8_t v = read_mem(next_byte());
    uint8_t offset = next_byte();
    if ((v & (1 << bit)) == 0) {
        cycles++;
        uint16_t new_pc = REG_PC + static_cast<int8_t>(offset);
        if (((new_pc ^ REG_PC) & 0xFF00) != 0) cycles++;            // Branch to a different page
        REG_PC = new_pc;
    }
}

void mos6502core::_BBS(uint8_t command, unsigned int & cycles)
{
    uint8_t bit = (command >> 4) & 0x07;
    uint8_t v = read_mem(next_byte());
    uint8_t offset = next_byte();
    if ((v & (1 << bit)) != 0) {
        cycles++;
        uint16_t new_pc = REG_PC + static_cast<int8_t>(offset);
        if (((new_pc ^ REG_PC) & 0xFF00) != 0) cycles++;            // Branch to a different page
        REG_PC = new_pc;
    }
}

void mos6502core::_RMB(uint8_t command, unsigned int & cycles)
{
    uint8_t bit = (command >> 4) & 0x07;
    uint16_t address = next_byte();
    uint8_t v = read_mem(address) & ~(1 << bit);
    write_mem(address, v);
}

void mos6502core::_SMB(uint8_t command, unsigned int & cycles)
{
    uint8_t bit = (command >> 4) & 0x07;
    uint16_t address = next_byte();
    uint8_t v = read_mem(address) | (1 << bit);
    write_mem(address, v);
}

void mos6502core::_STP(uint8_t command, unsigned int & cycles)
{
    context.stop = true;
}

void mos6502core::_WAI(uint8_t command, unsigned int & cycles)
{
    context.wait = true;
}


unsigned int mos6502core::execute()
{
    if (context.stop) return 5;

    unsigned int cycles = 0;

    if (context.is_nmi) {
        context.is_nmi = false;
        context.wait = false;
        _NMI(0, cycles);
    } else
    if (context.is_irq && (FLAG_I == 0)) {
        context.wait = false;
        _IRQ(0, cycles);
    } else {
        if (context.wait) return 5;
        uint8_t command = next_byte();
        cycles += MOS6502_TIMES[command];
        (this->*(commands[command]))(command, cycles);
    }
    return cycles;
}
