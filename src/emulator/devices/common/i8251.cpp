// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8251 (КР580ВВ51) programmable communication interface (USART)

#include "i8251.h"

// Status register bits
#define STATUS_TXRDY    0x01    // Transmitter ready
#define STATUS_RXRDY    0x02    // Receiver ready
#define STATUS_TXE      0x04    // Transmitter empty
#define STATUS_PE       0x08    // Parity error
#define STATUS_OE       0x10    // Overrun error
#define STATUS_FE       0x20    // Framing error
#define STATUS_SYNDET   0x40    // Sync detect / break detect
#define STATUS_DSR      0x80    // Data Set Ready (active low input, active high in status)

// Command word bits
#define CMD_TXEN        0x01    // Transmit enable
#define CMD_DTR         0x02    // Data Terminal Ready
#define CMD_RXEN        0x04    // Receive enable
#define CMD_SBRK        0x08    // Send break
#define CMD_ER          0x10    // Error reset
#define CMD_RTS         0x20    // Request To Send
#define CMD_IR          0x40    // Internal reset
#define CMD_EH          0x80    // Enter hunt mode (sync only)

// Callback IDs
#define CALLBACK_RXD    1
#define CALLBACK_DSR    2
#define CALLBACK_CTS    3
#define CALLBACK_TXC    4
#define CALLBACK_RXC    5
#define CALLBACK_C      6

I8251::I8251(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 1, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_txrdy(this, im, 1, "txrdy", MODE_W)
    , i_rxrdy(this, im, 1, "rxrdy", MODE_W)
    , i_txe(this, im, 1, "txe", MODE_W)
    , i_dtr(this, im, 1, "dtr", MODE_W)
    , i_rts(this, im, 1, "rts", MODE_W)
    , i_dsr(this, im, 1, "dsr", MODE_R, CALLBACK_DSR)
    , i_cts(this, im, 1, "cts", MODE_R, CALLBACK_CTS)
    , i_syndet(this, im, 1, "syndet", MODE_R)
    , i_txd(this, im, 8, "txd", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
    , i_txc(this, im, 1, "txc", MODE_R, CALLBACK_TXC)
    , i_rxc(this, im, 1, "rxc", MODE_R, CALLBACK_RXC)
    , i_c(this, im, 1, "c", MODE_R, CALLBACK_C)
    , ext_clock(false)
{
    init();
}

void I8251::reset(const bool cold)
{
    AddressableDevice::reset(cold);
    init();
}

void I8251::init()
{
    mode_word = 0;
    sync_mode = false;
    baud_factor = 1;
    char_length = 8;
    parity_enable = false;
    even_parity = false;
    stop_bits = 1;
    sync_chars[0] = 0;
    sync_chars[1] = 0;
    single_sync = false;

    command_word = 0;
    tx_enable = false;
    rx_enable = false;
    send_break = false;
    hunt_mode = false;

    status = STATUS_TXE;
    control_state = STATE_MODE;
    sync_chars_remaining = 0;

    tx_buffer = 0;
    rx_buffer = 0;
    tx_buffer_full = false;
    rx_buffer_full = false;

    tx_clock_count = 0;
    rx_clock_count = 0;
    tx_clock_divider = 1;
    rx_clock_divider = 1;

    i_txrdy.change(0);
    i_rxrdy.change(0);
    i_txe.change(1);
    i_dtr.change(0);
    i_rts.change(0);
}

void I8251::parse_mode_word(uint8_t value)
{
    mode_word = value;

    if ((value & 0x03) == 0) {
        // Synchronous mode
        sync_mode = true;
        baud_factor = 1;
        single_sync = (value & 0x80) != 0;

        char_length = 5 + ((value >> 2) & 0x03);
        parity_enable = (value & 0x10) != 0;
        even_parity = (value & 0x20) != 0;

        // Need to receive sync characters next
        sync_chars_remaining = single_sync ? 1 : 2;
        control_state = STATE_SYNC_CHAR;
    } else {
        // Asynchronous mode
        sync_mode = false;

        switch (value & 0x03) {
            case 1: baud_factor = 1;  break;
            case 2: baud_factor = 16; break;
            case 3: baud_factor = 64; break;
        }

        char_length = 5 + ((value >> 2) & 0x03);
        parity_enable = (value & 0x10) != 0;
        even_parity = (value & 0x20) != 0;

        switch ((value >> 6) & 0x03) {
            case 0:  stop_bits = 0; break; // Invalid
            case 1:  stop_bits = 1; break; // 1 stop bit
            case 2:  stop_bits = 2; break; // 1.5 stop bits
            case 3:  stop_bits = 3; break; // 2 stop bits
        }

        tx_clock_divider = baud_factor;
        rx_clock_divider = baud_factor;

        control_state = STATE_COMMAND;
    }
}

void I8251::update_command(uint8_t value)
{
    command_word = value;

    // Internal reset
    if (value & CMD_IR) {
        init();
        return;
    }

    tx_enable = (value & CMD_TXEN) != 0;
    rx_enable = (value & CMD_RXEN) != 0;
    send_break = (value & CMD_SBRK) != 0;

    // DTR and RTS outputs (active low on real hardware, but we use active high logic)
    i_dtr.change((value & CMD_DTR) ? 1 : 0);
    i_rts.change((value & CMD_RTS) ? 1 : 0);

    // Error reset
    if (value & CMD_ER) {
        status &= ~(STATUS_PE | STATUS_OE | STATUS_FE);
    }

    // Enter hunt mode (sync only)
    if ((value & CMD_EH) && sync_mode) {
        hunt_mode = true;
    }

    update_status();
}

void I8251::update_status()
{
    // TxRDY: transmitter is ready to accept data
    if (tx_enable && !tx_buffer_full) {
        status |= STATUS_TXRDY;
        i_txrdy.change(1);
    } else {
        status &= ~STATUS_TXRDY;
        i_txrdy.change(0);
    }

    // TxE: transmitter is completely empty
    if (!tx_buffer_full) {
        status |= STATUS_TXE;
        i_txe.change(1);
    } else {
        status &= ~STATUS_TXE;
        i_txe.change(0);
    }

    // RxRDY: receiver has data available
    if (rx_buffer_full) {
        status |= STATUS_RXRDY;
        i_rxrdy.change(1);
    } else {
        status &= ~STATUS_RXRDY;
        i_rxrdy.change(0);
    }

    // DSR input (directly reflected in status)
    if ((i_dsr.value & 1) == 0) {
        status |= STATUS_DSR;  // DSR is active low, shown as active high in status
    } else {
        status &= ~STATUS_DSR;
    }
}

unsigned int I8251::get_value(unsigned int address)
{
    // std::cout << "I8251::get_value " << address << std::endl;
    if ((address & 1) == 0) {
        // A0=0: Read data
        rx_buffer_full = false;
        update_status();
        return rx_buffer;
    } else {
        // A0=1: Read status
        update_status();
        return status;
    }
}

void I8251::set_value(const unsigned address, const unsigned value, bool force)
{
    // std::cout << "I8251::set_value " << address << " 0x" << std::hex << (value & 0xFF) << std::endl;
    if ((address & 1) == 0) {
        // A0=0: Write data
        tx_buffer = value & 0xFF;
        tx_buffer_full = true;
        update_status();
    } else {
        // A0=1: Write control
        switch (control_state) {
            case STATE_MODE:
                parse_mode_word(value & 0xFF);
                break;
            case STATE_SYNC_CHAR:
                sync_chars[2 - sync_chars_remaining] = value & 0xFF;
                sync_chars_remaining--;
                if (sync_chars_remaining == 0)
                    control_state = STATE_COMMAND;
                break;
            case STATE_COMMAND:
                update_command(value & 0xFF);
                break;
        }
    }
}

void I8251::do_clock(const unsigned counter)
{
    if (tx_enable && tx_buffer_full) {
        tx_clock_count += counter;
        if (tx_clock_count >= tx_clock_divider) {
            tx_clock_count -= tx_clock_divider;
            i_txd.change(tx_buffer);
            tx_buffer_full = false;
            update_status();
        }
    }
}

void I8251::clock(const unsigned counter)
{
    if (!ext_clock)
        do_clock(counter);
}

void I8251::interface_callback(MAYBE_UNUSED unsigned callback_id, const unsigned new_value, const unsigned old_value)
{
    if (callback_id == CALLBACK_RXD) {
        // Data received on rxd interface
        if (rx_enable) {
            if (rx_buffer_full) {
                // Overrun error
                status |= STATUS_OE;
            }
            rx_buffer = new_value & 0xFF;
            rx_buffer_full = true;
            update_status();
        }
    } else if (callback_id == CALLBACK_DSR) {
        update_status();
    } else if (callback_id == CALLBACK_CTS) {
        update_status();
    } else if (callback_id == CALLBACK_TXC) {
        // Transmitter clock edge (rising)
        if ((new_value & 1) && !(old_value & 1)) {
            if (tx_enable && tx_buffer_full) {
                tx_clock_count++;
                if (tx_clock_count >= tx_clock_divider) {
                    tx_clock_count = 0;
                    i_txd.change(tx_buffer);
                    tx_buffer_full = false;
                    update_status();
                }
            }
        }
    } else if (callback_id == CALLBACK_RXC) {
        // Receiver clock edge (rising)
        if ((new_value & 1) && !(old_value & 1)) {
            // Receiver clocking handled via rxd callback for simplicity
        }
    } else if (callback_id == CALLBACK_C) {
        ext_clock = true;
        // Rising edge on external clock
        if ((new_value & 1) && !(old_value & 1)) {
            do_clock(1);
        }
    }
}

ComputerDevice * create_i8251(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8251(im, cd);
}