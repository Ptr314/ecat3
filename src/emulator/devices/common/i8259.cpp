// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8259 (КР580ВН59) programmable interrupt controller

#include "i8259.h"

#define CALLBACK_IR     1
#define CALLBACK_INTA   2

I8259::I8259(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 1, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_ir(this, im, 8, "ir", MODE_R, CALLBACK_IR)
    , i_int(this, im, 1, "int", MODE_W)
    , i_inta(this, im, 1, "inta", MODE_R, CALLBACK_INTA)
{
    init();
}

void I8259::reset(const bool cold)
{
    AddressableDevice::reset(cold);
    init();
}

void I8259::init()
{
    memset(&ICW, 0, sizeof(ICW));
    memset(&OCW, 0, sizeof(OCW));
    IMR = 0xFF;         // All interrupts masked
    IRR = 0;
    ISR = 0;
    icw_step = 0;
    initialized = false;
    read_isr = false;
    i_int.change(0);
}

int I8259::get_highest_priority(uint8_t reg)
{
    for (int i = 0; i < 8; i++) {
        if (reg & (1 << i)) return i;
    }
    return -1;
}

void I8259::evaluate_interrupt()
{
    uint8_t active = IRR & ~IMR;
    if (active != 0) {
        int highest = get_highest_priority(active);
        int in_service = get_highest_priority(ISR);
        if (highest >= 0 && (in_service < 0 || highest < in_service)) {
            i_int.change(1);
            return;
        }
    }
    i_int.change(0);
}

unsigned int I8259::get_value(unsigned int address)
{
    if ((address & 1) == 0) {
        // A0=0: read IRR or ISR
        return read_isr ? ISR : IRR;
    } else {
        // A0=1: read IMR
        return IMR;
    }
}

void I8259::set_value(const unsigned address, const unsigned value, bool force)
{
    if ((address & 1) == 0) {
        // A0=0
        if (value & 0x10) {
            // ICW1
            ICW[0] = value;
            icw_step = 1;
            initialized = false;
            IMR = 0;
            ISR = 0;
            IRR = 0;
            read_isr = false;
            i_int.change(0);
        } else if (value & 0x08) {
            // OCW3
            OCW[2] = value;
            if (value & 0x02) {
                read_isr = (value & 0x01) != 0;
            }
        } else {
            // OCW2
            OCW[1] = value;
            unsigned int cmd = (value >> 5) & 0x07;
            unsigned int level = value & 0x07;
            switch (cmd) {
                case 1: // Non-specific EOI
                {
                    int highest = get_highest_priority(ISR);
                    if (highest >= 0) ISR &= ~(1 << highest);
                    break;
                }
                case 3: // Specific EOI
                    ISR &= ~(1 << level);
                    break;
                default:
                    break;
            }
            evaluate_interrupt();
        }
    } else {
        // A0=1
        if (!initialized) {
            // ICW sequence
            ICW[icw_step] = value;
            icw_step++;
            bool need_icw4 = (ICW[0] & 0x01) != 0;
            if (icw_step == 2 && !need_icw4) {
                initialized = true;
            } else if (icw_step == 3 && need_icw4) {
                initialized = true;
            } else if (icw_step > 3) {
                initialized = true;
            }
        } else {
            // OCW1: set IMR
            OCW[0] = value;
            IMR = value;
            evaluate_interrupt();
        }
    }
}

void I8259::interface_callback(MAYBE_UNUSED unsigned callback_id, const unsigned new_value, const unsigned old_value)
{
    if (callback_id == CALLBACK_IR) {
        // Detect rising edges on IR lines
        uint8_t rising = (new_value & ~old_value) & 0xFF;
        IRR |= rising;
        evaluate_interrupt();
    } else if (callback_id == CALLBACK_INTA) {
        // Interrupt acknowledge: rising edge
        if ((new_value & 1) && !(old_value & 1)) {
            int highest = get_highest_priority(IRR & ~IMR);
            if (highest >= 0) {
                ISR |= (1 << highest);
                IRR &= ~(1 << highest);
                // Place vector on data bus
                uint8_t vector = (ICW[1] & 0xF8) | highest;
                i_data.change(vector);
            }
            i_int.change(0);
        }
    }
}

ComputerDevice * create_i8259(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8259(im, cd);
}