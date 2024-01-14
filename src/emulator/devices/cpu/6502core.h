#ifndef MOS6502CORE_H
#define MOS6502CORE_H

#include <QObject>

#define MOS_6502_FAMILY_BASIC  0
#define MOS_6502_FAMILY_65C02  1

#pragma pack(1)

struct mos6502context
{
    uint8_t A, X, Y, P, S;

    union {
        struct{
            uint8_t  PCL, PCH;
        } r8;
        struct {
            uint16_t PC;
        } r16;
    };
    int type;
    bool is_nmi;
    bool is_irq;
    bool stop;
    bool wait;
};

namespace MOS6502
{
const uint8_t F_C   = 1;        //0x01
const uint8_t F_Z   = (1 << 1); //0x02
const uint8_t F_I   = (1 << 2); //0x04
const uint8_t F_D   = (1 << 3); //0x08
const uint8_t F_B   = (1 << 4); //0x10
const uint8_t F_P5  = (1 << 5); //0x20
const uint8_t F_V   = (1 << 6); //0x40
const uint8_t F_N   = (1 << 7); //0x80

const uint8_t F_ALL = 0xFF;
const uint8_t F_NZC = F_N + F_Z + F_C;
const uint8_t F_NZ = F_N + F_Z;
}

#pragma pack()

class mos6502core
{
protected:
    typedef void (mos6502core::*_OPER_FUNC)(uint8_t, unsigned int &);

    _OPER_FUNC commands[256];

    void doADC(uint8_t v);
    void doAND(uint8_t v);
    void doCMP(uint8_t v);
    void doEOR(uint8_t v);
    void doLDA(uint8_t v);
    void doORA(uint8_t v);
    void doSBC(uint8_t v);
    void doSTA(uint16_t address);

    // Documented
    void _ADC(uint8_t command, unsigned int & cycles);
    void _AND(uint8_t command, unsigned int & cycles);
    void _ASL(uint8_t command, unsigned int & cycles);
    void _BRANCH(uint8_t command, unsigned int & cycles);
    void _BIT(uint8_t command, unsigned int & cycles);
    void _BRK(uint8_t command, unsigned int & cycles);
    void _CLC(uint8_t command, unsigned int & cycles);
    void _CLD(uint8_t command, unsigned int & cycles);
    void _CLI(uint8_t command, unsigned int & cycles);
    void _CLV(uint8_t command, unsigned int & cycles);
    void _CMP(uint8_t command, unsigned int & cycles);
    void _CPX(uint8_t command, unsigned int & cycles);
    void _CPY(uint8_t command, unsigned int & cycles);
    void _DEC(uint8_t command, unsigned int & cycles);
    void _DEX(uint8_t command, unsigned int & cycles);
    void _DEY(uint8_t command, unsigned int & cycles);
    void _EOR(uint8_t command, unsigned int & cycles);
    void _INC(uint8_t command, unsigned int & cycles);
    void _INX(uint8_t command, unsigned int & cycles);
    void _INY(uint8_t command, unsigned int & cycles);
    void _JMP(uint8_t command, unsigned int & cycles);
    void _JSR(uint8_t command, unsigned int & cycles);
    void _LDA(uint8_t command, unsigned int & cycles);
    void _LDX(uint8_t command, unsigned int & cycles);
    void _LDY(uint8_t command, unsigned int & cycles);
    void _LSR(uint8_t command, unsigned int & cycles);
    void _NOP(uint8_t command, unsigned int & cycles);
    void _ORA(uint8_t command, unsigned int & cycles);
    void _PHA(uint8_t command, unsigned int & cycles);
    void _PHP(uint8_t command, unsigned int & cycles);
    void _PLA(uint8_t command, unsigned int & cycles);
    void _PLP(uint8_t command, unsigned int & cycles);
    void _ROL(uint8_t command, unsigned int & cycles);
    void _ROR(uint8_t command, unsigned int & cycles);
    void _RTI(uint8_t command, unsigned int & cycles);
    void _RTS(uint8_t command, unsigned int & cycles);
    void _SBC(uint8_t command, unsigned int & cycles);
    void _SEC(uint8_t command, unsigned int & cycles);
    void _SED(uint8_t command, unsigned int & cycles);
    void _SEI(uint8_t command, unsigned int & cycles);
    void _STA(uint8_t command, unsigned int & cycles);
    void _STX(uint8_t command, unsigned int & cycles);
    void _STY(uint8_t command, unsigned int & cycles);
    void _TAX(uint8_t command, unsigned int & cycles);
    void _TAY(uint8_t command, unsigned int & cycles);
    void _TSX(uint8_t command, unsigned int & cycles);
    void _TXA(uint8_t command, unsigned int & cycles);
    void _TXS(uint8_t command, unsigned int & cycles);
    void _TYA(uint8_t command, unsigned int & cycles);

    // Undocumented
    void __ANE(uint8_t command, unsigned int & cycles);
    void __ANC(uint8_t command, unsigned int & cycles);
    void __ANC2(uint8_t command, unsigned int & cycles);
    void __ARR(uint8_t command, unsigned int & cycles);
    void __ASR(uint8_t command, unsigned int & cycles);
    void __DCP(uint8_t command, unsigned int & cycles);
    void __ISB(uint8_t command, unsigned int & cycles);
    void __LAS(uint8_t command, unsigned int & cycles);
    void __LAX(uint8_t command, unsigned int & cycles);
    void __LXA(uint8_t command, unsigned int & cycles);
    void __RLA(uint8_t command, unsigned int & cycles);
    void __RRA(uint8_t command, unsigned int & cycles);
    void __SAX(uint8_t command, unsigned int & cycles);
    void __SBX(uint8_t command, unsigned int & cycles);
    void __SHA(uint8_t command, unsigned int & cycles);
    void __SHS(uint8_t command, unsigned int & cycles);
    void __SHX(uint8_t command, unsigned int & cycles);
    void __SHY(uint8_t command, unsigned int & cycles);
    void __SLO(uint8_t command, unsigned int & cycles);
    void __SRE(uint8_t command, unsigned int & cycles);
    void __SBC(uint8_t command, unsigned int & cycles);
    void __KILL(uint8_t command, unsigned int & cycles);
    void __NOP(uint8_t command, unsigned int & cycles);

    void _IRQ(uint8_t command, unsigned int & cycles);
    void _NMI(uint8_t command, unsigned int & cycles);

    //65c02
    void _BRA(uint8_t command, unsigned int & cycles);
    void _PHX(uint8_t command, unsigned int & cycles);
    void _PLX(uint8_t command, unsigned int & cycles);
    void _PHY(uint8_t command, unsigned int & cycles);
    void _PLY(uint8_t command, unsigned int & cycles);
    void _STZ(uint8_t command, unsigned int & cycles);
    void _TRB(uint8_t command, unsigned int & cycles);
    void _TSB(uint8_t command, unsigned int & cycles);
    void _BBR(uint8_t command, unsigned int & cycles);
    void _BBS(uint8_t command, unsigned int & cycles);
    void _RMB(uint8_t command, unsigned int & cycles);
    void _SMB(uint8_t command, unsigned int & cycles);
    void _STP(uint8_t command, unsigned int & cycles);
    void _WAI(uint8_t command, unsigned int & cycles);
    void _NOPc02(uint8_t command, unsigned int & cycles);

    void _ADCc02(uint8_t command, unsigned int & cycles);
    void _ANDc02(uint8_t command, unsigned int & cycles);
    void _CMPc02(uint8_t command, unsigned int & cycles);
    void _DEAc02(uint8_t command, unsigned int & cycles);
    void _INAc02(uint8_t command, unsigned int & cycles);
    void _EORc02(uint8_t command, unsigned int & cycles);
    void _LDAc02(uint8_t command, unsigned int & cycles);
    void _ORAc02(uint8_t command, unsigned int & cycles);
    void _SBCc02(uint8_t command, unsigned int & cycles);
    void _STAc02(uint8_t command, unsigned int & cycles);

    mos6502context context;
    uint8_t next_byte();
    uint8_t read_command();
    void init_commands_6502();
    void init_commands_65c02();
    uint8_t get_operand(uint8_t command, unsigned int & cycles);
    uint16_t get_address(uint8_t command, unsigned int & cycles);
    void calc_flags(uint32_t value, uint32_t mask);
    void set_flag(uint8_t flag, uint8_t value);

    uint16_t get_address_zp(unsigned int & cycles);

public:
    mos6502core(int family_type);
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual void reset();
    virtual mos6502context * get_context();
    virtual uint16_t get_pc();
    uint8_t get_command();
    virtual void set_nmi(bool nmi_val);
    virtual void set_irq(bool irq_val);

    unsigned int execute();
};

#endif // MOS6502CORE_H
