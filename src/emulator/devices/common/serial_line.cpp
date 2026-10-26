// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The far side of an emulated serial line: a host port and the bytes a script feeds

#include <vector>

#include "serial_line.h"
#include "emulator/state.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

// After so many empty reads the host port is asked once in IDLE_SLOWDOWN
// character times: at 9600 bauds once in 8 ms
#define IDLE_POLLS      4
#define IDLE_SLOWDOWN   8

// A queue longer than any script would push is not allocated from a file
#define MAX_QUEUE       (16u * 1024 * 1024)

void SerialLine::open_configured(const std::string &name, unsigned int baud)
{
    m_port_name = str_trim(name);
    if (!m_port_name.empty()) (void)m_host.open(m_port_name, baud);
}

emulator::Result SerialLine::connect(const std::string &name, unsigned int baud)
{
    m_port_name = name;
    m_idle_polls = 0;
    m_idle_skip = 0;
    return m_host.open(name, baud);
}

void SerialLine::feed_text(const std::string &text)
{
    for (char c : decode_send_text(text)) m_queue.push_back((uint8_t)c);
}

bool SerialLine::next(uint8_t &value, bool host)
{
    if (!m_queue.empty()) {
        value = m_queue.front();
        m_queue.pop_front();
        return true;
    }
    if (!host || !m_host.is_open()) return false;
    if (m_idle_skip > 0) {
        m_idle_skip--;
        return false;
    }
    if (m_host.read(value)) {
        m_idle_polls = 0;
        return true;
    }
    // What comes meanwhile waits in the driver of the port and is taken
    // later, a byte per character time
    if (++m_idle_polls >= IDLE_POLLS) m_idle_skip = IDLE_SLOWDOWN - 1;
    return false;
}

bool SerialLine::command(const std::string &command, const std::string &arg, unsigned int baud,
                         SystemData * sd, emulator::Result &result)
{
    result = emulator::Result::ok();
    if (command == "connect") {
        if (arg.empty())
            result = emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{SerialLine|" + std::string(QT_TRANSLATE_NOOP("SerialLine", "Command 'connect' expects a port name")) + "}");
        else
            result = connect(arg, baud);
        return true;
    }
    if (command == "disconnect") {
        disconnect();
        return true;
    }
    if (command == "send") {
        feed_text(arg);
        return true;
    }
    if (command == "sendfile") {
        if (arg.empty()) {
            result = emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{SerialLine|" + std::string(QT_TRANSLATE_NOOP("SerialLine", "Command 'sendfile' expects a file name")) + "}");
            return true;
        }
        std::string file = find_file_location(sd, arg);
        if (file.empty()) file = arg;
        if (!dsk_tools::file_exists(file)) {
            result = emulator::Result::error(emulator::ErrorCode::FileError,
                "{SerialLine|" + std::string(QT_TRANSLATE_NOOP("SerialLine", "File not found")) + "} " + arg);
            return true;
        }
        // Whole, in one call: UTF8_ifstream of MinGW wraps a Windows file whose
        // read() returns the count, the other builds have std::ifstream, whose
        // read() returns the stream
        const std::string data = dsk_tools::utf8_read_file(file);
        for (size_t i = 0; i < data.size(); i++) m_queue.push_back((uint8_t)data[i]);
        return true;
    }
    return false;
}

void SerialLine::save_queue(StateWriter &w) const
{
    //What is fed and not taken yet: the restored line does not say it. The
    //queue is as long as the script made it, so its length is stated beside it
    if (m_queue.empty()) return;
    std::vector<uint8_t> queue(m_queue.begin(), m_queue.end());
    w.n64("rx_queued", queue.size());
    w.array("rx_queue", queue.data(), queue.size());
}

void SerialLine::load_queue(const StateReader &r)
{
    m_queue.clear();
    uint64_t queued = 0;
    if (r.n64("rx_queued", queued) && queued > 0 && queued <= MAX_QUEUE) {
        std::vector<uint8_t> queue(static_cast<size_t>(queued), 0);
        if (r.array("rx_queue", queue.data(), queue.size()))
            m_queue.assign(queue.begin(), queue.end());
    }
}
