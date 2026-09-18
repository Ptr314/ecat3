// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DEC DL11 serial line unit, going out through a host serial port

#pragma once

#include <deque>

#include "emulator/core.h"
#include "emulator/host_serial.h"

// A serial line of the PDP-11 kind: four registers, a receiver and a
// transmitter, each with its ready flag, interrupt enable and vector. У УК-НЦ
// это стык С2, 176570-176576, векторы 370 и 374.
//
//   +0  RCSR  разряд 7 - принят байт, 6 - разрешение прерывания,
//             12 - переполнение; программа пишет только разряд 6
//   +2  RBUF  принятый байт; чтение снимает разряды 7 и 12 RCSR
//   +4  XCSR  разряд 7 - передатчик свободен, 6 - разрешение прерывания,
//             2 - петля (переданное возвращается в приёмник), 0 - обрыв
//   +6  XBUF  запись - байт на передачу, снимает разряд 7 XCSR
//
// Байт идёт по линии время десяти посылок (8N1) на скорости baud. Линия
// выходит на последовательный порт хоста (параметр port или команда connect),
// и туда же можно подать байты командами send и sendfile - так сценарий
// играет роль машины на другом конце.
//
// Запрос прерывания однократный: он встаёт, когда готовность появляется при
// разрешённом прерывании (или прерывание разрешают при готовности), и
// снимается, когда процессор его взял (~iako) или условие пропало. Иначе
// свободный передатчик предлагал бы прерывание бесконечно.
class DL11: public AddressableDevice
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    Interface i_iako;

    unsigned int m_rcsr = 0;
    unsigned int m_rbuf = 0;
    unsigned int m_xcsr = 0200;         // передатчик свободен и до первого сброса
    unsigned int m_xbuf = 0;

    unsigned int m_rx_vector = 060;
    unsigned int m_tx_vector = 064;
    bool m_rx_pending = false;
    bool m_tx_pending = false;
    unsigned int m_offered = 0;

    // Время символа в тактах домена и отсчёты до конца передачи и до
    // следующей попытки приёма
    unsigned int m_baud = 9600;
    uint64_t m_char_ticks = 0;
    bool m_tx_busy = false;
    int64_t m_tx_left = 0;
    int64_t m_rx_left = 0;

    std::deque<uint8_t> m_rx_queue;     // поданное сценарием
    HostSerialPort m_host;
    std::string m_port_name;

    unsigned int m_sent = 0;
    unsigned int m_received = 0;
    static const unsigned int OUTPUT_SIZE = 1024;
    uint8_t m_output[OUTPUT_SIZE] = {};

    void receive(uint8_t value);
    void transmit_done();
    void set_rcsr(unsigned int value);
    void set_xcsr(unsigned int value);
    void update_irq();

public:
    DL11(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_dl11(InterfaceManager *im, EmulatorConfigDevice *cd);
