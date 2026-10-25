// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple II / //e memory management (Правец-82/8М/8А/8С, CM631)

#pragma once

#include "emulator/core.h"

// The whole 64K of an Apple II as the processor sees it: the main and the
// auxiliary RAM, the language card at $D000-$FFFF, the system ROM and the
// ROMs of the slots at $C100-$CFFF. Each 256-byte page reads and writes
// through a pointer into one of the buffers, so an access costs a table look
// up; the tables are rebuilt only when a switch changes what a page shows.
// The soft switches at $C000-$C01F and $C050-$C057 are decoded by the IOU
// (apple2-iou), which tells this device; the language card at $C080-$C08F is
// decoded here
class Apple2MMU: public AddressableDevice
{
public:
    enum Switch {
        SW_80STORE = 0, SW_RAMRD, SW_RAMWRT, SW_INTCXROM, SW_ALTZP, SW_SLOTC3ROM,
        SW_PAGE2, SW_HIRES,
        SW_LCBANK2, SW_LCRAM, SW_LCWRITE, SW_INTC8ROM,
        SW_COUNT
    };

private:
    bool m_iie = true;          // model: the //e switches exist
    bool m_lc = true;           // a language card ($D000-$FFFF RAM)

    RAM * m_ram = nullptr;      // 64K: $0000-$BFFF, the second $D000 bank at
                                // $C000-$CFFF of the buffer, $D000-$FFFF
    RAM * m_aux = nullptr;      // 64K pages of the same layout, may be absent
    ROM * m_rom = nullptr;
    unsigned m_rom_base = 0;    // address of the first ROM byte

    ROM * m_slot_rom[8] = {};   // $Cn00-$CnFF of each slot
    ROM * m_slot_exp[8] = {};   // $C800-$CFFF of each slot
    // A card that answers its pages itself (a status bit in its ROM page, ROM
    // banks of its own): such a page has no buffer, every access goes to the
    // device with the full address
    AddressableDevice * m_slot_dev[8] = {};
    AddressableDevice * m_exp_dev[8] = {};
    unsigned m_rom_offset[8] = {};
    unsigned m_exp_offset[8] = {};

    bool m_sw[SW_COUNT] = {};
    bool m_prewrite = false;    // the first of the two reads that enable writing
    unsigned m_exp_owner = 0;   // slot whose $C800 ROM answers, 0: none
    unsigned m_aux_bank = 0;    // 64K page of the auxiliary memory
    // A language card of several banks in slot 0 instead of the one of 16K
    // (the 128K card of the 82, КР3.088.184): 16K a bank - the first $D000
    // bank, the second, $E000-$FFFF - picked by $C0n4-$C0n7, $C0nC-$C0nF
    RAM * m_lc_ram = nullptr;
    unsigned m_lc_bank = 0;
    unsigned m_lc_banks = 0;
    unsigned m_aux_banks = 0;

    uint8_t * m_rd[256];
    uint8_t * m_wr[256];        // null: the write goes nowhere
    uint8_t m_float[256];       // what an empty slot answers

    uint8_t * main_buf() const { return m_ram->get_buffer(); }
    uint8_t * aux_buf() const;
    uint8_t * rom_page(unsigned address) const;
    void rebuild(unsigned lo = 0x00, unsigned hi = 0xFF);
    void lc_access(unsigned address, bool read);
    void cx_access(unsigned address);
    AddressableDevice * page_device(unsigned page) const;


public:
    Apple2MMU(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned get_value(unsigned address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;
    bool debug_writable(unsigned int address) const override;

    // From the IOU: a //e switch changed, or asked for its state ($C011-$C01F)
    void set_switch(Switch s, bool on);
    bool get_switch(Switch s) const { return m_sw[s]; }
    bool is_iie() const { return m_iie; }
    bool has_aux() const { return m_aux != nullptr; }
    void set_aux_bank(unsigned bank);
    // The page of the auxiliary memory the video reads in the 80 column and
    // double modes: the first 64K of the module
    const uint8_t * video_aux() const { return m_aux ? m_aux->get_buffer() : nullptr; }
    const uint8_t * video_main() const { return main_buf(); }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void state_restored() override;
};

ComputerDevice * create_apple2_mmu(InterfaceManager *im, EmulatorConfigDevice *cd);
