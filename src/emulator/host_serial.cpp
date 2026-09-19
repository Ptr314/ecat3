// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A serial port of the host computer, byte by byte and without blocking

#include "host_serial.h"

#include <cstring>

#include "dsk_tools/dsk_tools.h"

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif !defined(__EMSCRIPTEN__)
    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>
    #include <cerrno>
#endif

static emulator::Result port_error(const char * what, const std::string &name)
{
    return emulator::Result::error(emulator::ErrorCode::CommandFailed,
        "{HostSerialPort|" + std::string(what) + "} " + name);
}

HostSerialPort::~HostSerialPort()
{
    close();
}

bool HostSerialPort::read(uint8_t &value)
{
    if (m_rx_pos >= m_rx_len) {
        m_rx_pos = 0;
        m_rx_len = read_raw(m_rx, sizeof(m_rx));
        if (m_rx_len == 0) return false;
    }
    value = m_rx[m_rx_pos++];
    return true;
}

#if defined(_WIN32)

//----------------------------- Windows ------------------------------------//

emulator::Result HostSerialPort::open(const std::string &name, unsigned int baud)
{
    close();

    // COM1-COM9 open by their plain name, COM10 and up only through the
    // device namespace, so the prefix is always added
    std::string path = name;
    if (path.rfind("\\\\.\\", 0) != 0) path = "\\\\.\\" + path;

    const std::wstring w = dsk_tools::utf8_to_wide(path);
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Cannot open the serial port"), name);

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Cannot set up the serial port"), name);
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Cannot set up the serial port"), name);
    }

    // MAXDWORD with zero totals: ReadFile returns at once with whatever has
    // arrived. A write that the driver cannot take is given up after 50 ms
    COMMTIMEOUTS t;
    memset(&t, 0, sizeof(t));
    t.ReadIntervalTimeout = MAXDWORD;
    t.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(h, &t);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    m_handle = h;
    return emulator::Result::ok();
}

void HostSerialPort::close()
{
    if (m_handle != nullptr) {
        CloseHandle((HANDLE)m_handle);
        m_handle = nullptr;
    }
    m_rx_pos = m_rx_len = 0;
}

bool HostSerialPort::is_open() const
{
    return m_handle != nullptr;
}

unsigned int HostSerialPort::read_raw(uint8_t * buffer, unsigned int size)
{
    if (m_handle == nullptr) return 0;
    DWORD got = 0;
    if (!ReadFile((HANDLE)m_handle, buffer, size, &got, nullptr)) return 0;
    return (unsigned int)got;
}

bool HostSerialPort::write(uint8_t value)
{
    if (m_handle == nullptr) return false;
    DWORD put = 0;
    if (!WriteFile((HANDLE)m_handle, &value, 1, &put, nullptr)) return false;
    return put == 1;
}

#elif !defined(__EMSCRIPTEN__)

//----------------------------- POSIX --------------------------------------//

static speed_t baud_constant(unsigned int baud)
{
    switch (baud) {
    case 300:    return B300;
    case 600:    return B600;
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B0;
    }
}

emulator::Result HostSerialPort::open(const std::string &name, unsigned int baud)
{
    close();

    const std::string path = (name.find('/') == std::string::npos)? "/dev/" + name : name;

    const speed_t speed = baud_constant(baud);
    if (speed == B0)
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Unsupported baud rate for"), name);

    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Cannot open the serial port"), name);

    termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        ::close(fd);
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Cannot set up the serial port"), name);
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB | CSIZE);
    tio.c_cflag |= CS8;
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        ::close(fd);
        return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Cannot set up the serial port"), name);
    }
    tcflush(fd, TCIOFLUSH);

    m_fd = fd;
    return emulator::Result::ok();
}

void HostSerialPort::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_rx_pos = m_rx_len = 0;
}

bool HostSerialPort::is_open() const
{
    return m_fd >= 0;
}

unsigned int HostSerialPort::read_raw(uint8_t * buffer, unsigned int size)
{
    if (m_fd < 0) return 0;
    const ssize_t got = ::read(m_fd, buffer, size);
    return (got > 0)? (unsigned int)got : 0;
}

bool HostSerialPort::write(uint8_t value)
{
    if (m_fd < 0) return false;
    return ::write(m_fd, &value, 1) == 1;
}

#else

//----------------------------- Web: no ports ------------------------------//

emulator::Result HostSerialPort::open(const std::string &name, unsigned int baud)
{
    (void)baud;
    return port_error(QT_TRANSLATE_NOOP("HostSerialPort", "Serial ports are not available here"), name);
}

void HostSerialPort::close() {}
bool HostSerialPort::is_open() const { return false; }
unsigned int HostSerialPort::read_raw(uint8_t * buffer, unsigned int size) { (void)buffer; (void)size; return 0; }
bool HostSerialPort::write(uint8_t value) { (void)value; return false; }

#endif
