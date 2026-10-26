// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The far side of an emulated serial line: a host port and the bytes a script feeds

#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "emulator/core.h"
#include "emulator/host_serial.h"

class StateWriter;
class StateReader;

// What every serial line of the emulator has outside its chip - DL11,
// INS8250, the ACIA of the U-LAN: the port of the host the line may go out
// to, and the bytes waiting for the receiver (fed by a script with send and
// sendfile, by a peer device, or by the line itself, as the U-LAN echo). The
// registers, the interrupts and the time of a character stay with the device.
//
// The receiver takes one byte per character time, from the queue first, then
// from the host port. A silent host port is asked only once in IDLE_SLOWDOWN
// character times: a read is a system call, thousands a second from the
// emulation thread otherwise. The step of the receiver itself does not
// change - the U-LAN firmware times its own echo by it.
//
// All of it belongs to the emulation thread.
class SerialLine
{
public:
    // The port the configuration names. A missing or busy one does not stop
    // the machine: the line goes nowhere, and connected() says so
    void open_configured(const std::string &name, unsigned int baud);
    emulator::Result connect(const std::string &name, unsigned int baud);
    void disconnect() { m_host.close(); }
    bool connected() const { return m_host.is_open(); }
    const std::string & port_name() const { return m_port_name; }

    // Bytes for the receiver, taken at the line rate
    void feed(uint8_t value) { m_queue.push_back(value); }
    void feed_text(const std::string &text);
    size_t queued() const { return m_queue.size(); }
    void clear_queue() { m_queue.clear(); }

    // One character time of the receiver: the next byte of the queue, else
    // of the host port when host is true (a loop or a plug cuts the host off)
    bool next(uint8_t &value, bool host);
    // A byte out to the host port, if one is open
    void write(uint8_t value) { if (m_host.is_open()) (void)m_host.write(value); }

    // connect, disconnect, send and sendfile of the device's own command set.
    // false: the command is not one of these
    bool command(const std::string &command, const std::string &arg, unsigned int baud,
                 SystemData * sd, emulator::Result &result);

    // The queue in a snapshot (rx_queued, rx_queue); the host port is not state
    void save_queue(StateWriter &w) const;
    void load_queue(const StateReader &r);

private:
    std::deque<uint8_t> m_queue;
    HostSerialPort m_host;
    std::string m_port_name;
    unsigned int m_idle_polls = 0;      // host reads in a row that found nothing
    unsigned int m_idle_skip = 0;       // character times before the next one
};
