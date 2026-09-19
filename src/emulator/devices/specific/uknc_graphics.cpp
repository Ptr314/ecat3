// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ graphics unit - octet drawing

#include "uknc_graphics.h"
#include "emulator/utils.h"

#define R_COLOR     0       // 177016
#define R_BG_LOW    1       // 177020
#define R_BG_HIGH   2       // 177022
#define R_OCTET     3       // 177024
#define R_MASK      4       // 177026
#define R_COUNT     5

UKNCGraphics::UKNCGraphics(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 16, "address", MODE_R)
{
    can_read = true;
    can_write = true;
    addresable_size = R_COUNT * 2;
}

emulator::Result UKNCGraphics::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    static const char * names[3] = {"plane0", "plane1", "plane2"};
    for (unsigned i = 0; i < 3; i++) {
        m_plane[i] = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter(names[i]).value, false));
        if (m_plane[i] == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCGraphics|" + std::string(QT_TRANSLATE_NOOP("UKNCGraphics", "Plane must be a RAM device")) + "} "
                + name + ": " + names[i]);
    }

    return emulator::Result::ok();
}

void UKNCGraphics::reset(MAYBE_UNUSED bool cold)
{
    m_color = m_bg_low = m_bg_high = m_octet = m_mask = 0;
    m_draws = 0;
}

//------------------------- Drawing ----------------------------------------//

// The background registers hold eight dots of three bits each, one triad per
// nibble: dot 0 in bits 0-2 of 177020, dot 7 in bits 12-14 of 177022
void UKNCGraphics::unpack_background(uint8_t out[3]) const
{
    out[0] = out[1] = out[2] = 0;
    for (unsigned k = 0; k < 8; k++) {
        const unsigned reg = (k < 4)? m_bg_low : m_bg_high;
        const unsigned triad = (reg >> ((k & 3) * 4)) & 7;
        for (unsigned p = 0; p < 3; p++)
            if (triad & (1u << p)) out[p] |= (uint8_t)(1u << k);
    }
}

void UKNCGraphics::pack_background(const uint8_t in[3])
{
    m_bg_low = m_bg_high = 0;
    for (unsigned k = 0; k < 8; k++) {
        unsigned triad = 0;
        for (unsigned p = 0; p < 3; p++)
            if (in[p] & (1u << k)) triad |= (1u << p);
        if (k < 4) m_bg_low  |= triad << (k * 4);
        else       m_bg_high |= triad << ((k & 3) * 4);
    }
}

void UKNCGraphics::load_background()
{
    // Reading 177024 takes the eight dots that are already on the screen at the
    // current address, so that a program can draw over them
    const unsigned int a = address();
    uint8_t planes[3];
    for (unsigned p = 0; p < 3; p++)
        planes[p] = (uint8_t)(m_plane[p]->get_direct(a) & 0xFF);
    pack_background(planes);
}

void UKNCGraphics::draw_octet(unsigned int octet)
{
    m_octet = octet & 0xFF;

    // The background of the eight dots, as the program left it
    uint8_t planes[3];
    unpack_background(planes);

    // Every bit set in the octet takes the foreground colour of 177016, every
    // clear bit keeps the background
    for (unsigned p = 0; p < 3; p++) {
        planes[p] &= (uint8_t)~m_octet;
        if (m_color & (1u << p)) planes[p] |= (uint8_t)m_octet;
    }

    // A plane whose mask bit is set is left alone
    const unsigned int a = address();
    for (unsigned p = 0; p < 3; p++)
        if ((m_mask & (1u << p)) == 0)
            m_plane[p]->set_value(a, planes[p]);

    m_draws++;
}

//------------------------- Bus access -------------------------------------//

unsigned int UKNCGraphics::get_value_word(unsigned int address)
{
    return read_register(address, false);
}

// The debugger and LOG read 177024 as zero without loading the background
unsigned UKNCGraphics::get_direct(unsigned address)
{
    const unsigned int w = read_register(address & ~1u, true);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

unsigned int UKNCGraphics::read_register(unsigned int address, bool peek)
{
    switch (address >> 1) {
    case R_COLOR:   return m_color & 7;
    case R_BG_LOW:  return m_bg_low & 0xFFFF;
    case R_BG_HIGH: return m_bg_high & 0xFFFF;
    case R_OCTET:
        // A read is a command, not a value: it loads the background from the
        // screen and answers zero
        if (!peek) load_background();
        return 0;
    case R_MASK:    return m_mask & 7;
    default:        return 0;
    }
}

void UKNCGraphics::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    switch (address >> 1) {
    case R_COLOR:   m_color = value & 7; break;
    case R_BG_LOW:  m_bg_low = value & 0xFFFF; break;
    case R_BG_HIGH: m_bg_high = value & 0xFFFF; break;
    case R_OCTET:   draw_octet(value); break;
    case R_MASK:    m_mask = value & 7; break;
    default: break;
    }
}

unsigned int UKNCGraphics::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void UKNCGraphics::set_value(unsigned int address, unsigned int value, bool force)
{
    const unsigned int b = value & 0xFF;

    // A register sees a byte as a word with the byte on its own lane and
    // zeros on the other, as UKNCBTL has it. Nothing is read back first:
    // reading 177024 loads the background from the screen, and a MOVB to it
    // would otherwise draw over the dots under the octet instead of over the
    // background the program put in 177020/177022
    set_value_word(address & ~1u, (address & 1)? (b << 8) : b, force);
}

//------------------------- Introspection ----------------------------------//

std::vector<DeviceFieldInfo> UKNCGraphics::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"address",    "Address register 177010, from pp-indirect",  false});
    r.push_back({"color",      "Foreground colour of a dot, 177016",         false});
    r.push_back({"background", "Background of the eight dots, 177020/177022", false});
    r.push_back({"mask",       "Planes writing is inhibited for, 177026",    false});
    r.push_back({"octet",      "Last octet drawn through 177024",            false});
    r.push_back({"draws",      "Octets drawn since the machine came up",     false});
    return r;
}

bool UKNCGraphics::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "address")    { out.values.push_back(address()); return true; }
    if (field == "color")      { out.values.push_back(m_color);   return true; }
    if (field == "mask")       { out.values.push_back(m_mask);    return true; }
    if (field == "octet")      { out.values.push_back(m_octet);   return true; }
    if (field == "draws")      { out.values.push_back(m_draws);   return true; }
    if (field == "background") {
        out.values.push_back(m_bg_low);
        out.values.push_back(m_bg_high);
        return true;
    }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_graphics(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCGraphics(im, cd);
}
