// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8251 (КР580ВВ51) programmable communication interface (USART)

#pragma once

#include "emulator/core.h"

class I8251:public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_txrdy;          // Transmitter ready output
    Interface i_rxrdy;          // Receiver ready output
    Interface i_txe;            // Transmitter empty output
    Interface i_dtr;            // Data Terminal Ready output
    Interface i_rts;            // Request To Send output
    Interface i_dsr;            // Data Set Ready input
    Interface i_cts;            // Clear To Send input
    Interface i_syndet;         // Sync detect
    Interface i_txd;            // Transmitted data output (byte-wide for emulation)
    Interface i_rxd;            // Received data input (byte-wide for emulation)
    Interface i_txc;            // Transmitter clock input
    Interface i_rxc;            // Receiver clock input
    Interface i_c;              // External clock input (alternative to clock() method)

    // Mode word fields
    uint8_t mode_word;
    bool    sync_mode;          // true=synchronous, false=asynchronous
    uint8_t baud_factor;        // Async: 1, 16, 64
    uint8_t char_length;        // 5, 6, 7, 8
    bool    parity_enable;
    bool    even_parity;
    uint8_t stop_bits;          // Async: 1, 1.5, 2 (stored as 1, 2, 3)
    uint8_t sync_chars[2];      // Sync mode: sync characters
    bool    single_sync;        // true=1 sync char, false=2

    // Command word fields
    uint8_t command_word;
    bool    tx_enable;
    bool    rx_enable;
    bool    send_break;
    bool    hunt_mode;

    // Control port state machine
    enum ControlState {
        STATE_MODE,             // Expecting mode word (after reset)
        STATE_SYNC_CHAR,        // Expecting sync character(s)
        STATE_COMMAND           // Expecting command word (normal operation)
    };
    ControlState control_state;
    uint8_t sync_chars_remaining; // Sync chars still to load

    // Status
    uint8_t status;

    // Data buffers
    uint8_t tx_buffer;
    uint8_t rx_buffer;
    bool    tx_buffer_full;
    bool    rx_buffer_full;

    // Baud rate clock dividers
    unsigned int tx_clock_count;
    unsigned int rx_clock_count;
    unsigned int tx_clock_divider;
    unsigned int rx_clock_divider;

    bool ext_clock;             // true if i_c interface is connected (auto-detected)

    void init();
    void do_clock(unsigned int counter);
    void update_status();
    void update_command(uint8_t value);
    void parse_mode_word(uint8_t value);
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

public:
    I8251(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void clock(unsigned int counter) override;
};

ComputerDevice * create_i8251(InterfaceManager *im, EmulatorConfigDevice *cd);