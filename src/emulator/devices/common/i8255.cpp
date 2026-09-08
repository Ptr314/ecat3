// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8255 (КР580ВВ55) PPI device

#include <cstring>

#include "i8255.h"

#define PORT_A   1
#define PORT_B   2
#define PORT_CH  3
#define PORT_CL  4

I8255::I8255(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_address(this, im, 2, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_port_a(this, im, 8, "A", MODE_R, PORT_A)
    , i_port_b(this, im, 8, "B", MODE_R, PORT_B)
    , i_port_ch(this, im, 4, "CH", MODE_R, PORT_CH)
    , i_port_cl(this, im, 4, "CL", MODE_R, PORT_CL)
{}

void I8255::reset(bool cold)
{
    if (cold) memset(&registers, 0, sizeof(registers));
    registers[3] = 0;
    i_port_a.set_mode(MODE_R);
    i_port_b.set_mode(MODE_R);
    i_port_ch.set_mode(MODE_R);
    i_port_cl.set_mode(MODE_R);
}

unsigned int I8255::get_value(unsigned int address)
{
    uint8_t data = registers[address & 0b11];
    return data;
}

void I8255::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int n = address & 0b11;

    // This hack is used when the IC is used internally to avoid setting interfaces etc.
    if (force) {
        registers[n] = (uint8_t)value;
        return;
    }

    switch (n) {
    case 0:
        if ((registers[3] & 0x60) == 0)
        {
            if ((registers[3] & 0x10) == 0)
            {
                registers[n] = (uint8_t)value;
                i_port_a.change(value);
            }
        } else {
            im->dm->error(this, "i8255:A is in an unsupported mode");
        }
        break;
    case 1:
        if ((registers[3] & 4) == 0)
        {
            if ((registers[3] & 2) == 0)
            {
                registers[n] = (uint8_t)value;
                i_port_b.change(value);
            }
        } else {
            //im->dm->error(this, I8255::tr("i8255:B is in an unsupported mode"));
            if ((registers[3] & 2) == 0)
            {
                registers[n] = (uint8_t)value;
                i_port_b.change(value);
                // TODO: we have to set a strobe here?
            }
        }
        break;
    case 2:
        if ((registers[3] & 8) == 0)
        {
            registers[n] &= 0x0F;
            registers[n] |= (uint8_t)value & 0xF0;
            i_port_ch.change(registers[n] >> 4);
        }
        if ((registers[3] & 1) == 0)
        {
            registers[n] &= 0xF0;
            registers[n] |= (uint8_t)value & 0x0F;
            i_port_cl.change(registers[n]);
        }
        break;
    default: //3 - control register
        if ((value & 0x80) != 0)
        {
            //Set mode
            registers[n] = (uint8_t)value;
            i_port_a. set_mode( ((value & 0x10) == 0)?MODE_W:MODE_R );
            i_port_b. set_mode( ((value & 0x02) == 0)?MODE_W:MODE_R );
            i_port_ch.set_mode( ((value & 0x08) == 0)?MODE_W:MODE_R );
            i_port_cl.set_mode( ((value & 0x01) == 0)?MODE_W:MODE_R );

            //A mode word resets every output register (8255A datasheet, "Mode
            //Definition Format"), and that is not decoration: the Агат-840 boot ROM
            //clears the drive select and the motor bit of port C by programming
            //the mode and nothing else. Keeping the latch across a mode word
            //leaves a warm restart addressing the drive the previous program
            //left selected, where the recalibration loop waits for a track 00
            //that never comes
            registers[0] = registers[1] = registers[2] = 0;
            if (i_port_a. get_mode() == MODE_W) i_port_a. change(0);
            if (i_port_b. get_mode() == MODE_W) i_port_b. change(0);
            if (i_port_ch.get_mode() == MODE_W) i_port_ch.change(0);
            if (i_port_cl.get_mode() == MODE_W) i_port_cl.change(0);

            if (i_port_a. get_mode() == MODE_R) interface_callback(PORT_A, i_port_a.value, registers[0]);
            if (i_port_b. get_mode() == MODE_R) interface_callback(PORT_B, i_port_b.value, registers[1]);
            if (i_port_ch.get_mode() == MODE_R) interface_callback(PORT_CH, i_port_ch.value, registers[2] >> 4);
            if (i_port_cl.get_mode() == MODE_R) interface_callback(PORT_CL, i_port_cl.value, registers[2] & 0xF);
        } else {
            //Bitwise operations on C
            unsigned int bn = value >> 1;
            unsigned int bv = (value & 1) << bn;
            unsigned int bm = 1 << bn;
            unsigned int v = (registers[2] & ~bm) | bv;
            set_value(2, v);
        }
        break;
    }

}

void I8255::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    switch (callback_id) {
    case PORT_A: {
        unsigned int mode_a = (registers[3] >> 5) & 0b11;
        switch (mode_a) {
        case 0: // Basic I/O
            if ((registers[3] & 0x10) != 0) registers[0] = (uint8_t)new_value;
            break;
        case 1: // Strobed I/O
            // TODO: check if we have to do nothing here
            break;
        default: // Bi-directional Bus
            im->dm->error(this, "i8255:A is in an unsupported mode");
            break;
        }
        break;
    }
    case PORT_B: {
        if ((registers[3] & 0x04) == 0) {
            // Basic I/O
            if ((registers[3] & 0x02) != 0) registers[1] = (uint8_t)new_value;
        } else {
            // Strobed I/O
            // TODO: check if we have to do nothing here
            //im->dm->error(this, I8255::tr("i8255:B is in an unsupported mode"));
        }
        break;
    }
    case PORT_CH:
        if ((registers[3] & 0x08) != 0)
        {
            registers[2] &= 0x0F;
            registers[2] |= (uint8_t)new_value & 0xF0;
        }
        break;
    case PORT_CL:
        if ((registers[3] & 0x01) != 0)
        {
            registers[2] &= 0xF0;
            registers[2] |= (uint8_t)new_value & 0x0F;
        }
        break;
    default:
        im->dm->error(this, "i8255:unknown interface called");
        break;
    }

}

std::vector<DeviceFieldInfo> I8255::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"ports",   "Latched values of ports A, B and C",           false});
    r.push_back({"a",       "Port A",                                       false});
    r.push_back({"b",       "Port B",                                       false});
    r.push_back({"c",       "Port C",                                       false});
    r.push_back({"control", "Control word, raw",                            false});
    r.push_back({"dirs",    "Direction of every port decoded from it",      false});
    return r;
}

bool I8255::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "ports")
    {
        out.numeric = true;
        for (unsigned int i = 0; i < 3; i++) out.values.push_back(registers[i]);
        return true;
    }

    if (field == "a" || field == "b" || field == "c" || field == "control")
    {
        unsigned int n = 3;
        if (field == "a") n = 0;
        else if (field == "b") n = 1;
        else if (field == "c") n = 2;
        out.numeric = true;
        out.values.push_back(registers[n]);
        return true;
    }

    //A machine whose keyboard or tape does not answer is very often a port
    //programmed the wrong way round, and that is invisible in the raw word
    if (field == "dirs")
    {
        const uint8_t c = registers[3];
        out.numeric = false;
        out.text =
            std::string("A=")   + ((c & 0x10) ? "in" : "out")
                     + " B="    + ((c & 0x02) ? "in" : "out")
                     + " CH="   + ((c & 0x08) ? "in" : "out")
                     + " CL="   + ((c & 0x01) ? "in" : "out")
                     + " modeA=" + std::to_string((c >> 5) & 3)
                     + " modeB=" + std::to_string((c >> 2) & 1)
                     + ((c & 0x80) ? "" : "  (bit 7 clear: the last write was a bit set/reset)");
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_i8255(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8255(im, cd);
}
