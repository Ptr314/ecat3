// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scanning matrix-based keyboard device

#pragma once

#include <atomic>

#include "emulator/devices/common/keyboard.h"

struct ScanData {
    unsigned int key_code;
    unsigned int scan_line;
    unsigned int out_line;
    int shift_state;
};

// A key of the machine's own keyboard: a position of the matrix, named the way
// the native table and the drawing name it
struct ScanKeyId {
    std::string id;
    unsigned int scan_line;
    unsigned int out_line;
};

class ScanKeyboard: public Keyboard
{
private:
    Interface i_scan;
    Interface i_output;
    // Return lines 8-15, for a matrix wider than a byte (the Поиск-1 reads
    // twelve). A line of its own, so that the 8-bit output of every other
    // machine stays what it was
    Interface i_output_hi;
    Interface i_shift;
    Interface i_ctrl;
    Interface i_ruslat;
    Interface i_ruslat_led;
    // The Oric: the columns are selected from outside (port A of the AY),
    // 0 the selected ones, and the machine reads one line - 1 when a key of
    // the scanned row is down in a selected column
    Interface i_colmask;
    Interface i_sense;
    // A key outside the matrix wired to the processor's NMI, 0 while it is
    // down: RESET of the Oric (SW1 of the Oric-1 schematic goes to NMI)
    Interface i_nmi;
    unsigned int code_nmi = _FFFF;
    // The key comes from the window's thread, and NMI is an edge the processor
    // takes and clears on its own: the level waits here for clock(). -1: none
    std::atomic<int> m_nmi_pending{-1};

    unsigned int scan_lines = 0;
    unsigned int out_lines = 0;

    // The scan lines are driven one at a time through an external decoder, and
    // what the machine writes is the number of the line rather than a mask of
    // them. The Юниор has eleven columns behind a К555ИД3 and only the low
    // four bits of the port to say which one - a matrix of that width does not
    // fit a byte at all
    bool decoded_scan = false;
    // Of the scan port, the bits that carry the line number (decoder_bits):
    // four on the Юниор, three on the Oric (PB0-PB2 of its VIA)
    unsigned int decoded_mask = 0x0F;

    std::vector<ScanData> scan_data;
    std::vector<ScanKeyId> id_data;
    unsigned int key_array[15];

    unsigned int code_ctrl;
    unsigned int code_shift;
    unsigned int code_ruslat;
    // For TYPE only: the host key the firmware switches РУС/ЛАТ with when the
    // key is a contact of the matrix (Юниор, Арго: Caps Lock; Поиск: F12, its
    // right Ctrl), and where the Russian letters are - КОИ-7 Latin keys, or
    // ЙЦУКЕН as on a PC, capitals with Shift (Поиск)
    unsigned int m_rus_key = _FFFF;
    bool m_rus_jcuken = false;
    // КОИ-7, but the five letters off the Latin alphabet - Ю Ш Э Щ Ч, the
    // keys @ [ \ ] ^ with Shift in either register (Правец-8Д) - are typed
    // as their codes in ASCII, ` { | } ~, and the map puts those there. The
    // alphabet is the Bulgarian one: Ъ on the key Y, where КОИ-7 has Ы
    bool m_rus_koi7_shift = false;
    unsigned int m_rus_yo = EmuKey::QuoteLeft;     // Ё of the ЙЦУКЕН: left of 1 on a PC

    unsigned int stored_shift = 1;

    // A machine whose Shift is a key of the matrix (the Юниор) has no line for
    // the forced Shift of the map entries (^ and _): they press or release this
    // contact instead. -1 when the config names none
    int mshift_scan = -1;
    int mshift_out = -1;
    bool mshift_saved = false;      // the contact as it was before the forcing
    void set_matrix_shift(bool pressed);
    bool matrix_shift_pressed() const;

    // The Rus/Lat indicator line as last driven, and as last taken into the
    // register; -1 until the machine drives it. See interface_callback()
    int led_line = -1;
    int led_taken = -1;

    void calculate_out();

    // Ids for the highlight of keys typed on the host keyboard
    std::string id_at(unsigned int scan, unsigned int out) const;
    std::string role_id(KeyRole role) const;

protected:
    void set_rus(bool new_rus) override;

    emulator::Result parse_key_table(const std::vector<std::string> &body, const std::string &file) override;
    void send_key_id(const std::string &id, bool press) override;
    void set_shift_state(bool pressed) override;
    void set_ctrl_state(bool pressed) override;

public:
    unsigned int rus_switch_key() const override
        { return (code_ruslat != 0 && code_ruslat != _FFFF) ? code_ruslat : m_rus_key; }
    bool key_for_char(unsigned int codepoint, unsigned int &key, bool &shift) override;
    ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;

    emulator::Result load_config(SystemData *sd) override;

    void reset(bool cool) override;
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
