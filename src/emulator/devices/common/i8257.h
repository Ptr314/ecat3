#ifndef I8257_H
#define I8257_H

#include "emulator/core.h"
#include "emulator/utils.h"

class I8257:public AddressableDevice
{
private:
    Interface * i_address;
    Interface * i_data;

public:
    uint8_t RgA[8];
    uint8_t RgC[8];
    uint8_t PtrA[4];        // Flip-flops to write high and low bytes alternately
    uint8_t PtrC[4];
    uint8_t RgMode;
    uint8_t RgState;

    I8257(InterfaceManager *im, EmulatorConfigDevice *cd):
        AddressableDevice(im, cd)
    {
        i_address = create_interface(2, "address", MODE_R);
        i_data =    create_interface(8, "data", MODE_R);

        memset(&PtrA, 0, sizeof(PtrA));
        memset(&PtrC, 0, sizeof(PtrC));
    }

    virtual void reset(bool cold) override
    {
        AddressableDevice::reset(cold);
        memset(&PtrA, 0, sizeof(PtrA));
        memset(&PtrC, 0, sizeof(PtrC));
    }

    virtual unsigned int get_value(unsigned int address) override
    {
        unsigned int a = address & 0x0F;
        unsigned int n = (a >> 1) & 0x03;
        unsigned int result = _FFFF; //To avoid warnings
        switch (a) {
        case 0:
        case 2:
        case 4:
        case 6:
            result = RgA[n*2 + PtrA[n]];
            PtrA[n] ^= 1;
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            result = RgC[n*2 + PtrC[n]];
            PtrC[n] ^= 1;
            break;
        case 8:
            result = RgState;
            break;
        default:
            im->dm->error(this, I8257::tr("i8257: reading from an unknown register"));
            break;
        }
        return result;
    }

    virtual void set_value(unsigned int address, unsigned int value) override
    {
        unsigned int a = address & 0x0F;
        unsigned int n = (a >> 1) & 0x03;
        uint8_t v = value & 0xFF;
        switch (a) {
        case 0:
        case 2:
        case 4:
        case 6:
            RgA[n*2 + PtrA[n]] = v;
            PtrA[n] ^= 1;
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            RgC[n*2 + PtrC[n]] = v;
            PtrC[n] ^= 1;
            break;
        case 8:
            RgMode = v;
            break;
        default:
            im->dm->error(this, I8257::tr("i8257: writing to an unknown register"));
            break;
        }
    }

};

ComputerDevice * create_i8257(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8257(im, cd);
}


#endif // I8257_H
