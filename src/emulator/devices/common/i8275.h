#ifndef I8275_H
#define I8275_H

#include "emulator/core.h"

class I8275:public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;

    uint8_t Mode;
    int RegIndex;
    unsigned int BlinkTicks;
    unsigned int Counter;
    unsigned int SystemClock;

public:
    uint8_t RegMode[4];
    uint8_t RegCursor[2];
    bool Blinker;

    I8275(InterfaceManager *im, EmulatorConfigDevice *cd):
          AddressableDevice(im, cd)
        , Counter(0)
        , Mode(0)
        , RegIndex(0)
        , Blinker(false)
        , i_address(this, im, 1, "address", MODE_R)
        , i_data(this, im, 8, "data", MODE_R)
    {
        memset(&RegMode, 0, sizeof(RegMode));
        memset(&RegCursor, 0, sizeof(RegCursor));
    }

    virtual unsigned int get_value(unsigned int address) override
    {
        unsigned int a = address & 1;
        if (a == 0)
            return 0;       //Data
        else
            return 0x20;    //Control (0x20 - CRT beam return)
    }

    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override
    {
        unsigned int a = address & 1;
        if (a == 0)
        {
            //Data
            if (Mode==0)
            {
                //Mode 0 - set main parameters
                RegMode[RegIndex & 3] = value & 0xFF;
                RegIndex++;
            } else
            if (Mode==0x80)
            {
                //Mode 80 - set cursor position
                RegCursor[RegIndex & 1] = value & 0xFF;
                RegIndex++;
            };
        } else {
            //Control
            Mode = value & 0xFF;
            RegIndex = 0;
        }

    }

    virtual void load_config(SystemData *sd) override
    {
        SystemClock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;
        BlinkTicks = SystemClock / 2;
    }

    virtual void clock(unsigned int counter) override
    {
        this->Counter += counter;
        if (this->Counter > BlinkTicks)
        {
                this->Counter -= BlinkTicks;
                Blinker = !Blinker;
        }
    }
};

ComputerDevice * create_i8275(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8275(im, cd);
}


#endif // I8275_H
