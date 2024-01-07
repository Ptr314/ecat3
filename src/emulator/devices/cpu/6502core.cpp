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
#define FLAG_Z (REG_P & F_Z)
#define FLAG_P (REG_P & F_P)
#define FLAG_N (REG_P & F_N)
#define FLAG_V (REG_P & F_V)


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

static const uint8_t ZERO_SIGN_5[256] = {
                        F_Z+F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,    // 00-0F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 10-1F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 20-2F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 30-3F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 40-4F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 50-5F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 60-6F */
                        F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,F_P5,        // 70-7F */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // 80-8F */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // 90-9F */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // A0-AF */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // B0-BF */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // C0-CF */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // D0-DF */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,                // E0-EF */
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,
                        F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5,F_N+F_P5                 // F0-FF */
};

mos6502core::mos6502core(int family_type)
{
    memset(&context, 0, sizeof(context));
    context.type = family_type;
    init_commands();
}

void mos6502core::reset()
{
    REG_PCL = read_mem(0xFFFC);
    REG_PCH = read_mem(0xFFFD);
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

void mos6502core::init_commands()
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
    commands[0xEB] = &mos6502core::_SBC; //!!!
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

inline uint8_t mos6502core::get_operand(uint8_t command, unsigned int * cycles)
{
    PartsRecLE T, D;
    uint8_t result;

    unsigned int mode = (command >> 2) & 0x07;
    switch (mode) {
    case 0:
        //ind, x
        T.w = (next_byte() + REG_X) & 0xFF;
        D.b.L = read_mem(T.w);
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

inline uint16_t mos6502core::get_address(uint8_t command, unsigned int * cycles)
{
    PartsRecLE T, D;
    uint16_t result;
    unsigned int mode = (command >> 2) & 0x07;
    switch (mode) {
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
    uint8_t F = ZERO_SIGN_5[value & 0xFF] & ((value & 0x100) >> 8);
    REG_P = (REG_P & ~mask) | (F & mask) | F_P5;
}

inline void mos6502core::set_flag(uint8_t flag, uint8_t value)
{
    REG_P = (REG_P & ~flag) | (value & flag);
}

void mos6502core::_ADC(uint8_t command, unsigned int * cycles)
{
    PartsRecLE T, D;
    T.w = get_operand(command, cycles);
    if (FLAG_B == 0) {
        // Binary mode
        D.w = REG_A + T.b.L + FLAG_C;
        calc_flags(D.w, F_NZC);
        set_flag(F_V, (REG_A ^ T.b.L ^ D.b.L) >> 1);
        REG_A = D.b.L;
    } else {
        // BCD mode
        //TODO: 6502 BCD ADC
    }
}

void mos6502core::_AND(uint8_t command, unsigned int * cycles)
{
    REG_A &= get_operand(command, cycles);
    calc_flags(REG_A, F_NZ);
}

void mos6502core::_ASL(uint8_t command, unsigned int * cycles)
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

void mos6502core::_BRANCH(uint8_t command, unsigned int * cycles)
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

void mos6502core::_BIT(uint8_t command, unsigned int * cycles)
{
    uint8_t v = get_operand(command, cycles);
    calc_flags(REG_A & v, F_Z);
    set_flag(F_V+F_N, v);
}

void mos6502core::_BRK(uint8_t command, unsigned int * cycles)
{
    write_mem(REG_S+0x100, REG_PCH);
    REG_S--;
    write_mem(REG_S+0x100, REG_PCL);
    REG_S--;
    write_mem(REG_S+0x100, REG_P);
    REG_S--;
    set_flag(F_B+F_I, F_B+F_I);
    REG_PCL = read_mem(0xFFFE);
    REG_PCH = read_mem(0xFFFF);
}

void mos6502core::_CLC(uint8_t command, unsigned int * cycles)
{
    set_flag(F_C, 0);
}

void mos6502core::_CLD(uint8_t command, unsigned int * cycles)
{
    set_flag(F_D, 0);
}

void mos6502core::_CLI(uint8_t command, unsigned int * cycles)
{
    set_flag(F_I, 0);
}

void mos6502core::_CLV(uint8_t command, unsigned int * cycles)
{
    set_flag(F_V, 0);
}

void mos6502core::_CMP(uint8_t command, unsigned int * cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_A) - get_operand(command, cycles);
    calc_flags(D.w, F_NZC);
}

void mos6502core::_CPX(uint8_t command, unsigned int * cycles)
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
    D.w = static_cast<uint16_t>(REG_X) - T.b.L;
    calc_flags(D.w, F_NZC);
}

void mos6502core::_CPY(uint8_t command, unsigned int * cycles)
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
    D.w = static_cast<uint16_t>(REG_Y) - T.b.L;
    calc_flags(D.w, F_NZC);
}

void mos6502core::_DEC(uint8_t command, unsigned int * cycles)
{
    PartsRecLE T, D;
    T.w = get_address(command, cycles);
    D.w = read_mem(T.w) - 1;
    calc_flags(D.w, F_NZ);
    write_mem(T.w, D.b.L);
}

void mos6502core::_DEX(uint8_t command, unsigned int * cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_X) - 1;
    calc_flags(D.w, F_NZ);
    REG_X = D.b.L;
}

void mos6502core::_DEY(uint8_t command, unsigned int * cycles)
{
    PartsRecLE D;
    D.w = static_cast<uint16_t>(REG_Y) - 1;
    calc_flags(D.w, F_NZ);
    REG_Y = D.b.L;
}

unsigned int mos6502core::execute()
{
    //TODO: 6502 interrupts handling

    uint8_t command = next_byte();
    unsigned int cycles = MOS6502_TIMES[command];
    (this->*(commands[command]))(command, &cycles);
    return cycles;
}