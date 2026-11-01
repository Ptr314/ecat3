// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: SCN2661 (КМ1818ВВ61) serial controller, asynchronous mode

#pragma once

#include "emulator/core.h"

// Four registers on word addresses: data, status, mode 1/2 (one address, a
// pointer steps between the two), command. The line to a device of the emulator
// goes whole bytes, in the time of a character (~txd, ~rxd), as with dl11; the
// requests are the chip's TXRDY and RXRDY pins (~txrdy, ~rxrdy, high when active).
class SCN2661: public AddressableDevice
{
private:
    Interface i_txd;
    Interface i_rxd;
    Interface i_txrdy;
    Interface i_rxrdy;
    Interface i_dsr;

    uint8_t m_mode[2] = {};
    unsigned int m_mode_ptr = 0;
    uint8_t m_command = 0;
    uint8_t m_status = 0;           // the error bits and RXRDY
    uint8_t m_rx = 0;
    uint8_t m_thr = 0;
    uint8_t m_tsr = 0;
    bool m_thr_full = false;
    bool m_tsr_busy = false;
    int64_t m_tx_left = 0;

    unsigned int m_osc = 5068800;

    unsigned int m_sent = 0;
    unsigned int m_received = 0;
    unsigned int m_overruns = 0;

    uint64_t char_ticks() const;
    void load_shifter();
    void transmit_done();
    void receive(uint8_t value);
    void update_lines();
    unsigned int read_status() const;
    void write_command(unsigned int value);

public:
    SCN2661(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

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

ComputerDevice * create_scn2661(InterfaceManager *im, EmulatorConfigDevice *cd);
