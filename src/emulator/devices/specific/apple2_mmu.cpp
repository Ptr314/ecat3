// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple II / //e memory management (Правец-82/8М/8А/8С, CM631)

#include <algorithm>
#include <climits>
#include <cstring>

#include "apple2_mmu.h"
#include "emulator/utils.h"

static const char * const SWITCH_NAMES[Apple2MMU::SW_COUNT] = {
    "80store", "ramrd", "ramwrt", "intcxrom", "altzp", "slotc3rom",
    "page2", "hires", "lcbank2", "lcram", "lcwrite", "intc8rom"
};

Apple2MMU::Apple2MMU(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    std::memset(m_float, 0xFF, sizeof(m_float));
    for (unsigned i = 0; i < 256; i++) { m_rd[i] = m_float; m_wr[i] = nullptr; }
}

emulator::Result Apple2MMU::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    const std::string model = str_tolower(read_confg_value(cd, "model", false, std::string("iie")));
    if (model == "iie") m_iie = true;
    else if (model == "ii") m_iie = false;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "Unknown model, ii or iie expected")) + "} " + model);

    m_ram = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_rom = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("rom").value));
    const std::string aux = cd->get_parameter("aux", false).value;
    if (!aux.empty()) m_aux = dynamic_cast<RAM*>(im->dm->get_device_by_name(aux));
    if (m_ram == nullptr || m_rom == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "A RAM and a ROM are expected")) + "} " + name);

    // The language card needs the whole 64K buffer: the first $D000 bank is
    // kept at $C000-$CFFF of it, where the processor never sees RAM. A //e has
    // it always
    m_lc = m_iie || read_confg_value(cd, "lc", false, true);
    const std::string lc_ram = cd->get_parameter("lc_ram", false).value;
    if (!lc_ram.empty()) {
        m_lc_ram = dynamic_cast<RAM*>(im->dm->get_device_by_name(lc_ram));
        m_lc_banks = (m_lc_ram != nullptr) ? m_lc_ram->get_size() / 0x4000 : 0;
        if (m_lc_banks == 0 || m_iie)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "The language card RAM is 16K banks of a ][")) + "} " + lc_ram);
        m_lc = true;
    }
    if (m_lc && m_lc_ram == nullptr && m_ram->get_size() < 0x10000)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "The language card needs 64K of RAM")) + "} " + name);
    if (m_ram->get_size() < 0xC000)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "The RAM must be 48K at least")) + "} " + name);
    if (m_aux != nullptr) {
        m_aux_banks = m_aux->get_size() / 0x10000;
        if (m_aux_banks == 0 || !m_iie)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "The auxiliary RAM is 64K pages of a //e")) + "} " + name);
    }

    m_rom_base = read_confg_value(cd, "rom_base", false, (unsigned)(0x10000 - m_rom->get_size()));
    if ((m_rom_base & 0xFF) != 0 || m_rom_base + m_rom->get_size() > 0x10000)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "The ROM must start on a page and end by $FFFF")) + "} " + name);

    // "slotN = rom" or "slotN = rom[$offset]": the 8С keeps the firmware of
    // its built-in slots 1, 2 and 6 in one ROM (D38), at $100, $200, $600.
    // Any other device answers the pages itself (no offset then)
    for (unsigned n = 1; n <= 7; n++) {
        for (unsigned exp = 0; exp < 2; exp++) {
            const EmulatorConfigParameter p = cd->get_parameter("slot" + std::to_string(n) + (exp ? "_exp" : ""), false);
            const std::string r = p.value;
            if (r.empty()) continue;
            std::string range = str_trim(p.right_range);
            if (range.size() >= 2 && range.front() == '[' && range.back() == ']') range = range.substr(1, range.size() - 2);
            unsigned offset = 0;
            try {
                if (!range.empty()) offset = parse_numeric_value(range, 10);
            } catch (const std::exception &) {
                offset = UINT_MAX;
            }
            ROM * rom = dynamic_cast<ROM*>(im->dm->get_device_by_name(r));
            if (rom == nullptr && range.empty()) {
                AddressableDevice * dev = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(r));
                if (dev != nullptr) {
                    if (exp) m_exp_dev[n] = dev; else m_slot_dev[n] = dev;
                    continue;
                }
            }
            if (rom == nullptr || offset > rom->get_size() || rom->get_size() - offset < (exp ? 0x800u : 0x100u))
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{Apple2MMU|" + std::string(QT_TRANSLATE_NOOP("Apple2MMU", "A slot ROM is not a ROM or the offset is past its end")) + "} " + r);
            if (exp) { m_slot_exp[n] = rom; m_exp_offset[n] = offset; }
            else     { m_slot_rom[n] = rom; m_rom_offset[n] = offset; }
        }
    }

    Apple2MMU::reset(true);
    return emulator::Result::ok();
}

void Apple2MMU::reset(bool cold)
{
    (void)cold;
    // Every //e switch goes off. The language card reads ROM and, by the book
    // of the Правец-8А (5.9), lets the RAM be written, in its second bank
    for (unsigned i = 0; i < SW_COUNT; i++) m_sw[i] = false;
    m_sw[SW_LCBANK2] = true;
    m_sw[SW_LCWRITE] = true;
    m_prewrite = false;
    m_exp_owner = 0;
    m_aux_bank = 0;
    m_lc_bank = 0;
    if (m_ram != nullptr) rebuild();
}

uint8_t * Apple2MMU::aux_buf() const
{
    return m_aux->get_buffer() + (m_aux_bank % m_aux_banks) * 0x10000;
}

uint8_t * Apple2MMU::rom_page(unsigned address) const
{
    if (address < m_rom_base || address - m_rom_base >= m_rom->get_size()) return const_cast<uint8_t*>(m_float);
    return m_rom->get_buffer() + (address - m_rom_base);
}

static uint8_t * slot_rom_buf(ROM * r, unsigned offset, uint8_t * none)
{
    if (r == nullptr || offset >= r->get_size()) return none;
    return r->get_buffer() + offset;
}

// Pages lo..hi only: the 80 column firmware flips PAGE2 under 80STORE twice
// a character, and ProDOS reads $C08x on every call
void Apple2MMU::rebuild(unsigned lo, unsigned hi)
{
    uint8_t * const main = main_buf();
    uint8_t * const aux = (m_aux != nullptr) ? aux_buf() : main;
    const bool has_aux = m_aux != nullptr;

    // $0000-$01FF: the zero page and the stack
    for (unsigned p = std::max(lo, 0x00u); p < 0x02 && p <= hi; p++) {
        uint8_t * b = (has_aux && m_sw[SW_ALTZP]) ? aux : main;
        m_rd[p] = m_wr[p] = b + (p << 8);
    }
    // $0200-$BFFF. 80STORE gives the screen pages to PAGE2, whatever
    // RAMRD/RAMWRT say: the text page always, the hires one with HIRES
    for (unsigned p = std::max(lo, 0x02u); p < 0xC0 && p <= hi; p++) {
        bool rd_aux = has_aux && m_sw[SW_RAMRD];
        bool wr_aux = has_aux && m_sw[SW_RAMWRT];
        if (has_aux && m_sw[SW_80STORE]
            && ((p >= 0x04 && p < 0x08) || (m_sw[SW_HIRES] && p >= 0x20 && p < 0x40)))
            rd_aux = wr_aux = m_sw[SW_PAGE2];
        m_rd[p] = (rd_aux ? aux : main) + (p << 8);
        m_wr[p] = (wr_aux ? aux : main) + (p << 8);
    }

    // $C100-$CFFF: the ROMs. On a //e the internal one replaces the slots
    // (INTCXROM), or only slot 3 (SLOTC3ROM off) and then $C800 as well
    // (INTC8ROM, set by an access to $C3xx)
    if (lo <= 0xC0 && hi >= 0xC0) {
        m_rd[0xC0] = m_float;
        m_wr[0xC0] = nullptr;
    }
    for (unsigned n = 1; n <= 7; n++) {
        const unsigned p = 0xC0 + n;
        if (p < lo || p > hi) continue;
        bool internal = false;
        if (m_iie) internal = m_sw[SW_INTCXROM] || (n == 3 && !m_sw[SW_SLOTC3ROM]);
        if (!internal && m_slot_dev[n] != nullptr)
            m_rd[p] = nullptr;
        else
            m_rd[p] = internal ? rom_page(p << 8) : slot_rom_buf(m_slot_rom[n], m_rom_offset[n], m_float);
        m_wr[p] = nullptr;
    }
    const bool c8_internal = m_iie && (m_sw[SW_INTCXROM] || m_sw[SW_INTC8ROM]);
    for (unsigned p = std::max(lo, 0xC8u); p < 0xD0 && p <= hi; p++) {
        if (c8_internal)
            m_rd[p] = rom_page(p << 8);
        else if (m_exp_owner != 0 && m_exp_dev[m_exp_owner] != nullptr)
            m_rd[p] = nullptr;
        else
            m_rd[p] = slot_rom_buf(m_exp_owner ? m_slot_exp[m_exp_owner] : nullptr, m_exp_offset[m_exp_owner] + ((p - 0xC8) << 8), m_float);
        m_wr[p] = nullptr;
    }

    // $D000-$FFFF: ROM or the language card, of the main memory or, with
    // ALTZP, of the auxiliary one. The first $D000 bank sits at $C000
    uint8_t * const lc = (has_aux && m_sw[SW_ALTZP]) ? aux : main;
    for (unsigned p = std::max(lo, 0xD0u); p < 0x100 && p <= hi; p++) {
        uint8_t * ram = nullptr;
        if (m_lc_ram != nullptr) {
            // A bank of the card: $D000 bank 1, bank 2, then $E000-$FFFF
            uint8_t * const bank = m_lc_ram->get_buffer() + m_lc_bank * 0x4000;
            ram = (p < 0xE0) ? bank + ((p - 0xD0) << 8) + (m_sw[SW_LCBANK2] ? 0x1000 : 0)
                             : bank + 0x2000 + ((p - 0xE0) << 8);
        } else if (m_lc) {
            unsigned a = p << 8;
            if (p < 0xE0 && !m_sw[SW_LCBANK2]) a -= 0x1000;
            ram = lc + a;
        }
        m_rd[p] = (ram != nullptr && m_sw[SW_LCRAM]) ? ram : rom_page(p << 8);
        m_wr[p] = (ram != nullptr && m_sw[SW_LCWRITE]) ? ram : nullptr;
    }
}

// $C080-$C08F. A0 and A1 pick what is read (RAM when they are equal), A3 the
// $D000 bank (0: the second). Writing is enabled by two reads in a row of an
// odd address; an even one disables it, a write breaks the pair
void Apple2MMU::lc_access(unsigned address, bool read)
{
    // A card of several banks takes A2 for its bank register: $C0n4-$C0n7
    // pick banks 0-3, $C0nC-$C0nF 4-7, and the card switches stay as they are
    // (the Saturn 128K card does so; the 128K card of the 82 is said to be
    // its copy, the Legend one)
    if (m_lc_ram != nullptr && (address & 4) != 0) {
        const unsigned bank = ((address & 3) | ((address & 8) >> 1)) % m_lc_banks;
        if (bank != m_lc_bank) {
            m_lc_bank = bank;
            rebuild(0xD0, 0xFF);
        }
        return;
    }
    const bool odd = (address & 1) != 0;
    const bool was_write = m_sw[SW_LCWRITE];
    if (odd) {
        if (read) {
            if (m_prewrite) m_sw[SW_LCWRITE] = true;
            m_prewrite = true;
        } else
            m_prewrite = false;
    } else {
        m_sw[SW_LCWRITE] = false;
        m_prewrite = false;
    }
    const bool was_ram = m_sw[SW_LCRAM], was_bank2 = m_sw[SW_LCBANK2];
    m_sw[SW_LCRAM] = ((address ^ (address >> 1)) & 1) == 0;
    m_sw[SW_LCBANK2] = (address & 8) == 0;
    if (was_ram != m_sw[SW_LCRAM] || was_bank2 != m_sw[SW_LCBANK2] || was_write != m_sw[SW_LCWRITE])
        rebuild(0xD0, 0xFF);
}

// Any access to $C100-$CFFF: a slot's own ROM claims $C800 for its
// expansion ROM, $C3xx with SLOTC3ROM off takes it for the internal one
// (whatever INTCXROM says, Sather 5-28), and $CFFF lets it go
void Apple2MMU::cx_access(unsigned address)
{
    const unsigned page = address >> 8;
    bool changed = false;
    if (page >= 0xC1 && page <= 0xC7) {
        const unsigned n = page & 7;
        if (m_iie && n == 3 && !m_sw[SW_SLOTC3ROM]) {
            if (!m_sw[SW_INTC8ROM]) { m_sw[SW_INTC8ROM] = true; changed = true; }
        } else if (!(m_iie && m_sw[SW_INTCXROM]) && (m_slot_exp[n] != nullptr || m_exp_dev[n] != nullptr) && m_exp_owner != n) {
            m_exp_owner = n;
            changed = true;
        }
    } else if (address == 0xCFFF) {
        if (m_sw[SW_INTC8ROM] || m_exp_owner != 0) {
            m_sw[SW_INTC8ROM] = false;
            m_exp_owner = 0;
            changed = true;
        }
    }
    if (changed) rebuild(0xC1, 0xCF);
}

// The card behind a page without a buffer: its slot page or, for $C800, the
// slot that owns the expansion space
AddressableDevice * Apple2MMU::page_device(unsigned page) const
{
    return (page < 0xC8) ? m_slot_dev[page & 7] : m_exp_dev[m_exp_owner];
}

unsigned Apple2MMU::get_value(unsigned address)
{
    if ((address & 0xF000) == 0xC000) {
        if ((address & 0xFFF0) == 0xC080) {
            lc_access(address, true);
            return _FFFF;
        }
        // The read of $CFFF still gets the byte of the ROM that answers in
        // that cycle; the release comes after it (a U-LAN card puts its
        // station number there)
        if (address == 0xCFFF) {
            const unsigned v = (m_rd[0xCF] == nullptr) ? page_device(0xCF)->get_value(address) : m_rd[0xCF][0xFF];
            cx_access(address);
            return v;
        }
        cx_access(address);
        const unsigned page = (address >> 8) & 0xFF;
        if (m_rd[page] == nullptr) return page_device(page)->get_value(address);
    }

    return m_rd[(address >> 8) & 0xFF][address & 0xFF];
}

unsigned Apple2MMU::get_direct(unsigned address)
{
    if ((address & 0xFFF0) == 0xC080) return _FFFF;
    const unsigned page = (address >> 8) & 0xFF;
    if (m_rd[page] == nullptr) return page_device(page)->get_direct(address);
    return m_rd[page][address & 0xFF];
}

void Apple2MMU::set_value(unsigned address, unsigned value, bool force)
{
    if ((address & 0xF000) == 0xC000) {
        if ((address & 0xFFF0) == 0xC080) {
            lc_access(address, false);
            return;
        }
        cx_access(address);
        const unsigned page = (address >> 8) & 0xFF;
        if (m_rd[page] == nullptr) {
            page_device(page)->set_value(address, value, force);
            return;
        }
    }
    uint8_t * const b = m_wr[(address >> 8) & 0xFF];
    if (b != nullptr)
        b[address & 0xFF] = static_cast<uint8_t>(value);
    else if (force && address < 0xC000)
        m_rd[(address >> 8) & 0xFF][address & 0xFF] = static_cast<uint8_t>(value);
}

bool Apple2MMU::debug_writable(unsigned int address) const
{
    return m_wr[(address >> 8) & 0xFF] != nullptr;
}

void Apple2MMU::set_switch(Switch s, bool on)
{
    if (m_sw[s] == on) return;
    m_sw[s] = on;
    switch (s) {
        // PAGE2 and HIRES move memory only under 80STORE
        case SW_PAGE2: case SW_HIRES:
            if (m_sw[SW_80STORE]) rebuild(0x04, 0x3F);
            break;
        case SW_80STORE:              rebuild(0x04, 0x3F); break;
        case SW_RAMRD: case SW_RAMWRT: rebuild(0x02, 0xBF); break;
        case SW_INTCXROM: case SW_SLOTC3ROM: rebuild(0xC1, 0xCF); break;
        default:                      rebuild(); break;
    }
}

void Apple2MMU::set_aux_bank(unsigned bank)
{
    if (m_aux == nullptr) return;
    bank %= m_aux_banks;
    if (bank == m_aux_bank) return;
    m_aux_bank = bank;
    rebuild();
}

std::vector<DeviceFieldInfo> Apple2MMU::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"switches", "The //e and language card switches that are on", false});
    r.push_back({"map",      "Where each 4K of the address space reads from",  false});
    r.push_back({"aux_bank", "64K page of the auxiliary memory",                false});
    r.push_back({"exp_owner", "Slot whose $C800 ROM answers, 0: none",          false});
    r.push_back({"lc_bank",  "16K bank of a language card of several banks",    false});
    return r;
}

bool Apple2MMU::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "switches") {
        out.numeric = false;
        out.text.clear();
        for (unsigned i = 0; i < SW_COUNT; i++)
            if (m_sw[i]) out.text += (out.text.empty() ? "" : " ") + std::string(SWITCH_NAMES[i]);
        if (m_prewrite) out.text += " prewrite";
        return true;
    }
    if (field == "map") {
        out.numeric = false;
        out.text.clear();
        for (unsigned p = 0; p < 0x100; p += 0x10) {
            const uint8_t * b = m_rd[p];
            std::string where;
            if (b == nullptr) where = "D";
            else if (b == m_float) where = "-";

            else if (b >= main_buf() && b < main_buf() + m_ram->get_size()) where = "M" + hex_str((unsigned)(b - main_buf()), 4);
            else if (m_lc_ram != nullptr && b >= m_lc_ram->get_buffer() && b < m_lc_ram->get_buffer() + m_lc_ram->get_size())
                where = "L" + hex_str((unsigned)(b - m_lc_ram->get_buffer()), 5);
            else if (m_aux != nullptr && b >= m_aux->get_buffer() && b < m_aux->get_buffer() + m_aux->get_size())
                where = "X" + hex_str((unsigned)(b - m_aux->get_buffer()), 5);
            else if (b >= m_rom->get_buffer() && b < m_rom->get_buffer() + m_rom->get_size())
                where = "R" + hex_str((unsigned)(b - m_rom->get_buffer()), 4);
            else where = "S";
            if (m_wr[p] == nullptr) where += "/ro";
            out.text += (p ? " " : "") + hex_str(p, 2) + ":" + where;
        }
        return true;
    }
    if (field == "aux_bank" || field == "exp_owner" || field == "lc_bank") {
        out.numeric = true;
        out.values.push_back(field == "aux_bank" ? m_aux_bank : field == "lc_bank" ? m_lc_bank : m_exp_owner);
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

void Apple2MMU::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    for (unsigned i = 0; i < SW_COUNT; i++) w.b(SWITCH_NAMES[i], m_sw[i]);
    w.b("prewrite", m_prewrite);
    w.n("exp_owner", m_exp_owner);
    w.n("aux_bank", m_aux_bank);
    if (m_lc_ram != nullptr) w.n("lc_bank", m_lc_bank);
}

emulator::Result Apple2MMU::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    for (unsigned i = 0; i < SW_COUNT; i++) r.b(SWITCH_NAMES[i], m_sw[i]);
    r.b("prewrite", m_prewrite);
    r.u("exp_owner", m_exp_owner);
    r.u("aux_bank", m_aux_bank);
    r.u("lc_bank", m_lc_bank);
    if (m_exp_owner > 7) m_exp_owner = 0;
    if (m_lc_banks == 0 || m_lc_bank >= m_lc_banks) m_lc_bank = 0;
    return emulator::Result::ok();
}

void Apple2MMU::state_restored()
{
    AddressableDevice::state_restored();
    rebuild();
}

ComputerDevice * create_apple2_mmu(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Apple2MMU(im, cd);
}
