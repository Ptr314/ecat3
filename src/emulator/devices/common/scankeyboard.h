// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scanning matrix-based keyboard device

#pragma once

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
    Interface i_shift;
    Interface i_ctrl;
    Interface i_ruslat;
    Interface i_ruslat_led;

    unsigned int scan_lines = 0;
    unsigned int out_lines = 0;

    std::vector<ScanData> scan_data;
    std::vector<ScanKeyId> id_data;
    unsigned int key_array[15];

    unsigned int code_ctrl;
    unsigned int code_shift;
    unsigned int code_ruslat;

    unsigned int stored_shift = 1;

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
    ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;

    emulator::Result load_config(SystemData *sd) override;

    void reset(bool cool) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
