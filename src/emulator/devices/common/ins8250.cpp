// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: National INS8250 asynchronous communications element (UART)

#include "ins8250.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

#define LSR_DR      0x01
#define LSR_OE      0x02
#define LSR_THRE    0x20
#define LSR_TEMT    0x40
#define MCR_LOOP    0x10
#define LCR_DLAB    0x80

// Start, eight data bits and stop
#define BITS_PER_CHAR   10
// A silent host port is polled less often after so many empty polls
#define IDLE_POLLS      4
#define IDLE_SLOWDOWN   8

INS8250::INS8250(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_intr(this, im, 1, "intr", MODE_W)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 8;
}

emulator::Result INS8250::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_clock_hz = read_confg_value(cd, "clock_hz", false, (unsigned)1843200);
    if (m_clock_hz == 0 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{INS8250|" + std::string(QT_TRANSLATE_NOOP("INS8250", "Incorrect clock")) + "} " + name);
    set_divisor(m_divisor);
    // The host port the config names. A missing or busy one does not stop
    // the machine: the line goes nowhere, the field connected is 0
    m_port_name = str_trim(read_confg_value(cd, "port", false, std::string("")));
    if (!m_port_name.empty()) (void)m_host.open(m_port_name, m_clock_hz / 16 / m_divisor);
    const std::string peer = str_trim(read_confg_value(cd, "peer", false, std::string("")));
    if (!peer.empty()) {
        m_peer = dynamic_cast<SerialPeer*>(im->dm->get_device_by_name(peer));
        if (m_peer == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{INS8250|" + std::string(QT_TRANSLATE_NOOP("INS8250", "The peer is not a device that takes a serial line")) + "} " + peer);
    }
    INS8250::reset(true);
    return emulator::Result::ok();
}

void INS8250::reset(bool cold)
{
    (void)cold;
    // Master reset: everything but the RBR, THR, divisor and scratch
    m_ier = 0;
    m_lcr = 0;
    m_mcr = 0;
    m_lsr = LSR_THRE | LSR_TEMT;
    m_thre_pending = false;
    m_tx_busy = false;
    update_msr();
    update_intr();
}

void INS8250::set_divisor(unsigned d)
{
    m_divisor = d ? d : 1;
    const uint64_t baud16 = (uint64_t)m_clock_hz;      // 16 x baud x divisor
    m_char_ticks = (uint64_t)m_system_clock * BITS_PER_CHAR * 16 * m_divisor / baud16;
    if (m_char_ticks == 0) m_char_ticks = 1;
}

void INS8250::update_msr()
{
    unsigned in;
    if (m_mcr & MCR_LOOP)
        in = ((m_mcr & 0x02) ? 0x10 : 0) | ((m_mcr & 0x01) ? 0x20 : 0)
           | ((m_mcr & 0x04) ? 0x40 : 0) | ((m_mcr & 0x08) ? 0x80 : 0);
    else
        in = 0x10 | 0x20 | 0x80;        // CTS, DSR, DCD: a modem that is always there
    const unsigned changed = (in ^ m_msr) & 0xF0;
    m_msr = (uint8_t)((m_msr & 0x0F) | in
          | ((changed & 0x10) ? 0x01 : 0) | ((changed & 0x20) ? 0x02 : 0)
          | (((m_msr & 0x40) && !(in & 0x40)) ? 0x04 : 0) | ((changed & 0x80) ? 0x08 : 0));
}

unsigned INS8250::iir() const
{
    if ((m_ier & 0x04) && (m_lsr & LSR_OE)) return 0x06;
    if ((m_ier & 0x01) && (m_lsr & LSR_DR)) return 0x04;
    if ((m_ier & 0x02) && m_thre_pending) return 0x02;
    if ((m_ier & 0x08) && (m_msr & 0x0F)) return 0x00;
    return 0x01;
}

void INS8250::update_intr()
{
    // OUT2 gates INTRPT on an IBM PC board, not in the chip: the chip drives it
    i_intr.change((iir() & 1) ? 0 : 1);
}

void INS8250::receive(uint8_t value)
{
    if (m_lsr & LSR_DR) {
        m_lsr |= LSR_OE;
        m_overruns++;
    }
    m_rbr = value;
    m_lsr |= LSR_DR;
    m_received++;
    update_intr();
}

void INS8250::transmit_done()
{
    m_tx_busy = false;
    m_sent++;
    put(m_thr);
    if ((m_mcr & MCR_LOOP) || m_plug)
        receive(m_thr);
    else if (m_peer != nullptr)
        m_peer->serial_in(m_thr);
    else
        (void)m_host.write(m_thr);

    m_lsr |= LSR_THRE | LSR_TEMT;
    m_thre_pending = true;
    update_intr();
}

void INS8250::clock(unsigned int counter)
{
    if (m_tx_busy) {
        m_tx_left -= counter;
        if (m_tx_left <= 0) transmit_done();
    }
    if (m_rx_left > 0) {
        m_rx_left -= counter;
        return;
    }
    if (m_lsr & LSR_DR) return;
    m_rx_left = (int64_t)m_char_ticks;
    if (!m_rx_queue.empty()) {
        const uint8_t b = m_rx_queue.front();
        m_rx_queue.pop_front();
        receive(b);
        return;
    }
    if ((m_mcr & MCR_LOOP) || m_plug || !m_host.is_open()) return;
    uint8_t b;
    if (m_host.read(b)) {
        m_idle_polls = 0;
        receive(b);
        return;
    }
    if (++m_idle_polls >= IDLE_POLLS) m_rx_left = (int64_t)m_char_ticks * IDLE_SLOWDOWN;
}

unsigned INS8250::get_value(unsigned address)
{
    switch (address & 7) {
        case 0:
            if (m_lcr & LCR_DLAB) return m_divisor & 0xFF;
            m_lsr &= ~LSR_DR;
            update_intr();
            return m_rbr;
        case 1: return (m_lcr & LCR_DLAB) ? (m_divisor >> 8) & 0xFF : m_ier;
        case 2: {
            const unsigned v = iir();
            if (v == 0x02) { m_thre_pending = false; update_intr(); }
            return v;
        }
        case 3: return m_lcr;
        case 4: return m_mcr;
        case 5: {
            const unsigned v = m_lsr;
            m_lsr &= ~LSR_OE;
            update_intr();
            return v;
        }
        case 6: {
            const unsigned v = m_msr;
            m_msr &= 0xF0;
            update_intr();
            return v;
        }
        default: return m_scr;
    }
}

unsigned INS8250::get_direct(unsigned address)
{
    switch (address & 7) {
        case 0: return (m_lcr & LCR_DLAB) ? (m_divisor & 0xFF) : m_rbr;
        case 1: return (m_lcr & LCR_DLAB) ? (m_divisor >> 8) & 0xFF : m_ier;
        case 2: return iir();
        case 3: return m_lcr;
        case 4: return m_mcr;
        case 5: return m_lsr;
        case 6: return m_msr;
        default: return m_scr;
    }
}

void INS8250::set_value(unsigned address, unsigned value, bool force)
{
    (void)force;
    value &= 0xFF;
    switch (address & 7) {
        case 0:
            if (m_lcr & LCR_DLAB) { set_divisor((m_divisor & 0xFF00) | value); break; }
            m_thr = (uint8_t)value;
            m_lsr &= ~(LSR_THRE | LSR_TEMT);
            m_thre_pending = false;
            // The holding register goes into the shift register at once when
            // that is free; a byte written while one is shifted out is lost
            if (!m_tx_busy) {
                m_tx_busy = true;
                m_tx_left = (int64_t)m_char_ticks;
            }
            break;
        case 1:
            if (m_lcr & LCR_DLAB) { set_divisor((m_divisor & 0xFF) | (value << 8)); break; }
            m_ier = (uint8_t)(value & 0x0F);
            if ((m_ier & 0x02) && (m_lsr & LSR_THRE)) m_thre_pending = true;
            break;
        case 3: m_lcr = (uint8_t)value; break;
        case 4: m_mcr = (uint8_t)(value & 0x1F); update_msr(); break;
        case 7: m_scr = (uint8_t)value; break;
        default: break;
    }
    update_intr();
}

ConfigFields INS8250::get_config_fields()
{
    ConfigField f;
    f.name = "port";
    f.title = QT_TRANSLATE_NOOP("ConfigFields", "Serial port of the computer");
    f.type = CONFIG_FIELD_STRING;
    return {f};
}

std::vector<DeviceFieldInfo> INS8250::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"lsr",       "Line status register",                           false});
    r.push_back({"lcr",       "Line control register",                          false});
    r.push_back({"mcr",       "Modem control register",                         false});
    r.push_back({"baud",      "Rate the divisor sets",                          false});
    r.push_back({"connected", "1 when the line goes out to a host port",        false});
    r.push_back({"port",      "Name of the host port",                          false});
    r.push_back({"sent",      "Bytes sent",                                     false});
    r.push_back({"received",  "Bytes received",                                 false});
    r.push_back({"overruns",  "Bytes lost to a full receiver",                  false});
    r.push_back({"queued",    "Bytes a script fed in and the receiver has not taken", false});
    r.push_back({"plug",      "1 with a loopback plug on the connector",        false});
    add_output_fields(r);
    return r;
}

bool INS8250::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "port") {
        out.text = m_port_name;
        return true;
    }
    if (get_output_field(field, from, to, out)) return true;
    out.numeric = true;
    if (field == "lsr")       { out.values.push_back(m_lsr); return true; }
    if (field == "lcr")       { out.values.push_back(m_lcr); return true; }
    if (field == "mcr")       { out.values.push_back(m_mcr); return true; }
    if (field == "plug")      { out.values.push_back(m_plug ? 1 : 0); return true; }
    if (field == "connected") { out.values.push_back(m_host.is_open() ? 1 : 0); return true; }
    out.width = 32;
    if (field == "baud")      { out.values.push_back(m_clock_hz / 16 / m_divisor); return true; }
    if (field == "sent")      { out.values.push_back(m_sent); return true; }
    if (field == "received")  { out.values.push_back(m_received); return true; }
    if (field == "overruns")  { out.values.push_back(m_overruns); return true; }
    if (field == "queued")    { out.values.push_back((unsigned)m_rx_queue.size()); return true; }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> INS8250::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"connect",    "\"port\"", "Takes the line out to a host port: COM5, /dev/ttyUSB0"});
    r.push_back({"disconnect", "",         "Disconnects the line from the host port"});
    r.push_back({"send",       "\"text\"", "Feeds bytes to the receiver (\\n, \\r, \\t, \\ooo)"});
    r.push_back({"sendfile",   "\"file\"", "Feeds a file to the receiver"});
    r.push_back({"plug",       "[0|1]",    "A loopback plug on the connector: output wired to input"});
    return r;
}

emulator::Result INS8250::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    const std::string arg = p.empty() ? std::string() : p[0];
    if (command == "connect") {
        if (arg.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{INS8250|" + std::string(QT_TRANSLATE_NOOP("INS8250", "Command 'connect' expects a port name")) + "}");
        m_port_name = arg;
        return m_host.open(arg, m_clock_hz / 16 / m_divisor);
    }
    if (command == "disconnect") {
        m_host.close();
        return emulator::Result::ok();
    }
    if (command == "send") {
        for (char c : decode_send_text(arg)) m_rx_queue.push_back((uint8_t)c);
        return emulator::Result::ok();
    }
    if (command == "plug") {
        m_plug = arg.empty() ? !m_plug : (parse_numeric_value(arg, 10) != 0);
        return emulator::Result::ok();
    }
    if (command == "sendfile") {
        if (arg.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{INS8250|" + std::string(QT_TRANSLATE_NOOP("INS8250", "Command 'sendfile' expects a file name")) + "}");
        std::string file = find_file_location(sd, arg);
        if (file.empty()) file = arg;
        if (!dsk_tools::file_exists(file))
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{INS8250|" + std::string(QT_TRANSLATE_NOOP("INS8250", "File not found")) + "} " + arg);
        const std::string data = dsk_tools::utf8_read_file(file);
        for (size_t i = 0; i < data.size(); i++) m_rx_queue.push_back((uint8_t)data[i]);
        return emulator::Result::ok();
    }
    return AddressableDevice::send_command(command, parameters);
}

void INS8250::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("rbr", m_rbr, 8);
    w.u("thr", m_thr, 8);
    w.u("ier", m_ier, 8);
    w.u("lcr", m_lcr, 8);
    w.u("mcr", m_mcr, 8);
    w.u("lsr", m_lsr, 8);
    w.u("msr", m_msr, 8);
    w.u("scr", m_scr, 8);
    w.n("divisor", m_divisor);
    w.b("thre_pending", m_thre_pending);
    w.b("tx_busy", m_tx_busy);
    w.n64("tx_left", (uint64_t)m_tx_left);
    w.n("sent", m_sent);
    w.n("received", m_received);
    w.b("plug", m_plug);
    if (!m_rx_queue.empty()) {
        std::vector<uint8_t> queue(m_rx_queue.begin(), m_rx_queue.end());
        w.n64("rx_queued", queue.size());
        w.array("rx_queue", queue.data(), queue.size());
    }
}

emulator::Result INS8250::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    unsigned v;
    if (r.u("rbr", v)) m_rbr = (uint8_t)v;
    if (r.u("thr", v)) m_thr = (uint8_t)v;
    if (r.u("ier", v)) m_ier = (uint8_t)v;
    if (r.u("lcr", v)) m_lcr = (uint8_t)v;
    if (r.u("mcr", v)) m_mcr = (uint8_t)v;
    if (r.u("lsr", v)) m_lsr = (uint8_t)v;
    if (r.u("msr", v)) m_msr = (uint8_t)v;
    if (r.u("scr", v)) m_scr = (uint8_t)v;
    if (r.u("divisor", v)) set_divisor(v);
    r.b("thre_pending", m_thre_pending);
    r.b("tx_busy", m_tx_busy);
    uint64_t tx_left = 0;
    if (r.n64("tx_left", tx_left)) m_tx_left = (int64_t)tx_left;
    r.u("sent", m_sent);
    r.u("received", m_received);
    r.b("plug", m_plug);
    m_rx_queue.clear();
    uint64_t queued = 0;
    if (r.n64("rx_queued", queued) && queued > 0 && queued <= 16u * 1024 * 1024) {
        std::vector<uint8_t> queue(static_cast<size_t>(queued), 0);
        if (r.array("rx_queue", queue.data(), queue.size()))
            m_rx_queue.assign(queue.begin(), queue.end());
    }
    return emulator::Result::ok();
}

ComputerDevice * create_ins8250(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new INS8250(im, cd);
}
