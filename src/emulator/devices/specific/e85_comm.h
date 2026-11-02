// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Communication port of the Электроника МС 0585 (КР580ВВ74 and the modem registers)

#pragma once

#include "emulator/core.h"

// The 8274 serves only channel A in its asynchronous mode; channel B holds the
// vector and tells the interrupt condition in RR2 (ТО2 Э-85, 4.2.9). The
// board adds the modem request register (bit 7 is the self test loop), the
// modem state and the rate register. Nothing goes out of the machine yet: what
// is sent without the loop is only counted, and the receiver hears nothing.
//
// Words from the start of the range, 17773300: 0 data, 1 control A, 2 reserved,
// 3 control B, 4 modem request, 5 modem state, 6 rates.
class E85Comm: public AddressableDevice
{
private:
    Interface i_irq;

    uint8_t m_wr[2][8] = {};
    unsigned int m_ptr[2] = {};
    uint8_t m_modem = 0;
    uint8_t m_rates = 0;

    // The receive queue of the chip. At 9600 baud ПРОС misses the single
    // character time now and then, and its handler reads until the queue is empty
    enum { RX_DEPTH = 3 };
    uint8_t m_rx[RX_DEPTH] = {};
    unsigned int m_rx_count = 0;
    uint8_t m_rx_last = 0;          // the data register with the queue empty
    bool m_overrun = false;
    uint8_t m_tx = 0;
    uint8_t m_shifter = 0;
    bool m_tx_full = false;
    bool m_shifting = false;
    int64_t m_tx_left = 0;

    bool m_tx_ip = false;           // transmit buffer went empty
    bool m_rx_ip = false;           // a character came
    bool m_ius = false;             // RR2 read, no end of interrupt yet

    unsigned int m_sent = 0;
    unsigned int m_received = 0;

    void channel_reset(unsigned int channel);
    bool pending() const;
    void update();
    void start_shifter();
    void transmit_done();
    void write_control(unsigned int channel, unsigned int value);
    unsigned int read_control(unsigned int channel, bool direct);
    unsigned int vector_register() const;

public:
    E85Comm(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_e85_comm(InterfaceManager *im, EmulatorConfigDevice *cd);
