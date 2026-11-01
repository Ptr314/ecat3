// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: SCN2661 (КМ1818ВВ61) serial controller, asynchronous mode

#include "scn2661.h"
#include "emulator/utils.h"

#define CALLBACK_RXD    1

#define ST_TXRDY    0001
#define ST_RXRDY    0002
#define ST_PARITY   0010
#define ST_OVERRUN  0020
#define ST_FRAME    0040
#define ST_ERRORS   (ST_PARITY | ST_OVERRUN | ST_FRAME)
#define ST_ALWAYS   0100            // the unused bit, reads 1
#define ST_DSR      0200

#define CMD_TXEN    0001
#define CMD_RXEN    0004
#define CMD_RESET   0020
#define CMD_ECHO    0100
#define CMD_LOCAL   0200            // short loop; with CMD_ECHO the long one

// Divider of the reference for each code of mode register 2 (table 29), with
// the clock 16 times the rate
static const uint16_t DIVIDERS[16] = {
    6336, 4224, 2880, 2355, 2112, 1056, 528, 264,
    176, 158, 132, 88, 66, 44, 33, 16
};

SCN2661::SCN2661(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_txd(this, im, 8, "txd", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
    , i_txrdy(this, im, 1, "txrdy", MODE_W)
    , i_rxrdy(this, im, 1, "rxrdy", MODE_W)
    , i_dsr(this, im, 1, "dsr", MODE_R)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 8;
}

emulator::Result SCN2661::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_osc = read_confg_value(cd, "osc", false, (unsigned int)5068800);
    return emulator::Result::ok();
}

void SCN2661::reset(bool cold)
{
    AddressableDevice::reset(cold);
    m_mode[0] = m_mode[1] = 0;
    m_mode_ptr = 0;
    m_command = 0;
    m_status = 0;
    m_rx = 0;
    m_thr_full = false;
    m_tsr_busy = false;
    m_tx_left = 0;
    update_lines();
}

// Start bit, data, parity, stop bits, in clocks of the machine
uint64_t SCN2661::char_ticks() const
{
    const unsigned int data = 5 + ((m_mode[0] >> 2) & 3);
    const unsigned int parity = (m_mode[0] & 0020) ? 1 : 0;
    const unsigned int stop = ((m_mode[0] >> 6) == 3) ? 2 : 1;
    const uint64_t divider = (uint64_t)DIVIDERS[m_mode[1] & 017] * 16;
    return (uint64_t)m_system_clock * (1 + data + parity + stop) * divider / m_osc;
}

void SCN2661::update_lines()
{
    const unsigned int tx = ((m_command & CMD_TXEN) != 0 && !m_thr_full) ? 1 : 0;
    const unsigned int rx = (m_status & ST_RXRDY) ? 1 : 0;
    if ((i_txrdy.value & 1) != tx || i_txrdy.value > 1) i_txrdy.change(tx);
    if ((i_rxrdy.value & 1) != rx || i_rxrdy.value > 1) i_rxrdy.change(rx);
}

void SCN2661::load_shifter()
{
    if (m_tsr_busy || !m_thr_full || (m_command & CMD_TXEN) == 0) return;
    m_tsr = m_thr;
    m_thr_full = false;
    m_tsr_busy = true;
    m_tx_left = (int64_t)char_ticks();
}

void SCN2661::transmit_done()
{
    m_tsr_busy = false;
    m_sent++;
    if ((m_command & CMD_LOCAL) != 0 && (m_command & CMD_ECHO) == 0) receive(m_tsr);
    else i_txd.change(m_tsr);
    load_shifter();
    update_lines();
}

void SCN2661::receive(uint8_t value)
{
    const bool local = (m_command & (CMD_LOCAL | CMD_ECHO)) == CMD_LOCAL;
    if ((m_command & CMD_RXEN) == 0 && !local) return;
    if (m_status & ST_RXRDY) {
        m_status |= ST_OVERRUN;
        m_overruns++;
    }
    m_rx = value;
    m_status |= ST_RXRDY;
    m_received++;
    if (m_command & CMD_ECHO) i_txd.change(value);
    update_lines();
}

void SCN2661::clock(unsigned int counter)
{
    if (!m_tsr_busy) return;
    m_tx_left -= counter;
    if (m_tx_left <= 0) transmit_done();
}

void SCN2661::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_RXD) receive((uint8_t)(new_value & 0xFF));
}

// Table 27. TXRDY reads 0 in the loops; the pin goes on working
unsigned int SCN2661::read_status() const
{
    unsigned int v = (m_status & (ST_RXRDY | ST_ERRORS)) | ST_ALWAYS;
    if ((m_command & CMD_TXEN) != 0 && !m_thr_full && (m_command & CMD_LOCAL) == 0) v |= ST_TXRDY;
    if ((i_dsr.value & 1) != 0) v |= ST_DSR;
    return v;
}

void SCN2661::write_command(unsigned int value)
{
    const bool rx_off = (value & CMD_RXEN) == 0;
    m_command = (uint8_t)(value & ~CMD_RESET);
    if (value & CMD_RESET) m_status &= (uint8_t)~ST_ERRORS;
    if (rx_off) m_status &= (uint8_t)~(ST_RXRDY | ST_ERRORS);
    load_shifter();
    update_lines();
}

unsigned int SCN2661::get_value_word(unsigned int address)
{
    switch ((address >> 1) & 3) {
    case 0:
        m_status &= (uint8_t)~ST_RXRDY;
        update_lines();
        return m_rx;
    case 1:
        return read_status();
    case 2: {
        const unsigned int v = m_mode[m_mode_ptr];
        m_mode_ptr ^= 1;
        return v;
    }
    default:
        // Reading the command register puts the pointer back on mode 1: the
        // autotest starts every programming with it
        m_mode_ptr = 0;
        return m_command;
    }
}

void SCN2661::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    value &= 0xFF;
    switch ((address >> 1) & 3) {
    case 0:
        m_thr = (uint8_t)value;
        m_thr_full = true;
        // TXRDY drops first, even with the shifter free: its rising edge is
        // what asks for the next character
        update_lines();
        load_shifter();
        update_lines();
        break;
    case 1:
        break;                      // SYN1/SYN2/DLE, of the synchronous modes
    case 2:
        m_mode[m_mode_ptr] = (uint8_t)value;
        m_mode_ptr ^= 1;
        break;
    default:
        write_command(value);
        break;
    }
}

// The debugger reads without taking a character or stepping the pointer
unsigned SCN2661::get_direct(unsigned address)
{
    if (address & 1) return 0;
    switch ((address >> 1) & 3) {
    case 0:  return m_rx;
    case 1:  return read_status();
    case 2:  return m_mode[m_mode_ptr];
    default: return m_command;
    }
}

unsigned int SCN2661::get_value(unsigned int address)
{
    return (address & 1) ? 0 : get_value_word(address);
}

void SCN2661::set_value(unsigned int address, unsigned int value, bool force)
{
    if ((address & 1) == 0) set_value_word(address, value, force);
}

std::vector<DeviceFieldInfo> SCN2661::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"status",   "Status register", false});
    r.push_back({"command",  "Command register", false});
    r.push_back({"mode",     "Mode registers 1 and 2", false});
    r.push_back({"sent",     "Characters sent", false});
    r.push_back({"received", "Characters received", false});
    r.push_back({"overruns", "Receiver overruns", false});
    return r;
}

bool SCN2661::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "status")   { out.values.push_back(read_status()); return true; }
    if (field == "command")  { out.values.push_back(m_command);     return true; }
    if (field == "mode")     { out.values.push_back(m_mode[0]); out.values.push_back(m_mode[1]); return true; }
    out.width = 16;
    if (field == "sent")     { out.values.push_back(m_sent);        return true; }
    if (field == "received") { out.values.push_back(m_received);    return true; }
    if (field == "overruns") { out.values.push_back(m_overruns);    return true; }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void SCN2661::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("mode1", m_mode[0]);
    w.u("mode2", m_mode[1]);
    w.n("mode_ptr", m_mode_ptr);
    w.u("command", m_command);
    w.u("status", m_status);
    w.u("rx", m_rx);
    w.u("thr", m_thr);
    w.u("tsr", m_tsr);
    w.b("thr_full", m_thr_full);
    w.b("tsr_busy", m_tsr_busy);
    w.n64("tx_left", (uint64_t)m_tx_left);
}

emulator::Result SCN2661::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    unsigned int v = 0;
    if (r.u("mode1", v)) m_mode[0] = (uint8_t)v;
    if (r.u("mode2", v)) m_mode[1] = (uint8_t)v;
    r.u("mode_ptr", m_mode_ptr);
    if (r.u("command", v)) m_command = (uint8_t)v;
    if (r.u("status", v)) m_status = (uint8_t)v;
    if (r.u("rx", v)) m_rx = (uint8_t)v;
    if (r.u("thr", v)) m_thr = (uint8_t)v;
    if (r.u("tsr", v)) m_tsr = (uint8_t)v;
    r.b("thr_full", m_thr_full);
    r.b("tsr_busy", m_tsr_busy);
    uint64_t left = 0;
    if (r.n64("tx_left", left)) m_tx_left = (int64_t)left;
    return emulator::Result::ok();
}

ComputerDevice * create_scn2661(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new SCN2661(im, cd);
}
