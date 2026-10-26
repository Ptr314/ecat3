// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: National INS8250 asynchronous communications element (UART)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/serial_line.h"
#include "emulator/devices/common/byte_tap.h"

// The UART of the IBM PC and of the RS-232 port of the Правец-8С: eight
// registers on A0-A2, the divisor latch behind bit 7 of the line control.
//
//   0  RBR (read) / THR (write); DLL with DLAB
//   1  IER: 0 - received data, 1 - THR empty, 2 - line status, 3 - modem
//      status; DLM with DLAB
//   2  IIR (read): 1 - no interrupt, 6 - line status, 4 - received data,
//      2 - THR empty, 0 - modem status
//   3  LCR: bit 7 DLAB; the frame itself is not modelled - a byte is a byte
//   4  MCR: 0 DTR, 1 RTS, 2 OUT1, 3 OUT2, 4 loop
//   5  LSR: 0 DR, 1 OE, 5 THRE, 6 TEMT
//   6  MSR: 4 CTS, 5 DSR, 6 RI, 7 DCD (and their changes in 0-3)
//   7  SCR
//
// A byte takes ten bits at the rate the divisor sets from the 1.8432 MHz
// crystal (clock_hz). The line goes out to a port of the host (parameter port
// or command connect); a script may feed the receiver (send, sendfile). With
// the loop bit of the MCR the transmitter feeds the receiver, and the modem
// inputs follow the modem outputs (DTR -> DSR, RTS -> CTS, OUT1 -> RI, OUT2 ->
// DCD); otherwise they read CTS, DSR and DCD on, as from a modem that is
// always there.
// A device at the other end of the line inside the emulator (a host computer
// played by the emulator itself): it gets each byte the UART sends and puts
// its answers into the receiver with INS8250::peer_send()
class SerialPeer
{
public:
    virtual ~SerialPeer() {}
    virtual void serial_in(uint8_t value) = 0;
};

class INS8250: public AddressableDevice, public ByteTap
{
private:
    Interface i_intr;               // INTRPT, 1 - an interrupt is pending

    uint8_t m_rbr = 0, m_thr = 0, m_ier = 0, m_lcr = 0, m_mcr = 0, m_lsr = 0x60, m_msr = 0, m_scr = 0;
    unsigned m_divisor = 12;
    unsigned m_clock_hz = 1843200;
    bool m_thre_pending = false;    // the THR empty interrupt, until IIR is read or THR written

    uint64_t m_char_ticks = 0;
    // The holding register (THR) and the shift register behind it: a byte
    // goes into the shift register at once when that is free, and a second
    // one waits in THR until the first is out
    uint8_t m_tsr = 0;
    bool m_tx_busy = false;         // the shift register is sending
    int64_t m_tx_left = 0;
    int64_t m_rx_left = 0;

    SerialLine m_line;                  // the host port and what a script or the peer feeds
    bool m_plug = false;
    SerialPeer * m_peer = nullptr;      // parameter peer: the line ends in the emulator

    unsigned m_sent = 0, m_received = 0, m_overruns = 0;

    void set_divisor(unsigned d);
    void receive(uint8_t value);
    void start_shift();
    void transmit_done();
    unsigned iir() const;
    void update_intr();
    void update_msr();

public:
    INS8250(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;
    // From the peer: bytes for the receiver, taken at the line rate
    void peer_send(uint8_t value) { m_line.feed(value); }
    unsigned char_ticks() const { return (unsigned)m_char_ticks; }

    ConfigFields get_config_fields() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_ins8250(InterfaceManager *im, EmulatorConfigDevice *cd);
