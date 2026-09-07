// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8257 (КР580ВТ57) DMA controller device

#include <cstring>

#include "i8257.h"
#include "emulator/utils.h"

I8257::I8257(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 2, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
{
    clear_registers();
}

void I8257::reset(bool cold)
{
    AddressableDevice::reset(cold);
    clear_registers();
}

void I8257::clear_registers()
{
    memset(&PtrA, 0, sizeof(PtrA));
    memset(&PtrC, 0, sizeof(PtrC));
    memset(&RgA, 0, sizeof(RgA));
    memset(&RgC, 0, sizeof(RgC));
    RgMode = 0;
    RgState = 0;
}

//The low 14 bits of the count register are the number of transfers less one,
//the top two are the transfer type
unsigned int I8257::dma_next(unsigned int channel)
{
    unsigned int n = channel & 3;
    unsigned int addr = RgA[n*2] | (RgA[n*2+1] << 8);
    unsigned int cnt  = RgC[n*2] | (RgC[n*2+1] << 8);

    unsigned int next_addr = (addr + 1) & 0xFFFF;
    unsigned int next_cnt  = (cnt & 0xC000) | ((cnt - 1) & 0x3FFF);

    if ((cnt & 0x3FFF) == 0)
    {
        //Terminal count
        RgState |= (1u << n);
        if (n == 2 && (RgMode & 0x80) != 0)
        {
            //Auto load: channel 2 restarts from the copy kept in channel 3.
            //Nothing programs channel 3 directly - a write to channel 2 is
            //duplicated there while the auto load bit is set, see set_value
            next_addr = RgA[6] | (RgA[7] << 8);
            next_cnt  = RgC[6] | (RgC[7] << 8);
        } else
        if ((RgMode & 0x40) != 0)
        {
            //TC stop
            RgMode &= ~(1u << n);
        }
    }

    RgA[n*2]   = next_addr & 0xFF;
    RgA[n*2+1] = (next_addr >> 8) & 0xFF;
    RgC[n*2]   = next_cnt & 0xFF;
    RgC[n*2+1] = (next_cnt >> 8) & 0xFF;

    return addr;
}

unsigned int I8257::get_value(unsigned int address)
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
        //Reading the status clears the terminal count flags it reports
        result = RgState;
        RgState &= ~0x0Fu;
        break;
    default:
        im->dm->error(this, "i8257: reading from an unknown register");
        break;
    }
    return result;
}

unsigned I8257::get_direct(unsigned address)
{
    //The address and count registers are read a byte at a time, and each read
    //flips the pointer to the other half. An inspection reads the half the
    //program will get next and leaves the pointer alone
    unsigned int a = address & 0x0F;
    unsigned int n = (a >> 1) & 0x03;
    switch (a) {
    case 0:
    case 2:
    case 4:
    case 6:
        return RgA[n*2 + PtrA[n]];
    case 1:
    case 3:
    case 5:
    case 7:
        return RgC[n*2 + PtrC[n]];
    case 8:
        return RgState;
    default:
        return _FFFF;
    }
}

void I8257::set_value(unsigned int address, unsigned int value, bool force)
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
        //With auto load set, what is written into channel 2 is written into
        //channel 3 as well - that copy is what channel 2 reloads from at the
        //end of a frame, and no ROM here ever programs channel 3 itself
        if (n == 2 && (RgMode & 0x80) != 0) RgA[6 + PtrA[n]] = v;
        PtrA[n] ^= 1;
        break;
    case 1:
    case 3:
    case 5:
    case 7:
        RgC[n*2 + PtrC[n]] = v;
        if (n == 2 && (RgMode & 0x80) != 0) RgC[6 + PtrC[n]] = v;
        PtrC[n] ^= 1;
        break;
    case 8:
        RgMode = v;
        break;
    default:
        im->dm->error(this, "i8257: writing to an unknown register");
        break;
    }
}

std::vector<DeviceFieldInfo> I8257::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"address", "Address registers of the four channels",    false});
    r.push_back({"count",   "Count/mode registers of the four channels", false});
    r.push_back({"mode",    "Mode set register and the status register", false});
    return r;
}

bool I8257::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //The registers are kept as low/high byte pairs, the way the guest writes
    //them, and joined back here
    if (field == "address" || field == "count")
    {
        const uint8_t * src = (field == "address")?RgA:RgC;
        out.numeric = true;
        out.width = 16;
        for (unsigned int i = 0; i < 4; i++)
            out.values.push_back(src[i*2] + (src[i*2+1] << 8));
        return true;
    }

    if (field == "mode")
    {
        out.numeric = true;
        out.width = 8;
        out.values.push_back(RgMode);
        out.values.push_back(RgState);
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_i8257(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8257(im, cd);
}
