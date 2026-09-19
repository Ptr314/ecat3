// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A serial port of the host computer, byte by byte and without blocking

#pragma once

#include <cstdint>
#include <string>

#include "emulator/result.h"

// The emulated serial line goes out through a real or virtual (com0com, socat)
// port of the host. Everything is polled from the emulation thread, so neither
// read() nor write() may wait: a byte that is not there yet is simply not
// there. The frame is always 8N1 without flow control.
//
// The name is what the user types:
//   Windows - COM5, opened as \\.\COM5 (the only form COM10 and up accept);
//             a full \\.\COM5 is taken as is
//   POSIX   - a path (/dev/ttyUSB0, /dev/pts/3, /dev/cu.usbserial-X); a name
//             without a slash is looked for in /dev
// The web build has no ports at all: open() always fails.
class HostSerialPort
{
public:
    HostSerialPort() = default;
    ~HostSerialPort();
    HostSerialPort(const HostSerialPort &) = delete;
    HostSerialPort & operator=(const HostSerialPort &) = delete;

    emulator::Result open(const std::string &name, unsigned int baud);
    void close();
    bool is_open() const;

    // A received byte, if one has arrived
    bool read(uint8_t &value);
    // Hands a byte to the port; false if the port refused it
    bool write(uint8_t value);

private:
    // What one system call brought in and read() has not handed out yet: the
    // port is read in whole chunks rather than a byte per call
    uint8_t m_rx[256];
    unsigned int m_rx_pos = 0;
    unsigned int m_rx_len = 0;
    unsigned int read_raw(uint8_t * buffer, unsigned int size);

#ifdef _WIN32
    void * m_handle = nullptr;          // HANDLE, INVALID_HANDLE_VALUE is kept as nullptr
#else
    int m_fd = -1;
#endif
};
