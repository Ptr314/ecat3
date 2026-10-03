// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК power panel: power-up sequence of АИП and АСП

#include "dvk_panel.h"
#include "emulator/utils.h"

DVKPanel::DVKPanel(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , i_aip(this, im, 1, "aip", MODE_W)
    , i_asp(this, im, 1, "asp", MODE_W)
    , i_ost(this, im, 1, "ost", MODE_W)
    , i_prt(this, im, 1, "prt", MODE_W)
{
    m_clocked = true;
}

emulator::Result DVKPanel::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    // Времена - в миллисекундах; в конфигурации ДВК числа восьмеричные, так
    // что пишутся с «_»
    const uint64_t clock = (m_system_clock != 0) ? m_system_clock : 1000000;
    const unsigned int pg_ms = read_confg_value(cd, "power_good", false, (unsigned int)500);
    const unsigned int aip_ms = read_confg_value(cd, "aip_delay", false, (unsigned int)13);
    const unsigned int asp_ms = read_confg_value(cd, "asp_delay", false, (unsigned int)10);
    const unsigned int reset_ms = read_confg_value(cd, "reset_time", false, (unsigned int)50);
    m_pg_ticks = (unsigned int)(clock * pg_ms / 1000);
    m_aip_ticks = (unsigned int)(clock * aip_ms / 1000);
    m_asp_ticks = (unsigned int)(clock * asp_ms / 1000);
    m_reset_ticks = (unsigned int)(clock * reset_ms / 1000);
    const unsigned int hz = read_confg_value(cd, "prt_frequency", false, (unsigned int)50);
    m_prt_half = (hz != 0) ? (unsigned int)(clock / (2 * hz)) : 1;
    if (m_prt_half == 0) m_prt_half = 1;

    // Положение тумблеров при включении; потом их переключают на панели
    m_halt = read_confg_value(cd, "halt", false, false);
    m_prt = read_confg_value(cd, "prt", false, false);
    i_ost.change(m_halt ? 0 : 1);
    i_prt.change(0);
    m_prt_ticks = 0;

    // До пуска машина выключена: обе аварии стоят
    i_aip.change(0);
    i_asp.change(0);
    m_phase = PH_WAIT_PG;
    m_ticks = 0;
    return emulator::Result::ok();
}

void DVKPanel::set_phase(unsigned int phase)
{
    m_phase = phase;
    m_ticks = 0;
    update_prt();
}

// ПРТ идёт, пока машина включена (и при нажатом «Сбросе») и включён тумблер
void DVKPanel::update_prt()
{
    const bool run = m_prt && (m_phase == PH_ON || m_phase == PH_RESET);
    if (!run && i_prt.value != 0) i_prt.change(0);
    if (!run) m_prt_ticks = 0;
}

// Холодный пуск - это включение питания: последовательность заново. Тёплый
// сброс машины пульта не касается
void DVKPanel::reset(bool cold)
{
    if (!cold) return;
    i_aip.change(0);
    i_asp.change(0);
    set_phase(PH_WAIT_PG);
}

void DVKPanel::clock(unsigned int counter)
{
    if (m_prt && (m_phase == PH_ON || m_phase == PH_RESET)) {
        m_prt_ticks += counter;
        while (m_prt_ticks >= m_prt_half) {
            m_prt_ticks -= m_prt_half;
            i_prt.change((i_prt.value & 1) ^ 1);
        }
    }
    if (m_phase == PH_ON) return;
    m_ticks += counter;
    switch (m_phase) {
    case PH_WAIT_PG:
        if (m_ticks >= m_pg_ticks) set_phase(PH_WAIT_AIP);
        break;
    case PH_WAIT_AIP:
        if (m_ticks >= m_aip_ticks) {
            i_aip.change(1);
            set_phase(PH_WAIT_ASP);
        }
        break;
    case PH_WAIT_ASP:
        if (m_ticks >= m_asp_ticks) {
            i_asp.change(1);
            set_phase(PH_ON);
        }
        break;
    case PH_RESET:
        // Кнопка «Сброс» отпущена: АИП снова в норме, АСП так и не падала
        if (m_ticks >= m_reset_ticks) {
            i_aip.change(1);
            set_phase(PH_ON);
        }
        break;
    default:
        break;
    }
}

#define OPTION_HALT     0
#define OPTION_PRT      1

DeviceOptions DVKPanel::get_device_options()
{
    return {
        {
            OPTION_HALT, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Panel switch \"Halt\""), "",
            {
                {0, QT_TRANSLATE_NOOP("DeviceOptions", "Run")},
                {1, QT_TRANSLATE_NOOP("DeviceOptions", "Halt")}
            },
            static_cast<unsigned>(m_halt ? 1 : 0)
        },
        {
            OPTION_PRT, DEVICE_OPTION_DROPDOWN, QT_TRANSLATE_NOOP("DeviceOptions", "Line clock 50 Hz"), "",
            {
                {0, QT_TRANSLATE_NOOP("DeviceOptions", "Interrupts off")},
                {1, QT_TRANSLATE_NOOP("DeviceOptions", "Interrupts on")}
            },
            static_cast<unsigned>(m_prt ? 1 : 0)
        }
    };
}

// На потоке эмуляции (окно зовёт через invoke): линии можно вести сразу
void DVKPanel::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id == OPTION_HALT) {
        m_halt = (value_id != 0);
        i_ost.change(m_halt ? 0 : 1);
    } else if (option_id == OPTION_PRT) {
        m_prt = (value_id != 0);
        update_prt();
    }
}

std::vector<DeviceFieldInfo> DVKPanel::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"phase", "0 - ждёт POWER GOOD, 1 - снятия АИП, 2 - снятия АСП, 3 - работа, 4 - нажат Сброс", false});
    r.push_back({"aip",   "АИП: 1 - нет аварии", false});
    r.push_back({"asp",   "АСП: 1 - нет аварии", false});
    r.push_back({"halt",  "Тумблер «Пульт»", false});
    r.push_back({"prt",   "Тумблер «Прерывания»", false});
    return r;
}

bool DVKPanel::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "phase") { out.values.push_back(m_phase); return true; }
    if (field == "aip")   { out.values.push_back(i_aip.value & 1); return true; }
    if (field == "asp")   { out.values.push_back(i_asp.value & 1); return true; }
    if (field == "halt")  { out.values.push_back(m_halt ? 1 : 0); return true; }
    if (field == "prt")   { out.values.push_back(m_prt ? 1 : 0); return true; }
    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> DVKPanel::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"reset", "", "Кнопка «Сброс»: АИП на время reset_time"});
    return r;
}

emulator::Result DVKPanel::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "reset") {
        if (m_phase == PH_ON || m_phase == PH_RESET) {
            i_aip.change(0);
            set_phase(PH_RESET);
        }
        return emulator::Result::ok();
    }
    return ComputerDevice::send_command(command, parameters);
}

void DVKPanel::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.u("phase", m_phase);
    w.u("ticks", m_ticks);
    w.b("halt", m_halt);
    w.b("prt", m_prt);
    w.u("prt_ticks", m_prt_ticks);
}

emulator::Result DVKPanel::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    // Снимок без пульта или старый - машина уже работает
    m_phase = PH_ON;
    m_ticks = 0;
    r.u("phase", m_phase);
    r.u("ticks", m_ticks);
    r.b("halt", m_halt);
    r.b("prt", m_prt);
    r.u("prt_ticks", m_prt_ticks);
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_panel(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKPanel(im, cd);
}
