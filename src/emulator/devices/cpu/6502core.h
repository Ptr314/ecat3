#ifndef MOS6502CORE_H
#define MOS6502CORE_H

#include <QObject>

#define MOS_6502_FAMILY_BASIC  0

#pragma pack(1)

struct mos6502context
{
    uint8_t A, X, Y, P, S;

    union {
        struct{
            uint8_t  PCL, PCH;
        } re8;
        struct {
            uint16_t PC;
        } reg16;
    };
    int type;
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
}

#pragma pack()

class mos6502core
{
private:

protected:
    mos6502context context;
    uint8_t next_byte();
    uint8_t read_command();

public:
    mos6502core(int family_type);
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual void reset();
    virtual mos6502context * get_context();
    virtual uint16_t get_pc();
    uint8_t get_command();
    virtual void set_nmi(unsigned int nmi_val);
    virtual void set_int(unsigned int int_val);

    unsigned int execute();
};

#endif // MOS6502CORE_H
