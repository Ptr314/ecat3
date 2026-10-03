// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК monochrome graphics controller КГД

#include "dvk_kgd.h"
#include "emulator/utils.h"

#define KGD_CR_MASK     0140000
#define KGD_AR_MASK     0037777
#define KGD_FRAME_LINES 312

DVKKGD::DVKKGD(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , m_vram(KGD_VRAM_SIZE, 0)
{
    m_clocked = true;   // clock() ведёт счётчик развёртки
    can_read = true;
    can_write = true;
    addresable_size = 8;
}

emulator::Result DVKKGD::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    // Строка растра 64 мкс
    m_line_ticks = (m_system_clock != 0) ? m_system_clock / 15625 : 512;
    if (m_line_ticks == 0) m_line_ticks = 1;
    return emulator::Result::ok();
}

void DVKKGD::reset(MAYBE_UNUSED bool cold)
{
    // Сброс гасит графику и включает текст; видеопамять остаётся
    m_cr = 0;
    m_ar = 0;
}

void DVKKGD::clock(unsigned int counter)
{
    m_ticks += counter;
}

unsigned DVKKGD::get_direct(unsigned address)
{
    unsigned int v = 0;
    switch ((address >> 1) & 3) {
    case 0: v = m_cr; break;
    case 1: v = m_vram[m_ar]; break;
    case 2: v = m_ar; break;
    case 3: {
        const uint64_t line = m_ticks / m_line_ticks;
        const unsigned int col = (unsigned int)((m_ticks % m_line_ticks) * 1024 / m_line_ticks);
        v = ((unsigned int)(line % KGD_FRAME_LINES) & 037) << 11 | (col & 03777);
        break;
    }
    }
    return (address & 1) ? (v >> 8) & 0xFF : v;
}

unsigned int DVKKGD::get_value_word(unsigned int address)
{
    return get_direct(address & ~1u);
}

unsigned int DVKKGD::get_value(unsigned int address)
{
    return get_direct(address) & 0xFF;
}

void DVKKGD::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    switch ((address >> 1) & 3) {
    case 0: m_cr = value & KGD_CR_MASK; break;
    case 1: m_vram[m_ar] = (uint8_t)(value & 0xFF); break;
    case 2: m_ar = value & KGD_AR_MASK; break;
    default: break;
    }
}

// Байтовая запись меняет свою половину регистра, как у реплики: старший байт
// управления, младший байт данных, любая половина адреса
void DVKKGD::set_value(unsigned int address, unsigned int value, bool force)
{
    const unsigned int reg = (address >> 1) & 3;
    const bool high = (address & 1) != 0;
    value &= 0xFF;
    switch (reg) {
    case 0:
        if (high) set_value_word(0, value << 8, force);
        break;
    case 1:
        if (!high) set_value_word(2, value, force);
        break;
    case 2:
        set_value_word(4, high ? ((m_ar & 0xFF) | (value << 8)) : ((m_ar & 0xFF00) | value), force);
        break;
    default:
        break;
    }
}

std::vector<DeviceFieldInfo> DVKKGD::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"cr",   "Регистр управления (176640)",       false});
    r.push_back({"ar",   "Регистр адреса (176644)",           false});
    r.push_back({"vram", "Видеопамять, байты (from,to)",       false});
    r.push_back({"lit",  "Сколько точек картинки горит",       false});
    return r;
}

bool DVKKGD::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "cr") { out.values.push_back(m_cr); return true; }
    if (field == "ar") { out.values.push_back(m_ar); return true; }
    if (field == "vram") {
        out.width = 8;
        for (unsigned int i = from; i <= to && i < m_vram.size(); i++) out.values.push_back(m_vram[i]);
        return true;
    }
    if (field == "lit") {
        unsigned int n = 0;
        for (unsigned int i = 0; i < KGD_HEIGHT * KGD_LINE_BYTES; i++)
            for (unsigned int b = 0; b < 8; b++) n += (m_vram[i] >> b) & 1;
        out.values.push_back(n);
        return true;
    }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void DVKKGD::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("cr", m_cr);
    w.u("ar", m_ar);
    w.n64("ticks", m_ticks);
    w.hex("vram", m_vram.data(), m_vram.size());
}

emulator::Result DVKKGD::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("cr", m_cr);
    r.u("ar", m_ar);
    r.n64("ticks", m_ticks);
    r.hex("vram", m_vram.data(), m_vram.size());
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_kgd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKKGD(im, cd);
}
