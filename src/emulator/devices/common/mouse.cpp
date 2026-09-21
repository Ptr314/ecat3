// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Mouse with latched direction outputs (УВК-01 «Марсианка»)

#include "mouse.h"
#include "emulator/utils.h"

#define MOUSE_RESET 1
#define MOUSE_READ  2

// The ball cannot run ahead of the hand for long: steps the program has not
// taken yet are dropped beyond this many
#define MOUSE_MAX_PENDING 256

Mouse::Mouse(InterfaceManager *im, EmulatorConfigDevice *cd):
    PluggableDevice(im, cd)
    , i_out(this, im, 16, "out", MODE_W)
    , i_reset(this, im, 1, "reset", MODE_R, MOUSE_RESET)
    , i_read(this, im, 1, "read", MODE_R, MOUSE_READ)
    , m_bit_up(0), m_bit_right(0), m_bit_down(0), m_bit_left(0)
    , m_bit_button1(0), m_bit_button2(0)
    , m_period(1)
    , m_ticks(0)
    , m_latch(0)
    , m_unread(0)
    , m_buttons_out(0)
    , m_state(0)
    , m_dx(0)
    , m_dy(0)
    , m_buttons(0)
{
    device_class = "mouse";
    m_clocked = true;   //system_clock() is overridden here
}

emulator::Result Mouse::load_config(SystemData *sd)
{
    emulator::Result res = PluggableDevice::load_config(sd);
    if (!res) return res;

    // The bits are what the program of the machine reads, so they are machine
    // values; the defaults are those of the «Марсианка» on the БК port 0177714
    struct { const char * name; unsigned int * bit; unsigned int def; } bits[] = {
        {"up",      &m_bit_up,      0x01},
        {"right",   &m_bit_right,   0x02},
        {"down",    &m_bit_down,    0x04},
        {"left",    &m_bit_left,    0x08},
        {"button1", &m_bit_button1, 0x20},
        {"button2", &m_bit_button2, 0x40}
    };
    for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
        const std::string v = cd->get_parameter(bits[i].name, false).value;
        try {
            *bits[i].bit = v.empty()?bits[i].def:parse_numeric_value(v);
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Mouse|" + std::string(QT_TRANSLATE_NOOP("Mouse", "Incorrect value of a parameter")) + "} " + bits[i].name);
        }
    }

    // 1 m/s at 0.5 mm a step is the fastest the manual allows: 2000 steps a second
    unsigned int rate = 2000;
    const std::string r = cd->get_parameter("rate", false).value;
    if (!r.empty()) {
        try {
            rate = parse_numeric_value(r);
        } catch (const std::exception &) {
            rate = 0;
        }
        if (rate == 0)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Mouse|" + std::string(QT_TRANSLATE_NOOP("Mouse", "Incorrect value of a parameter")) + "} rate");
    }
    m_period = m_system_clock / rate;
    if (m_period == 0) m_period = 1;

    // Lines nobody has driven yet read as all ones
    if (m_plugged) {
        m_state = _FFFF;
        update();
    }
    return emulator::Result::ok();
}

void Mouse::reset(MAYBE_UNUSED bool cold)
{
    m_dx = 0;
    m_dy = 0;
    m_latch = 0;
    m_unread = 0;
    m_ticks = 0;
    update();
}

// A reset line that is not wired anywhere never resets the mouse; a wired one
// holds the flip-flops cleared while it is high
bool Mouse::reset_held() const
{
    return i_reset.linked_bits != 0 && (i_reset.value & 1) != 0;
}

void Mouse::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == MOUSE_READ) {
        // The port strobes the line on every read: whatever is set now has been seen
        if ((new_value & 1) != 0 && (old_value & 1) == 0) m_unread = 0;
        return;
    }

    if ((new_value & 1) != 0 && m_latch != 0) {
        // Steps cleared before the program could read them are made again
        if (m_unread & m_bit_right) add_pending(m_dx, 1);
        if (m_unread & m_bit_left)  add_pending(m_dx, -1);
        if (m_unread & m_bit_down)  add_pending(m_dy, 1);
        if (m_unread & m_bit_up)    add_pending(m_dy, -1);
        m_latch = 0;
        m_unread = 0;
        update();
    }
}

void Mouse::add_pending(std::atomic<int> &axis, int delta)
{
    int current = axis.load();
    int value;
    do {
        value = current + delta;
        if (value > MOUSE_MAX_PENDING) value = MOUSE_MAX_PENDING;
        if (value < -MOUSE_MAX_PENDING) value = -MOUSE_MAX_PENDING;
    } while (!axis.compare_exchange_weak(current, value));
}

void Mouse::system_clock(unsigned int counter)
{
    if (!m_plugged) return;

    unsigned buttons = m_buttons.load();
    unsigned buttons_out = ((buttons & 1)?m_bit_button1:0) | ((buttons & 2)?m_bit_button2:0);
    bool changed = (buttons_out != m_buttons_out);
    m_buttons_out = buttons_out;

    int dx = m_dx.load();
    int dy = m_dy.load();
    if (dx == 0 && dy == 0) {
        // The first step of a movement comes at once
        m_ticks = m_period;
        if (changed) update();
        return;
    }

    m_ticks += counter;
    if (m_ticks < m_period || reset_held()) {
        if (changed) update();
        return;
    }
    m_ticks = 0;

    // A step waits while the program has not taken the previous one
    const unsigned int before = m_latch;
    if (dx > 0 && (m_latch & m_bit_right) == 0) { m_latch |= m_bit_right; add_pending(m_dx, -1); }
    if (dx < 0 && (m_latch & m_bit_left) == 0)  { m_latch |= m_bit_left;  add_pending(m_dx, 1); }
    if (dy > 0 && (m_latch & m_bit_down) == 0)  { m_latch |= m_bit_down;  add_pending(m_dy, -1); }
    if (dy < 0 && (m_latch & m_bit_up) == 0)    { m_latch |= m_bit_up;    add_pending(m_dy, 1); }
    if (m_latch != before) {
        // Without the read strobe there is no telling, and the step is gone
        if (i_read.linked_bits != 0) m_unread |= m_latch & ~before;
        changed = true;
    }

    if (changed) update();
}

void Mouse::update()
{
    if (!m_plugged) return;
    unsigned int state = m_latch | m_buttons_out;
    if (state != m_state) {
        m_state = state;
        i_out.change(state);
    }
}

void Mouse::plug_changed()
{
    // The host keeps whatever it did while the mouse was out of the socket
    m_dx = 0;
    m_dy = 0;
    m_latch = 0;
    m_unread = 0;
    m_buttons_out = 0;
    if (m_plugged) {
        m_state = _FFFF;
        update();
    } else {
        // The socket may stay empty: a button held while the mouse is pulled
        // out must not stay pressed on the port
        m_state = 0;
        i_out.change(0);
    }
}

const char * Mouse::plug_title() const
{
    return QT_TRANSLATE_NOOP("DeviceOptions", "Mouse");
}

void Mouse::move(int dx, int dy, int buttons)
{
    if (!m_plugged) return;
    if (buttons >= 0) m_buttons = static_cast<unsigned>(buttons) & 3;
    if (dx != 0) add_pending(m_dx, dx);
    if (dy != 0) add_pending(m_dy, dy);
}

void Mouse::save_state(StateWriter &w)
{
    PluggableDevice::save_state(w);
    w.n("ticks", m_ticks);
    w.u("latch", m_latch);
    w.u("unread", m_unread);
    w.u("buttons_out", m_buttons_out);
    w.u("state", m_state);
    //m_dx, m_dy and m_buttons are what the host has moved and not handed over
    //yet. The host is not moving anything when a snapshot is opened
}

emulator::Result Mouse::load_state(const StateReader &r)
{
    emulator::Result res = PluggableDevice::load_state(r);
    if (!res) return res;
    r.u("ticks", m_ticks);
    r.u("latch", m_latch);
    r.u("unread", m_unread);
    r.u("buttons_out", m_buttons_out);
    r.u("state", m_state);
    m_dx = 0;
    m_dy = 0;
    m_buttons = 0;
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> Mouse::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = PluggableDevice::get_device_fields();
    r.push_back({"state",   "Bits on the output lines: direction flip-flops and buttons", false});
    r.push_back({"pending", "Steps the program has not taken yet, both axes together", false});
    r.push_back({"plugged", "1 when the mouse is plugged in", false});
    return r;
}

bool Mouse::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "state")   { out.values.push_back(m_state == _FFFF ? 0 : m_state); return true; }
    if (field == "pending") {
        int x = m_dx.load(), y = m_dy.load();
        out.values.push_back(static_cast<unsigned int>((x < 0?-x:x) + (y < 0?-y:y)));
        return true;
    }
    if (field == "plugged") { out.values.push_back(m_plugged?1:0); return true; }
    out.width = 0;
    out.numeric = false;
    return PluggableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_mouse(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Mouse(im, cd);
}
