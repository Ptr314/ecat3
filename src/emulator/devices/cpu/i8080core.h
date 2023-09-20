#ifndef I8080CORE_H
#define I8080CORE_H

#include <cstdint>

#pragma pack(1)
struct i8080context
{
    union {
        struct{
            uint8_t  C,B,E,D,H,L,
                     m,
                     A, F;
            uint16_t SP, PC, PC2;
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

union PartsRecLE {
    struct{
        uint8_t L, H;
    } b;
    uint16_t w;
    uint32_t dw;
};

#pragma pack()

#define F_CARRY         0x01
#define F_PARITY        0x04
#define F_HALF_CARRY    0x10
#define F_ZERO          0x40
#define F_SIGN          0x80
#define F_ALL           (F_CARRY + F_PARITY + F_HALF_CARRY + F_ZERO + F_SIGN)

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
                                {F_SIGN, F_SIGN}        //NERATIVE
                            };

static const uint8_t I8080LENGTHS[256] = {
                   1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,     // 00-0F
                   1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,     // 10-1F
                   1,3,3,1,1,1,2,1,1,1,3,1,1,1,2,1,     // 20-2F
                   1,3,3,1,1,1,2,1,1,1,3,1,1,1,2,1,     // 30-3F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 40-4F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 50-5F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 60-6F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 70-7F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 80-8F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // 90-9F
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // A0-AF
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,     // B0-BF
                   1,1,3,3,3,1,2,1,1,1,3,1,3,3,2,1,     // C0-CF
                   1,1,3,2,3,1,2,1,1,1,3,2,3,1,2,1,     // D0-DF
                   1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,     // E0-EF
                   1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1};    // F0-FF

class i8080core
{
private:
    uint8_t next_byte();
    uint8_t read_command();
    uint8_t calc_flags(uint32_t v1, uint32_t v2, uint32_t value);
    void do_ret();
    void do_jump();
    void do_call();

protected:
    i8080context context;

public:
    i8080core();
    virtual uint8_t read_mem(uint16_t address) = 0;
    virtual void write_mem(uint16_t address, uint8_t value) = 0;
    virtual uint8_t read_port(uint16_t address);
    virtual void write_port(uint16_t address, uint8_t value);
    virtual void inte_changed(unsigned int inte);
    virtual void reset();
    virtual i8080context * get_context();

    unsigned int execute();
};

#endif // I8080CORE_H
