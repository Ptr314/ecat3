// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Slot cards of the Правец-8А that answer their own ROM pages

#pragma once

#include <deque>

#include "emulator/core.h"
#include "emulator/host_serial.h"
#include "emulator/devices/common/byte_tap.h"
#include "emulator/devices/common/ins8250.h"

// Both cards sit in two places of the bus: their slot page and $C800 through
// the MMU (slotN = card, slotN_exp = card), which passes the full address
// ($C100 and up), and the I/O space $C0n0-$C0nF through the mapper, which
// passes 0-$F.

// A parallel printer card of the Epson APL kind (and its Soviet and
// Bulgarian copies): a 2K ROM seen whole at $C800 and its first page at
// $Cn00. A write to $C0nX latches the byte and strobes the printer; BUSY is
// read as bit 7 of the slot page at $CnC0-$CnFF, where the ROM holds filler
// (the firmware polls $CnC1). The card of unknown origin of the 8А puts the
// byte out inverted (invert = 1)
class Apple2ParallelCard: public AddressableDevice
{
private:
    Interface i_data;
    Interface i_strobe;
    Interface i_busy;
    ROM * m_rom = nullptr;
    bool m_invert = false;
    unsigned m_written = 0;

public:
    Apple2ParallelCard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// U-LAN, the network card of СП «Вариант» and «Правец»: an MC6850 ACIA on a
// line shared by the whole class, 4K of ROM in two 2K banks.
//
//   $C0n3 (A2 = 0)  ACIA control (write) / status (read); A0, A1, A3 are not
//                   decoded, the firmware uses $C0nB as well
//   $C0n7 (A2 = 1)  ACIA data
//
// The RTS output of the ACIA is A11 of the ROM: control bits 6-5 = 10 (RTS
// high) show the second bank. The firmware switches banks by writing $03 or
// $41 until the bank number at $C901 is the one it wants. A read of $CFFF
// gives the station number (the firmware keeps bits 0-5); the ROM has 0
// there, so it is taken to come from switches on the card - parameter station.
//
// The line is a bus: every station hears what it sends, and the firmware
// checks it - a sent byte comes back to the receiver after its time on the
// line. It may go out to a host port (connect), which carries the bytes of
// the other stations in.
class PravetzULan: public AddressableDevice, public ByteTap
{
private:
    ROM * m_rom = nullptr;
    unsigned m_rom_offset = 0;
    unsigned m_station = 1;
    bool m_dcd = false;             // the DCD input as the status shows it

    uint8_t m_control = 0;
    uint8_t m_status = 0x02;        // TDRE
    uint8_t m_rdr = 0;
    std::deque<uint8_t> m_rx_queue;
    std::deque<uint8_t> m_echo;     // own bytes on their way back
    int64_t m_rx_left = 0;
    int64_t m_tx_left = 0;
    uint64_t m_char_ticks = 0;
    unsigned m_baud = 115200;
    unsigned m_sent = 0, m_received = 0;

    HostSerialPort m_host;
    std::string m_port_name;

    unsigned bank_base() const;
    unsigned rom_byte(unsigned offset) const;
    void write_control(uint8_t value);
    void update_rate();

public:
    PravetzULan(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// The IBM PC of the COMport card of СП «Вариант» (1996): COMPORT.EXE keeps
// 140K disk images (DOS order) and serves their sectors over the serial line
// to the card's driver, which the Правец sees as a DOS 3.3 drive; drive N is
// image N. Played here by the emulator itself, at the far end of the card's
// ins8250 (its parameter peer). The protocol, from the card ROM
// (docs-external/pravetz/cards/COMPORT_src/COMPORT-SPEC.md):
//   read   BF 01 D T S CS              -> BF 00 <256 bytes> SUM
//   write  BF 02 D T S <256 bytes> CS  -> BF 00 00
//   CS: the sum of the bytes after BF; a bad one is answered BF FF FF (send
//   again), a drive without an image or a sector past the disk BF 01 01
// Images are kept in memory: writes go there and into a snapshot, the file
// is written by the command save
class ComportServer: public ComputerDevice, public SerialPeer
{
private:
    static const unsigned IMAGE_SIZE = 35 * 16 * 256;
    INS8250 * m_uart = nullptr;
    std::vector<std::vector<uint8_t>> m_images;     // index = drive - 1, empty: none
    std::vector<std::string> m_files;
    std::vector<uint8_t> m_frame;                   // the request after BF
    bool m_in_frame = false;
    unsigned m_reads = 0, m_writes = 0, m_errors = 0;
    unsigned m_last = 0;        // the last request: command, drive, track, sector


    void answer(const std::vector<uint8_t> &bytes);
    void request();
    emulator::Result load_image(unsigned drive, const std::string &file);

public:
    ComportServer(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void serial_in(uint8_t value) override;
    ConfigFields get_config_fields() override;


    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    //The images are the server's own, as an fdd's: a snapshot carries what is
    //in them now, and the files the configuration names stay out of it
    bool state_owns_file(const std::string &parameter) const override
        { return parameter.compare(0, 5, "disk_") == 0 && parameter.compare(0, 11, "disk_title_") != 0; }
};

ComputerDevice * create_apple2_parallel(InterfaceManager *im, EmulatorConfigDevice *cd);

ComputerDevice * create_comport_server(InterfaceManager *im, EmulatorConfigDevice *cd);

ComputerDevice * create_pravetz_ulan(InterfaceManager *im, EmulatorConfigDevice *cd);
