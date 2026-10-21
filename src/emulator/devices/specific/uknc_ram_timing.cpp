// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ memory timing, one processor's side

#include "uknc_ram_timing.h"
#include "emulator/utils.h"

// Время считается в пикосекундах: такт ЦП 125 нс, ПП 160, сетка 640 - в
// наносекундах всё целое, но частота процессора в конфигурации может быть
// любой, а сетка с уходом частоты (drift) - дробной
#define PS 1000ull

UKNCRamTiming::UKNCRamTiming(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    m_clocked = true;   // clock() переопределён
}

emulator::Result UKNCRamTiming::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;
    m_grid = read_confg_value(cd, "grid", false, (unsigned int)640);
    m_phase = read_confg_value(cd, "phase", false, (unsigned int)0);
    m_reply = read_confg_value(cd, "reply", false, (unsigned int)320);
    m_reply_write = read_confg_value(cd, "reply_write", false, m_reply);
    m_reply_write_byte = read_confg_value(cd, "reply_write_byte", false, m_reply_write);
    m_read = read_confg_value(cd, "read", true, (unsigned int)0);
    m_write = read_confg_value(cd, "write", true, (unsigned int)0);
    // Байт пишется так же, как слово, если не сказано иное; в RMW - как
    // запись RMW слова, если у байта нет своего времени
    const bool byte_given = !cd->get_parameter("write_byte", false).value.empty();
    m_write_byte = read_confg_value(cd, "write_byte", false, m_write);
    m_rmw_write = read_confg_value(cd, "rmw_write", false, m_write);
    m_rmw_write_byte = read_confg_value(cd, "rmw_write_byte", false, byte_given ? m_write_byte : m_rmw_write);
    m_late = read_confg_value(cd, "late", false, (unsigned int)0);
    m_drift = read_confg_value(cd, "drift", false, (unsigned int)0);
    m_setup = read_confg_value(cd, "setup", false, (unsigned int)0);
    if (m_grid == 0) m_grid = 1;
    m_phase %= m_grid;
    m_period = m_system_clock ? (unsigned int)((1000000000000ull + m_system_clock / 2) / m_system_clock) : 0;
    const uint64_t g = (uint64_t)m_grid * PS * (1000000 + m_drift) / 1000000;
    m_grid_ps = (g == 0) ? 1 : (g > 0x7FFFFFFFull ? 0x7FFFFFFFu : (uint32_t)g);
    m_last = grid_floor(m_busy);
    return emulator::Result::ok();
}

// Точка сетки не позже t (или первая, если t раньше фазы)
uint64_t UKNCRamTiming::grid_floor(uint64_t t) const
{
    const uint64_t phase = (uint64_t)m_phase * PS;
    if (t <= phase) return phase;
    return (t - phase) / m_grid_ps * m_grid_ps + phase;
}

void UKNCRamTiming::reset(bool cold)
{
    ComputerDevice::reset(cold);
    if (cold) {
        m_pos = 0;
        m_busy = 0;
        m_last = grid_floor(0);
    }
}

void UKNCRamTiming::clock(unsigned int counter)
{
    m_pos += counter;
}

// offset - строб (DIN или DOUT) в тактах процессора от точки, до которой
// устройство дотактовано; ответ - такты от строба до RPLY. RPLY приходит
// чуть позже точки сетки, поэтому процессор видит его на следующем своём
// такте и тогда, когда точка ровно на такте.
//
// Вызывается на каждый цикл шины обоих процессоров, поэтому делит в 32
// разрядах: до первой точки сетки считается от m_last, точки, на которой
// начался прошлый цикл, а это от силы несколько микросекунд. 64-разрядное
// деление (в 32-разрядной сборке - вызов __udivdi3) остаётся для долгой
// паузы без обращений к памяти
unsigned int UKNCRamTiming::answer(unsigned int flags, unsigned int offset)
{
    if (m_period == 0) return 0;
    const uint64_t strobe = (m_pos + offset) * m_period;
    // Строб берётся в точке сетки, которая не меньше чем на setup позже него
    const uint64_t asked = strobe + (uint64_t)m_setup * PS;
    const uint64_t from = (asked > m_busy) ? asked : m_busy;
    // первая точка сетки не раньше from; m_last не позже m_busy, а значит и from
    uint64_t start;
    const uint64_t delta = from - m_last;
    if (delta < 0x80000000ull) {
        const uint32_t d = (uint32_t)delta;
        start = m_last + (uint64_t)((d + m_grid_ps - 1) / m_grid_ps) * m_grid_ps;
    } else {
        start = grid_floor(from);
        if (start < from) start += m_grid_ps;
    }
    m_last = start;
    unsigned int keep = m_read;
    if (flags & WAIT_WRITE) {
        const bool byte = (flags & WAIT_BYTE) != 0;
        if (flags & WAIT_RMW) keep = byte ? m_rmw_write_byte : m_rmw_write;
        else keep = byte ? m_write_byte : m_write;
    }
    m_busy = start + (uint64_t)keep * PS;
    m_cycles++;
    const unsigned int reply = !(flags & WAIT_WRITE) ? m_reply : (flags & WAIT_BYTE) ? m_reply_write_byte : m_reply_write;
    const uint64_t rply = start + (uint64_t)reply * PS;
    const uint64_t wait = rply - strobe;
    return (unsigned int)(wait < 0x100000000ull ? (uint32_t)wait / m_period : wait / m_period) + m_late;
}

unsigned int UKNCRamTiming::wait_states(MAYBE_UNUSED unsigned int address, bool write, unsigned int offset)
{
    return answer(write ? WAIT_WRITE : 0, offset);
}

unsigned int UKNCRamTiming::wait_cycle(MAYBE_UNUSED unsigned int address, unsigned int flags, unsigned int offset)
{
    return answer(flags, offset);
}

//--------------------------- Поля и состояние ------------------------------//

std::vector<DeviceFieldInfo> UKNCRamTiming::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"cycles", "Сколько циклов памяти рассчитано с пуска", false});
    return r;
}

bool UKNCRamTiming::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 32;
    if (field == "cycles") { out.values.push_back((unsigned int)m_cycles); return true; }
    out.numeric = false;
    out.width = 0;
    return ComputerDevice::get_field(field, from, to, out);
}

void UKNCRamTiming::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.n64("pos", m_pos);
    w.n64("busy", m_busy);
    w.n64("cycles", m_cycles);
}

emulator::Result UKNCRamTiming::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    r.n64("pos", m_pos);
    r.n64("busy", m_busy);
    r.n64("cycles", m_cycles);
    m_last = grid_floor(m_busy);
    return emulator::Result::ok();
}

ComputerDevice * create_uknc_ram_timing(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCRamTiming(im, cd);
}
