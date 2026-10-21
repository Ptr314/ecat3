// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8255 (КР580ВВ55) PPI device

#pragma once

#include "emulator/core.h"

class I8255:public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_port_a;
    Interface i_port_b;
    Interface i_port_ch;
    Interface i_port_cl;

    uint8_t registers [4];

    //Управляющее слово, с которым микросхема оказывается после сброса. У
    //отдельной ВВ55 его пишет программа, но в машине порт бывает и жёстко
    //разведённым: параллельный порт УК-НЦ всегда выводит порт A, и ни ПЗУ, ни
    //программы управляющего слова ему не пишут
    unsigned int m_control_default;
    bool m_has_control_default;

public:
    I8255(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
    virtual void reset(bool cold) override;
    //Word cycles come only from a 16-bit processor, and on its bus (the УК-НЦ
    //ПП) the chip sits on the low byte and decodes A0 itself: a word touches
    //the one register its address selects, even or odd, the high byte is not
    //driven (reads 0) and not taken. RBTRON.SAV tests the printer with
    //BIT #2,@#177101 and drops the strobe with BIC #200,@#177102; as two
    //bytes, the second would write the control word back and clear the ports
    bool byte_wide() const override { return true; }
    unsigned int get_value_word(unsigned int address) override { return get_value(address) & 0xFF; }
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override { set_value(address, value & 0xFF, force); }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_i8255(InterfaceManager *im, EmulatorConfigDevice *cd);
