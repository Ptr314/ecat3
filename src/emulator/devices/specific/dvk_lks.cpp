// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Line clock with the LKS register (177546), МС1201.04

#include "dvk_lks.h"
#include "emulator/utils.h"

#define LKS_IE      0000100
#define LKS_MON     0000200

#define CALLBACK_INIT   1
#define CALLBACK_PRT    2

DVKLKS::DVKLKS(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irq(this, im, 1, "irq", MODE_W)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_prt(this, im, 1, "prt", MODE_R, CALLBACK_PRT)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 2;
}

emulator::Result DVKLKS::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    const unsigned int hz = read_confg_value(cd, "frequency", false, (unsigned int)50);
    m_internal = (hz != 0);
    m_period = (hz != 0 && m_system_clock != 0) ? m_system_clock / hz : 1;
    if (m_period == 0) m_period = 1;
    i_irq.change(0);
    return emulator::Result::ok();
}

void DVKLKS::reset(MAYBE_UNUSED bool cold)
{
    m_ie = false;
    m_monitor = true;
}

void DVKLKS::clock(unsigned int counter)
{
    if (m_pulse) {
        m_pulse = false;
        i_irq.change(0);
    }
    if (!m_internal) return;
    m_ticks += counter;
    if (m_ticks < m_period) return;
    m_ticks -= m_period;
    tick();
}

// Импульс таймера: со своего генератора или фронт ПРТ
void DVKLKS::tick()
{
    m_count++;
    m_monitor = true;
    if (m_ie) {
        m_pulse = true;
        i_irq.change(1);
    }
}

void DVKLKS::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT && (new_value & 1)) reset(false);
    if (callback_id == CALLBACK_PRT && !m_internal && (new_value & 1) && !(old_value & 1)) tick();
}

unsigned DVKLKS::get_direct(unsigned address)
{
    const unsigned int v = (m_ie ? LKS_IE : 0) | (m_monitor ? LKS_MON : 0);
    return (address & 1) ? 0 : v;
}

unsigned int DVKLKS::get_value(unsigned int address)
{
    return get_direct(address);
}

unsigned int DVKLKS::get_value_word(MAYBE_UNUSED unsigned int address)
{
    return get_direct(0);
}

void DVKLKS::set_value_word(MAYBE_UNUSED unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    m_ie = (value & LKS_IE) != 0;
    if ((value & LKS_MON) == 0) m_monitor = false;
}

void DVKLKS::set_value(unsigned int address, unsigned int value, bool force)
{
    if ((address & 1) == 0) set_value_word(0, value & 0xFF, force);
}

std::vector<DeviceFieldInfo> DVKLKS::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"lks",   "Регистр LKS", false});
    r.push_back({"ticks", "Тиков с пуска", false});
    return r;
}

bool DVKLKS::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "lks")   { out.values.push_back(get_direct(0)); return true; }
    if (field == "ticks") { out.values.push_back(m_count);       return true; }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void DVKLKS::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.b("ie", m_ie);
    w.b("monitor", m_monitor);
    w.u("ticks", m_ticks);
    w.b("pulse", m_pulse);
}

emulator::Result DVKLKS::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.b("ie", m_ie);
    r.b("monitor", m_monitor);
    r.u("ticks", m_ticks);
    r.b("pulse", m_pulse);
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_lks(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKLKS(im, cd);
}
