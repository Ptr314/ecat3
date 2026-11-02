// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НГМД Электроники МС 0585: обвязка 8751 «Кочан» и КР1818ВГ93

#include <cstring>

#include "e85_kngmd.h"
#include "emulator/devices/common/wd1793.h"
#include "emulator/utils.h"

#define CALLBACK_P1     1
#define CALLBACK_P2     2
#define CALLBACK_LINES  3
#define CALLBACK_INIT   4

#define MCU_BASE        01000   // база портов 8051 в карте устройства

#define HOST_BUFFER     020
#define HOST_POINTER    022
#define HOST_START      024
#define HOST_BUFFER_W   026

#define MCU_BUFFER      0x10
#define MCU_POINTER     0x12
#define MCU_RELEASE     0x14
#define MCU_BUFFER_W    0x16
#define MCU_FDC         0x18

#define P1_SIDE         0x01
#define P1_READ_BANK    0x08
#define P1_INTB         0x10
#define P1_INTA         0x20

#define P2_MOTOR        0x01
#define P2_DESELECT     0x02
#define P2_INTRQ        0x10
#define P2_DRQ          0x20
#define P2_BUSY         0x40
#define P2_READY        0x80

// Готовность накопителя опрашивается раз во столько тактов: диск вставляют редко
#define READY_PERIOD    1024

E85Kngmd::E85Kngmd(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_p1(this, im, 8, "p1", MODE_R, CALLBACK_P1)
    , i_p2(this, im, 8, "p2", MODE_R, CALLBACK_P2)
    , i_pins(this, im, 8, "pins", MODE_W)
    , i_drq(this, im, 1, "drq", MODE_R, CALLBACK_LINES)
    , i_intrq(this, im, 1, "intrq", MODE_R, CALLBACK_LINES)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_reset(this, im, 1, "reset", MODE_W)
    , i_select(this, im, 3, "select", MODE_W)
    , i_side(this, im, 1, "side", MODE_W)
    , i_motor(this, im, 1, "motor", MODE_W)
    , i_irqa(this, im, 1, "irqa", MODE_W)
    , i_irqb(this, im, 1, "irqb", MODE_W)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 02000;
    memset(m_buffer, 0, sizeof(m_buffer));
}

emulator::Result E85Kngmd::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    const std::string fdc = cd->get_parameter("fdc", false).value;
    m_fdc = dynamic_cast<WD1793*>(im->dm->get_device_by_name(fdc, false));
    if (m_fdc == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{E85Kngmd|" + std::string(QT_TRANSLATE_NOOP("E85Kngmd", "The controller chip is expected in 'fdc'")) + "} " + name);
    return emulator::Result::ok();
}

void E85Kngmd::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        memset(m_write, 0, sizeof(m_write));
        memset(m_read, 0, sizeof(m_read));
        memset(m_buffer, 0, sizeof(m_buffer));
    }
    i_irqa.change(0);
    i_irqb.change(0);
    board_init();
    update_outputs();
}

// Начальная установка (ТО4, 4.8.4): адрес буфера в ноль, сигнал на БИСК и МП, магистраль
// у контроллера. ЦП не видит плату, пока прошивка не кончит свой тест и не отдаст её сама
void E85Kngmd::board_init()
{
    m_pointer = 0;
    m_busy = true;
    i_reset.change(1);
    i_reset.change(0);
    update_pins();
}

void E85Kngmd::update_pins()
{
    unsigned int v = 0x0F;
    if (i_intrq.value & 1) v |= P2_INTRQ;
    if (i_drq.value & 1) v |= P2_DRQ;
    if (m_busy) v |= P2_BUSY;
    if (m_fdc != nullptr && m_fdc->selected_ready()) v |= P2_READY;
    if (i_pins.value != v) i_pins.change(v);
}

void E85Kngmd::update_outputs()
{
    const unsigned int p1 = i_p1.value & 0xFF;
    const unsigned int p2 = i_p2.value & 0xFF;
    const unsigned int select = (p2 & P2_DESELECT) ? 7 : ((~p1 >> 1) & 3);
    if (i_select.value != select) i_select.change(select);
    const unsigned int side = (p1 & P1_SIDE) ? 0 : 1;
    if (i_side.value != side) i_side.change(side);
    const unsigned int motor = (p2 & P2_MOTOR) ? 1 : 0;
    if (i_motor.value != motor) i_motor.change(motor);
    const unsigned int a = (p1 & P1_INTA) ? 0 : 1;
    if (i_irqa.value != a) i_irqa.change(a);
    const unsigned int b = (p1 & P1_INTB) ? 0 : 1;
    if (i_irqb.value != b) i_irqb.change(b);
}

void E85Kngmd::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_P1:
    case CALLBACK_P2:
        update_outputs();
        update_pins();
        break;
    case CALLBACK_LINES:
        update_pins();
        break;
    case CALLBACK_INIT:
        if (new_value & 1) board_init();
        break;
    default:
        break;
    }
}

void E85Kngmd::clock(unsigned int counter)
{
    m_ready_wait += counter;
    if (m_ready_wait >= READY_PERIOD) {
        m_ready_wait = 0;
        update_pins();
    }
}

void E85Kngmd::start()
{
    m_busy = true;
    m_commands++;
    update_pins();
}

unsigned int E85Kngmd::host_read(unsigned int address, bool peek)
{
    // Пока магистраль у контроллера, ЦП его ячеек не видит. Загрузчик ПЗУ ждёт
    // так разряда готовности РСК (155312): иначе он брал буфер, ещё не заполненный
    if (m_busy) return 0;
    if (address < HOST_BUFFER) return m_read[(address >> 1) & 7];
    switch (address) {
    case HOST_BUFFER: {
        const unsigned int v = m_buffer[m_pointer];
        if (!peek) m_pointer = (m_pointer + 1) & 0777;
        return v;
    }
    case HOST_POINTER:
        if (!peek) m_pointer = 0;
        return 0;
    case HOST_START:
        if (!peek) start();
        return 0;
    default:
        return 0;
    }
}

void E85Kngmd::host_write(unsigned int address, unsigned int value)
{
    if (m_busy) return;
    if (address < HOST_BUFFER) {
        m_write[(address >> 1) & 7] = (uint8_t)value;
        return;
    }
    switch (address) {
    case HOST_BUFFER:
    case HOST_BUFFER_W:
        m_buffer[m_pointer] = (uint8_t)value;
        m_pointer = (m_pointer + 1) & 0777;
        break;
    case HOST_POINTER:
        m_pointer = 0;
        break;
    case HOST_START:
        start();
        break;
    default:
        break;
    }
}

unsigned int E85Kngmd::mcu_read(unsigned int address, bool peek)
{
    if (address < MCU_BUFFER) {
        const uint8_t * bank = (i_p1.value & P1_READ_BANK) ? m_read : m_write;
        return bank[(address >> 1) & 7];
    }
    if (address >= MCU_FDC) {
        const unsigned int reg = (address >> 1) & 3;
        return (peek ? m_fdc->get_direct(reg) : m_fdc->get_value(reg)) & 0xFF;
    }
    switch (address) {
    case MCU_BUFFER: {
        const unsigned int v = m_buffer[m_pointer];
        if (!peek) m_pointer = (m_pointer + 1) & 0777;
        return v;
    }
    case MCU_POINTER:
        if (!peek) m_pointer = 0;
        return 0;
    case MCU_RELEASE:
        if (!peek) {
            m_busy = false;
            update_pins();
        }
        return 0;
    default:
        return 0;
    }
}

void E85Kngmd::mcu_write(unsigned int address, unsigned int value)
{
    if (address < MCU_BUFFER) {
        uint8_t * bank = (i_p1.value & P1_READ_BANK) ? m_read : m_write;
        bank[(address >> 1) & 7] = (uint8_t)value;
        return;
    }
    if (address >= MCU_FDC) {
        m_fdc->set_value((address >> 1) & 3, value & 0xFF);
        return;
    }
    switch (address) {
    case MCU_BUFFER:
    case MCU_BUFFER_W:
        m_buffer[m_pointer] = (uint8_t)value;
        m_pointer = (m_pointer + 1) & 0777;
        break;
    case MCU_POINTER:
        m_pointer = 0;
        break;
    case MCU_RELEASE:
        m_busy = false;
        update_pins();
        break;
    default:
        break;
    }
}

unsigned int E85Kngmd::get_value_word(unsigned int address)
{
    if (address >= MCU_BASE) return mcu_read((address - MCU_BASE) & 0x1F, false);
    return host_read(address & 076, false);
}

void E85Kngmd::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (address >= MCU_BASE) mcu_write((address - MCU_BASE) & 0x1F, value & 0xFF);
    else host_write(address & 076, value & 0xFF);
}

unsigned int E85Kngmd::get_value(unsigned int address)
{
    if (address >= MCU_BASE) return get_value_word(address);
    return (address & 1) ? 0 : get_value_word(address);
}

// То же без запуска прошивки и сдвига адреса буфера: отладчик, LOG
unsigned E85Kngmd::get_direct(unsigned address)
{
    if (address >= MCU_BASE) return mcu_read((address - MCU_BASE) & 0x1F, true);
    return (address & 1) ? 0 : host_read(address & 076, true);
}

void E85Kngmd::set_value(unsigned int address, unsigned int value, bool force)
{
    if (address >= MCU_BASE || (address & 1) == 0) set_value_word(address, value, force);
}

std::vector<DeviceFieldInfo> E85Kngmd::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"regs_write", "Registers the processor writes, R0-R7", false});
    r.push_back({"regs_read",  "Registers the processor reads, R10-R17", false});
    r.push_back({"busy",       "The bus is with the controller", false});
    r.push_back({"commands",   "Commands started since the start", false});
    return r;
}

bool E85Kngmd::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "regs_write" || field == "regs_read") {
        for (unsigned int i = 0; i < 8; i++) out.values.push_back(field == "regs_write" ? m_write[i] : m_read[i]);
        return true;
    }
    if (field == "busy")     { out.values.push_back(m_busy ? 1 : 0); return true; }
    if (field == "commands") { out.width = 16; out.values.push_back(m_commands); return true; }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void E85Kngmd::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.array("write", m_write, 8);
    w.array("read", m_read, 8);
    w.hex("buffer", m_buffer, sizeof(m_buffer));
    w.n("pointer", m_pointer);
    w.b("busy", m_busy);
    w.n("ready_wait", m_ready_wait);
    w.n("commands", m_commands);
}

emulator::Result E85Kngmd::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.array("write", m_write, 8);
    r.array("read", m_read, 8);
    r.hex("buffer", m_buffer, sizeof(m_buffer));
    r.u("pointer", m_pointer);
    r.b("busy", m_busy);
    r.u("ready_wait", m_ready_wait);
    r.u("commands", m_commands);
    return emulator::Result::ok();
}

ComputerDevice * create_e85_kngmd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Kngmd(im, cd);
}
