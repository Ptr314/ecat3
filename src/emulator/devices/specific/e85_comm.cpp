// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Communication port of the Электроника МС 0585 (КР580ВВ74 and the modem registers)

#include <cstring>

#include "e85_comm.h"
#include "emulator/utils.h"

#define REG_DATA    0
#define REG_CTRL_A  1
#define REG_CTRL_B  3
#define REG_MODEM   4
#define REG_RATES   6

#define WR1_TX_INT  0002
#define WR1_RX_INT  0030
#define WR3_RX_EN   0001
#define WR5_TX_EN   0010
#define MODEM_LOOP  0200

#define BITS_PER_CHAR   10

// Table 14, the clock 16 times the rate
static const uint16_t DIVIDERS[16] = {
    6336, 4224, 2880, 2355, 2112, 1056, 528, 264,
    176, 158, 132, 88, 66, 44, 33, 16
};

E85Comm::E85Comm(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irq(this, im, 1, "irq", MODE_W)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 16;
}

emulator::Result E85Comm::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_osc = read_confg_value(cd, "osc", false, (unsigned int)5068800);
    return emulator::Result::ok();
}

void E85Comm::channel_reset(unsigned int channel)
{
    memset(m_wr[channel], 0, sizeof(m_wr[channel]));
    m_ptr[channel] = 0;
    if (channel != 0) return;
    m_rx_count = 0;
    m_overrun = false;
    m_tx_full = false;
    m_shifting = false;
    m_tx_ip = false;
    m_rx_ip = false;
    m_ius = false;
}

void E85Comm::reset(bool cold)
{
    AddressableDevice::reset(cold);
    channel_reset(0);
    channel_reset(1);
    m_modem = 0;
    update();
}

bool E85Comm::pending() const
{
    const uint8_t wr1 = m_wr[0][1];
    return (m_tx_ip && (wr1 & WR1_TX_INT) != 0) || (m_rx_ip && (wr1 & WR1_RX_INT) != 0);
}

void E85Comm::update()
{
    const unsigned int line = (pending() && !m_ius) ? 1 : 0;
    if (i_irq.value != line) i_irq.change(line);
}

void E85Comm::start_shifter()
{
    if (m_shifting || !m_tx_full) return;
    m_shifter = m_tx;
    m_tx_full = false;
    m_shifting = true;
    m_tx_ip = true;
    const uint64_t divider = (uint64_t)DIVIDERS[m_rates >> 4] * 16;
    m_tx_left = (int64_t)((uint64_t)m_system_clock * BITS_PER_CHAR * divider / m_osc);
}

void E85Comm::transmit_done()
{
    m_shifting = false;
    m_sent++;
    if ((m_modem & MODEM_LOOP) != 0 && (m_wr[0][3] & WR3_RX_EN) != 0) {
        if (m_rx_count == RX_DEPTH) {
            m_overrun = true;
            m_rx_count--;
        }
        m_rx[m_rx_count++] = m_shifter;
        m_rx_ip = true;
        m_received++;
    }
    start_shifter();
    update();
}

void E85Comm::clock(unsigned int counter)
{
    if (!m_shifting) return;
    m_tx_left -= counter;
    if (m_tx_left <= 0) transmit_done();
}

void E85Comm::write_control(unsigned int channel, unsigned int value)
{
    const unsigned int ptr = m_ptr[channel];
    m_ptr[channel] = 0;
    if (ptr != 0) {
        m_wr[channel][ptr] = (uint8_t)value;
        update();
        return;
    }
    m_ptr[channel] = value & 7;
    switch ((value >> 3) & 7) {
    case 3:
        channel_reset(channel);
        break;
    case 5:
        if (channel == 0) m_tx_ip = false;
        break;
    case 6:
        if (channel == 0) m_overrun = false;
        break;
    case 7:
        m_ius = false;
        break;
    default:
        break;
    }
    update();
}

// Channel B, RR2. Bits 2-4 always hold the cause (table 23 of ТО2), whatever WR1B
// says: ПРОС never writes that register. 020 transmitter of channel A, as the
// autotest (133136) and the driver want, not 0 as in the table; 030 receiver;
// 034 a receive error, or nothing pending
unsigned int E85Comm::vector_register() const
{
    unsigned int v = m_wr[1][2] & ~034u;
    if (m_overrun) v |= 034;
    else if (m_rx_ip) v |= 030;
    else if (m_tx_ip) v |= 020;
    else v |= 034;
    return v;
}

unsigned int E85Comm::read_control(unsigned int channel, bool direct)
{
    const unsigned int ptr = direct ? 0 : (m_ptr[channel] & 3);
    if (!direct) m_ptr[channel] = 0;
    switch (ptr) {
    case 0:
        if (channel != 0) return 0;
        return (m_rx_count != 0 ? 001 : 0) | (pending() ? 002 : 0) | (m_tx_full ? 0 : 004);
    case 1:
        if (channel != 0) return 001;
        return (m_shifting || m_tx_full ? 0 : 001) | (m_overrun ? 040 : 0);
    case 2:
        if (channel == 0) return 0;
        {
            const unsigned int v = vector_register();
            if (pending()) {
                m_ius = true;
                update();
            }
            return v;
        }
    default:
        return 0;
    }
}

unsigned int E85Comm::get_value_word(unsigned int address)
{
    switch ((address >> 1) & 7) {
    case REG_DATA:
        if (m_rx_count != 0) {
            m_rx_last = m_rx[0];
            m_rx_count--;
            for (unsigned int i = 0; i < m_rx_count; i++) m_rx[i] = m_rx[i + 1];
        }
        m_rx_ip = m_rx_count != 0;
        update();
        return m_rx_last;
    case REG_CTRL_A: return read_control(0, false);
    case REG_CTRL_B: return read_control(1, false);
    case REG_MODEM:  return m_modem;
    default:         return 0;
    }
}

void E85Comm::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    value &= 0xFF;
    switch ((address >> 1) & 7) {
    case REG_DATA:
        if ((m_wr[0][5] & WR5_TX_EN) == 0) break;
        m_tx = (uint8_t)value;
        m_tx_full = true;
        m_tx_ip = false;
        start_shifter();
        update();
        break;
    case REG_CTRL_A: write_control(0, value); break;
    case REG_CTRL_B: write_control(1, value); break;
    case REG_MODEM:  m_modem = (uint8_t)value; break;
    case REG_RATES:  m_rates = (uint8_t)value; break;
    default:         break;
    }
}

unsigned E85Comm::get_direct(unsigned address)
{
    if (address & 1) return 0;
    switch ((address >> 1) & 7) {
    case REG_DATA:   return m_rx_count != 0 ? m_rx[0] : m_rx_last;
    case REG_CTRL_A: return read_control(0, true);
    case REG_CTRL_B: return read_control(1, true);
    case REG_MODEM:  return m_modem;
    default:         return 0;
    }
}

unsigned int E85Comm::get_value(unsigned int address)
{
    return (address & 1) ? 0 : get_value_word(address);
}

void E85Comm::set_value(unsigned int address, unsigned int value, bool force)
{
    if ((address & 1) == 0) set_value_word(address, value, force);
}

std::vector<DeviceFieldInfo> E85Comm::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"sent",     "Characters sent", false});
    r.push_back({"received", "Characters received in the loop", false});
    r.push_back({"wr_a",     "Write registers of channel A", false});
    r.push_back({"wr_b",     "Write registers of channel B", false});
    r.push_back({"modem",    "Register 17773310", false});
    r.push_back({"pending",  "Requests: 1 transmitter, 2 receiver, 4 in service", false});
    return r;
}

bool E85Comm::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "sent")     { out.values.push_back(m_sent);     return true; }
    if (field == "received") { out.values.push_back(m_received); return true; }
    out.width = 8;
    if (field == "wr_a" || field == "wr_b") {
        const uint8_t * wr = m_wr[field == "wr_a" ? 0 : 1];
        for (unsigned int i = 0; i < 8; i++) out.values.push_back(wr[i]);
        return true;
    }
    if (field == "modem")    { out.values.push_back(m_modem);    return true; }
    if (field == "pending") {
        out.values.push_back((m_tx_ip ? 1 : 0) | (m_rx_ip ? 2 : 0) | (m_ius ? 4 : 0));
        return true;
    }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void E85Comm::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.array("wr_a", m_wr[0], 8);
    w.array("wr_b", m_wr[1], 8);
    w.n("ptr_a", m_ptr[0]);
    w.n("ptr_b", m_ptr[1]);
    w.u("modem", m_modem);
    w.u("rates", m_rates);
    w.array("rx", m_rx, RX_DEPTH);
    w.n("rx_count", m_rx_count);
    w.u("rx_last", m_rx_last);
    w.b("overrun", m_overrun);
    w.u("tx", m_tx);
    w.u("shifter", m_shifter);
    w.b("tx_full", m_tx_full);
    w.b("shifting", m_shifting);
    w.n64("tx_left", (uint64_t)m_tx_left);
    w.b("tx_ip", m_tx_ip);
    w.b("rx_ip", m_rx_ip);
    w.b("ius", m_ius);
}

emulator::Result E85Comm::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.array("wr_a", m_wr[0], 8);
    r.array("wr_b", m_wr[1], 8);
    r.u("ptr_a", m_ptr[0]);
    r.u("ptr_b", m_ptr[1]);
    unsigned int v = 0;
    if (r.u("modem", v)) m_modem = (uint8_t)v;
    if (r.u("rates", v)) m_rates = (uint8_t)v;
    r.array("rx", m_rx, RX_DEPTH);
    r.u("rx_count", m_rx_count);
    if (m_rx_count > RX_DEPTH) m_rx_count = RX_DEPTH;
    if (r.u("rx_last", v)) m_rx_last = (uint8_t)v;
    r.b("overrun", m_overrun);
    if (r.u("tx", v)) m_tx = (uint8_t)v;
    if (r.u("shifter", v)) m_shifter = (uint8_t)v;
    r.b("tx_full", m_tx_full);
    r.b("shifting", m_shifting);
    uint64_t left = 0;
    if (r.n64("tx_left", left)) m_tx_left = (int64_t)left;
    r.b("tx_ip", m_tx_ip);
    r.b("rx_ip", m_rx_ip);
    r.b("ius", m_ius);
    return emulator::Result::ok();
}

ComputerDevice * create_e85_comm(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Comm(im, cd);
}
