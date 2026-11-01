// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Am9519 (КР1818ВН19) universal interrupt controller on a К1801 bus

#include <cstring>

#include "am9519.h"

#define CALLBACK_IRQ    1
#define CALLBACK_CHAIN  2
#define CALLBACK_IAKO   3

#define MODE_ROTATING   0001
#define MODE_COMMON     0002
#define MODE_POLLED     0004
#define MODE_ARMED      0200

#define WRITE_NONE      0
#define WRITE_IMR       1
#define WRITE_ACR       2
#define WRITE_VECTOR    3

Am9519::Am9519(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irq(this, im, 8, "irq", MODE_R, CALLBACK_IRQ)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , m_irq(i_virq, i_vector)
{
    can_read = true;
    can_write = true;
    addresable_size = 4;
}

// Vectors survive the command, as the description says (table 38)
void Am9519::chip_reset()
{
    m_irr = 0;
    m_isr = 0;
    m_imr = 0xFF;
    m_acr = 0;
    m_mode = 0;
    m_write = WRITE_NONE;
    m_last = 7;
}

void Am9519::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) memset(m_vectors, 0, sizeof(m_vectors));
    chip_reset();
    m_own = 0;
    m_own_line = -1;
    m_irq.clear();
    i_virq.change(1);
}

// The line to serve: the first request in the order of priority, unless a line
// of the same or a higher priority is in service. -1 for none
int Am9519::highest() const
{
    const unsigned int pending = m_irr & ~m_imr & 0xFF;
    if (pending == 0) return -1;
    const unsigned int first = (m_mode & MODE_ROTATING) ? ((m_last + 1) & 7) : 0;
    for (unsigned int i = 0; i < 8; i++) {
        const unsigned int line = (first + i) & 7;
        if (m_isr & (1u << line)) return -1;
        if (pending & (1u << line)) return (int)line;
    }
    return -1;
}

void Am9519::update()
{
    m_own = 0;
    m_own_line = -1;
    if ((m_mode & MODE_ARMED) != 0 && (m_mode & MODE_POLLED) == 0) {
        const int line = highest();
        if (line >= 0) {
            m_own_line = line;
            m_own = m_vectors[(m_mode & MODE_COMMON) ? 0 : line];
        }
    }
    m_irq.offer(VirqLine::chain(m_own, i_virq_in, i_vector_in));
}

void Am9519::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_IRQ:
        m_irr |= (uint8_t)(new_value & ~old_value);
        update();
        break;
    case CALLBACK_IAKO: {
        const unsigned int vector = new_value & 0xFFFF;
        if (vector == 0) return;
        if (m_own_line >= 0 && vector == m_own) {
            const unsigned int bit = 1u << m_own_line;
            m_irr &= (uint8_t)~bit;
            if ((m_acr & bit) == 0) m_isr |= (uint8_t)bit;
            m_last = (unsigned int)m_own_line;
            m_served++;
            // The processor has dropped its latch: the next request must
            // make a new edge, even with the same vector
            m_irq.offer(0);
        }
        update();
        break;
    }
    default:
        update();
        break;
    }
}

// Table 38. The codes between the listed ones do what their neighbours do: the
// chip decodes the upper bits only
void Am9519::command(unsigned int value)
{
    const unsigned int bit = 1u << (value & 7);
    switch (value >> 3) {
    case 000:
    case 001:
        chip_reset();
        break;
    case 002: m_irr = 0; m_imr = 0; break;
    case 003: m_irr &= (uint8_t)~bit; m_imr &= (uint8_t)~bit; break;
    case 004: m_imr = 0; break;
    case 005: m_imr &= (uint8_t)~bit; break;
    case 006: m_imr = 0xFF; break;
    case 007: m_imr |= (uint8_t)bit; break;
    case 010: m_irr = 0; break;
    case 011: m_irr &= (uint8_t)~bit; break;
    case 012: m_irr = 0xFF; break;
    case 013: m_irr |= (uint8_t)bit; break;
    case 014:
    case 015: {
        // Clears the in-service bit of the highest priority
        const unsigned int first = (m_mode & MODE_ROTATING) ? ((m_last + 1) & 7) : 0;
        for (unsigned int i = 0; i < 8; i++) {
            const unsigned int b = 1u << ((first + i) & 7);
            if (m_isr & b) { m_isr &= (uint8_t)~b; break; }
        }
        break;
    }
    case 016: m_isr = 0; break;
    case 017: m_isr &= (uint8_t)~bit; break;
    case 020:
    case 021:
    case 022:
    case 023:
        m_mode = (uint8_t)((m_mode & 0340) | (value & 037));
        break;
    case 024:
    case 025:
        m_mode = (uint8_t)((m_mode & 0237) | ((value & 014) << 3));
        if ((value & 3) == 1) m_mode |= MODE_ARMED;
        else if ((value & 3) == 2) m_mode &= (uint8_t)~MODE_ARMED;
        else if ((value & 3) == 3) note_unsupported("am9519: forbidden mode command");
        break;
    case 026:
    case 027:
        m_write = WRITE_IMR;
        break;
    case 030:
    case 031:
        m_write = WRITE_ACR;
        break;
    default:
        if (value >= 0340) {
            m_write = WRITE_VECTOR;
            m_write_line = value & 7;
        } else
            note_unsupported("am9519: unknown command");
        break;
    }
    update();
}

// Table 39
unsigned int Am9519::status() const
{
    const unsigned int pending = m_irr & ~m_imr & 0xFF;
    unsigned int v = 0;
    if (pending != 0) {
        const unsigned int first = (m_mode & MODE_ROTATING) ? ((m_last + 1) & 7) : 0;
        for (unsigned int i = 0; i < 8; i++) {
            const unsigned int line = (first + i) & 7;
            if (pending & (1u << line)) { v = line; break; }
        }
    } else
        v = 0200;
    if (m_mode & MODE_ARMED) v |= 010;
    if (m_mode & MODE_POLLED) v |= 020;
    if (m_mode & MODE_ROTATING) v |= 040;
    return v;
}

unsigned int Am9519::get_value_word(unsigned int address)
{
    if (address & 2) return status();
    switch ((m_mode >> 5) & 3) {
    case 0:  return m_isr;
    case 1:  return m_imr;
    case 2:  return m_irr;
    default: return m_acr;
    }
}

void Am9519::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    value &= 0xFF;
    if (address & 2) {
        command(value);
        return;
    }
    switch (m_write) {
    case WRITE_IMR:
        m_imr = (uint8_t)value;
        update();
        break;
    case WRITE_ACR:
        m_acr = (uint8_t)value;
        break;
    case WRITE_VECTOR:
        m_vectors[m_write_line] = (uint8_t)value;
        update();
        break;
    default:
        note_unsupported("am9519: data written with nothing selected");
        break;
    }
}

unsigned int Am9519::get_value(unsigned int address)
{
    return (address & 1) ? 0 : get_value_word(address);
}

void Am9519::set_value(unsigned int address, unsigned int value, bool force)
{
    if ((address & 1) == 0) set_value_word(address, value, force);
}

std::vector<DeviceFieldInfo> Am9519::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"irr",     "Request register", false});
    r.push_back({"isr",     "In-service register", false});
    r.push_back({"imr",     "Mask register", false});
    r.push_back({"acr",     "Auto clear register", false});
    r.push_back({"mode",    "Mode register", false});
    r.push_back({"vectors", "Vectors of lines 0-7", false});
    r.push_back({"served",  "Interrupts taken from this chip", false});
    return r;
}

bool Am9519::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "irr")     { out.values.push_back(m_irr);  return true; }
    if (field == "isr")     { out.values.push_back(m_isr);  return true; }
    if (field == "imr")     { out.values.push_back(m_imr);  return true; }
    if (field == "acr")     { out.values.push_back(m_acr);  return true; }
    if (field == "mode")    { out.values.push_back(m_mode); return true; }
    if (field == "vectors") {
        for (unsigned int i = 0; i < 8; i++) out.values.push_back(m_vectors[i]);
        return true;
    }
    if (field == "served")  { out.width = 16; out.values.push_back(m_served); return true; }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void Am9519::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("irr", m_irr);
    w.u("isr", m_isr);
    w.u("imr", m_imr);
    w.u("acr", m_acr);
    w.u("mode", m_mode);
    w.array("vectors", m_vectors, 8);
    w.n("write", m_write);
    w.n("write_line", m_write_line);
    w.n("last", m_last);
    w.u("offered", m_irq.offered());
}

emulator::Result Am9519::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    unsigned int v = 0;
    if (r.u("irr", v)) m_irr = (uint8_t)v;
    if (r.u("isr", v)) m_isr = (uint8_t)v;
    if (r.u("imr", v)) m_imr = (uint8_t)v;
    if (r.u("acr", v)) m_acr = (uint8_t)v;
    if (r.u("mode", v)) m_mode = (uint8_t)v;
    r.array("vectors", m_vectors, 8);
    r.u("write", m_write);
    r.u("write_line", m_write_line);
    r.u("last", m_last);
    unsigned int offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    // Recomputed without a line moving: load_state() must not drive one
    m_own = 0;
    m_own_line = -1;
    if ((m_mode & MODE_ARMED) != 0 && (m_mode & MODE_POLLED) == 0) {
        m_own_line = highest();
        if (m_own_line >= 0) m_own = m_vectors[(m_mode & MODE_COMMON) ? 0 : m_own_line];
    }
    return emulator::Result::ok();
}

ComputerDevice * create_am9519(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Am9519(im, cd);
}
