// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Apple II / //e input-output unit (Правец-82/8М/8А/8С, CM632)

#include "apple2_iou.h"
#include "apple2_mmu.h"
#include "emulator/utils.h"

Apple2IOU::Apple2IOU(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_pb(this, im, 3, "pb", MODE_R)
    , i_tape_in(this, im, 1, "tape_in", MODE_R)
    , i_tape_out(this, im, 1, "tape_out", MODE_W)
    , i_softsw(this, im, 1, "softsw", MODE_W)
    , i_an(this, im, 4, "an", MODE_W)
{
    m_clocked = true;   //the paddle timers count the processor's clocks
}

emulator::Result Apple2IOU::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    const std::string model = str_tolower(read_confg_value(cd, "model", false, std::string("iie")));
    if (model == "iie") m_iie = true;
    else if (model == "ii") m_iie = false;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2IOU|" + std::string(QT_TRANSLATE_NOOP("Apple2IOU", "Unknown model, ii or iie expected")) + "} " + model);

    m_pravetz = read_confg_value(cd, "pravetz", false, false);
    // The tape output starts low: a line starts at _FFFF, and the first
    // toggle would make no edge
    i_tape_out.change(0);
    m_altchar_on_read = read_confg_value(cd, "altchar_on_read", false, false);
    m_pb_fixed = read_confg_value(cd, "pb_fixed", false, (unsigned)0) & 7;
    for (unsigned n = 0; n < 4; n++)
        m_pdl[n] = read_confg_value(cd, "pdl" + std::to_string(n), false, (unsigned)255) & 0xFF;

    m_mmu = dynamic_cast<Apple2MMU*>(im->dm->get_device_by_name(cd->get_parameter("mmu").value));
    if (m_mmu == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2IOU|" + std::string(QT_TRANSLATE_NOOP("Apple2IOU", "An apple2-mmu device is expected")) + "} " + name);

    const std::string kbd = cd->get_parameter("keyboard", false).value;
    if (!kbd.empty()) {
        m_kbd = dynamic_cast<Apple2KeyLatch*>(im->dm->get_device_by_name(kbd));
        if (m_kbd == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Apple2IOU|" + std::string(QT_TRANSLATE_NOOP("Apple2IOU", "The keyboard has no Apple II latch")) + "} " + kbd);
    }
    const std::string video = cd->get_parameter("video", false).value;
    if (!video.empty())
        m_video = dynamic_cast<Apple2VideoSource*>(im->dm->get_device_by_name(video));

    Apple2IOU::reset(true);
    return emulator::Result::ok();
}

void Apple2IOU::reset(bool cold)
{
    // The reset clears the 80 column and character set switches of the //e,
    // and on the Правец-8А the 8-bit code table: MODE0, as at power on (5.9)
    // PAGE2 and HIRES too: the MMU drops its copies of them
    m_flags &= ~(unsigned)(VF_80COL | VF_ALTCHAR | VF_80STORE | VF_PAGE2 | VF_HIRES);
    if (cold) {
        m_flags = 0;
        m_an = 0;
        m_pdl_started = false;
    }
    m_softsw = false;
    i_softsw.change(0);
    i_an.change(m_an);
}

void Apple2IOU::clock(unsigned int counter)
{
    m_cycles += counter;
}

void Apple2IOU::set_flag(unsigned f, bool on)
{
    if (on) m_flags |= f; else m_flags &= ~f;
}

// $C011-$C01F: one switch in bit 7, the keyboard code below it
unsigned Apple2IOU::status(unsigned address)
{
    bool b = false;
    switch (address & 0x0F) {
        case 0x1: b = m_mmu->get_switch(Apple2MMU::SW_LCBANK2); break;
        case 0x2: b = m_mmu->get_switch(Apple2MMU::SW_LCRAM); break;
        case 0x3: b = m_mmu->get_switch(Apple2MMU::SW_RAMRD); break;
        case 0x4: b = m_mmu->get_switch(Apple2MMU::SW_RAMWRT); break;
        case 0x5: b = m_mmu->get_switch(Apple2MMU::SW_INTCXROM); break;
        case 0x6: b = m_mmu->get_switch(Apple2MMU::SW_ALTZP); break;
        case 0x7: b = m_mmu->get_switch(Apple2MMU::SW_SLOTC3ROM); break;
        case 0x8: b = m_mmu->get_switch(Apple2MMU::SW_80STORE); break;
        // RDVBLBAR: low while the beam is in the vertical blanking
        case 0x9: b = (m_video == nullptr) || !m_video->in_vbl(); break;
        case 0xA: b = (m_flags & VF_TEXT) != 0; break;
        case 0xB: b = (m_flags & VF_MIXED) != 0; break;
        case 0xC: b = (m_flags & VF_PAGE2) != 0; break;
        case 0xD: b = (m_flags & VF_HIRES) != 0; break;
        case 0xE: b = (m_flags & VF_ALTCHAR) != 0; break;
        case 0xF: b = (m_flags & VF_80COL) != 0; break;
    }
    const unsigned low = (m_kbd != nullptr) ? (m_kbd->kbd_latch() & 0x7F) : 0;
    return (b ? 0x80 : 0) | low;
}

// Everything but the reads that return a value: the switches change on a
// read as well as on a write, except the //e memory ones ($C000-$C00F are
// written)
void Apple2IOU::access(unsigned address, bool read, unsigned value)
{
    const unsigned a = address & 0xFF;
    if (a < 0x10) {
        if (!m_iie) return;
        if (read && !(m_altchar_on_read && a >= 0x0E)) return;
        const bool on = (a & 1) != 0;
        switch (a >> 1) {
            case 0: set_flag(VF_80STORE, on); m_mmu->set_switch(Apple2MMU::SW_80STORE, on); break;
            case 1: m_mmu->set_switch(Apple2MMU::SW_RAMRD, on); break;
            case 2: m_mmu->set_switch(Apple2MMU::SW_RAMWRT, on); break;
            case 3: m_mmu->set_switch(Apple2MMU::SW_INTCXROM, on); break;
            case 4: m_mmu->set_switch(Apple2MMU::SW_ALTZP, on); break;
            case 5: m_mmu->set_switch(Apple2MMU::SW_SLOTC3ROM, on); break;
            case 6: set_flag(VF_80COL, on); break;
            case 7: set_flag(VF_ALTCHAR, on); break;
        }
        return;
    }
    if (a < 0x20) {
        // Any access to $C010 drops the strobe; on the //e a write to any of
        // $C010-$C01F does, the reads of $C011-$C01F only look
        if (m_kbd != nullptr && (a == 0x10 || !read || !m_iie)) m_kbd->kbd_clear_strobe();
        return;
    }
    if (a < 0x30) {
        i_tape_out.change(i_tape_out.value ^ 1);
        return;
    }
    if (a >= 0x50 && a < 0x60) {
        const bool on = (a & 1) != 0;
        switch ((a >> 1) & 7) {
            case 0: set_flag(VF_TEXT, on); break;
            case 1: set_flag(VF_MIXED, on); break;
            case 2: set_flag(VF_PAGE2, on); m_mmu->set_switch(Apple2MMU::SW_PAGE2, on); break;
            case 3: set_flag(VF_HIRES, on); m_mmu->set_switch(Apple2MMU::SW_HIRES, on); break;
            default: {
                const unsigned n = ((a >> 1) & 7) - 4;
                m_an = on ? (m_an | (1u << n)) : (m_an & ~(1u << n));
                set_flag(VF_AN3, (m_an & 8) != 0);
                i_an.change(m_an);
                break;
            }
        }
        return;
    }
    if (a >= 0x60 && a < 0x70) {
        // Правец: a write anywhere at $C06x sets the code table flip-flop from
        // D0 (SETMOD does STX $C060)
        if (!read && m_pravetz) {
            m_softsw = (value & 1) != 0;
            i_softsw.change(m_softsw ? 1 : 0);
        }
        return;
    }
    if (a >= 0x70 && a < 0x80) {
        m_pdl_start = m_cycles;
        m_pdl_started = true;
        // The MEGARAM of the Правец-8А (like the RamWorks) takes the number
        // of its 64K page from a write to $C073: *PDL.RESET and the data bus
        // are on the X0 connector. The ramdisk of СП «Вариант» on the
        // school disks sizes it so, writing $0F..$00 there
        if (!read && (a & 0x09) == 0x01) m_mmu->set_aux_bank(value & 0xFF);
        return;
    }
}

unsigned Apple2IOU::get_value(unsigned address)
{
    const unsigned a = address & 0xFF;
    if (a < 0x10) {
        access(address, true, 0);
        return (m_kbd != nullptr) ? m_kbd->kbd_latch() : 0;
    }
    if (a == 0x10) {
        unsigned v = 0;
        if (m_kbd != nullptr) {
            v = m_iie ? (((m_kbd->kbd_any_down()) ? 0x80 : 0) | (m_kbd->kbd_latch() & 0x7F)) : m_kbd->kbd_latch();
            m_kbd->kbd_clear_strobe();
        }
        return v;
    }
    if (a < 0x20) {
        if (!m_iie) {
            access(address, true, 0);
            return (m_kbd != nullptr) ? m_kbd->kbd_latch() : 0;
        }
        return status(address);
    }
    if (a >= 0x60 && a < 0x70) {
        // The Правец reads the code table flip-flop on D6 of any of them
        const unsigned d6 = (m_pravetz && m_softsw) ? 0x40 : 0;
        const unsigned n = a & 7;
        bool b = false;
        if (n == 0) {
            if (m_pravetz) b = (m_kbd != nullptr) && m_kbd->kbd_bit7();
            else b = (i_tape_in.value & 1) != 0;
        } else if (n < 4) {
            b = (((i_pb.value & 7) | m_pb_fixed) >> (n - 1)) & 1;
        } else {
            // The 558 holds its output up for 11 clocks per step of the
            // resistance after $C070; 255 is an open input
            const unsigned v = m_pdl[n - 4];
            b = m_pdl_started && (v >= 255 || m_cycles - m_pdl_start < (uint64_t)v * 11 + 8);
        }
        return (b ? 0x80 : 0) | d6;
    }
    access(address, true, 0);
    return 0;
}

unsigned Apple2IOU::get_direct(unsigned address)
{
    const unsigned a = address & 0xFF;
    if (a < 0x10 || (a < 0x20 && !m_iie)) return (m_kbd != nullptr) ? m_kbd->kbd_latch() : 0;
    if (a < 0x20) {
        if (a == 0x10) return (m_kbd != nullptr) ? m_kbd->kbd_latch() : 0;
        return status(address);
    }
    return 0;
}

void Apple2IOU::set_value(unsigned address, unsigned value, bool force)
{
    (void)force;
    access(address, false, value);
}

std::vector<DeviceFieldInfo> Apple2IOU::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"video",  "Video switches: text, mixed, page2, hires, 80col, altchar, an3", false});
    r.push_back({"an",     "Annunciators AN0-AN3",                       false});
    r.push_back({"softsw", "Правец: 1 in the 8-bit code table (MODE1)",  false});
    r.push_back({"pdl",    "Paddle values 0-255 (255: nothing plugged)",  false});
    return r;
}

bool Apple2IOU::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "video") {
        static const char * names[] = {"text", "mixed", "page2", "hires", "80col", "altchar", "an3", "80store"};
        out.numeric = false;
        out.text.clear();
        for (unsigned i = 0; i < 8; i++)
            if (m_flags & (1u << i)) out.text += (out.text.empty() ? "" : " ") + std::string(names[i]);
        return true;
    }
    if (field == "an" || field == "softsw") {
        out.numeric = true;
        out.values.push_back(field == "an" ? m_an : (m_softsw ? 1 : 0));
        return true;
    }
    if (field == "pdl") {
        out.numeric = true;
        for (unsigned n = 0; n < 4; n++) out.values.push_back(m_pdl[n]);
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> Apple2IOU::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"pdl", "n,value", "Sets paddle n (0-3) to 0-255"});
    return r;
}

emulator::Result Apple2IOU::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "pdl") {
        std::vector<std::string> p = split_params(parameters);
        if (p.size() != 2)
            return emulator::Result::error(emulator::ErrorCode::BadParameters, "pdl n,value");
        const unsigned n = parse_numeric_value(str_trim(p[0]));
        if (n > 3) return emulator::Result::error(emulator::ErrorCode::BadParameters, "pdl n,value");
        m_pdl[n] = parse_numeric_value(str_trim(p[1])) & 0xFF;
        return emulator::Result::ok();
    }
    return AddressableDevice::send_command(command, parameters);
}

void Apple2IOU::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.n("flags", m_flags);
    w.n("an", m_an);
    w.b("softsw", m_softsw);
    for (unsigned n = 0; n < 4; n++) w.n((std::string("pdl") + std::to_string(n)).c_str(), m_pdl[n]);
    w.b("pdl_started", m_pdl_started);
    //Only how long ago the timers started matters
    w.u("pdl_elapsed", (unsigned)std::min<uint64_t>(m_cycles - m_pdl_start, 0xFFFFFFFFu), 32);
}

emulator::Result Apple2IOU::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("flags", m_flags);
    r.u("an", m_an);
    r.b("softsw", m_softsw);
    for (unsigned n = 0; n < 4; n++) r.u((std::string("pdl") + std::to_string(n)).c_str(), m_pdl[n]);
    r.b("pdl_started", m_pdl_started);
    unsigned elapsed = 0;
    r.u("pdl_elapsed", elapsed);
    m_cycles = 0x100000000ull;
    m_pdl_start = m_cycles - elapsed;
    return emulator::Result::ok();
}

ComputerDevice * create_apple2_iou(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Apple2IOU(im, cd);
}
