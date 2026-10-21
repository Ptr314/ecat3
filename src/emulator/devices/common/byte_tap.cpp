// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The bytes a port sends out, kept for scripts and the ports window

#include "byte_tap.h"

void ByteTap::put(uint8_t b)
{
    m_output[m_put++ & (OUTPUT_SIZE - 1)] = b;
    // Флаг проверяется ещё раз под замком: окно могло выключить отвод
    // между проверкой и захватом, и байт лёг бы в следующий сеанс
    if (m_tap_on) {
        compat_lock_guard g(m_tap_mutex);
        if (m_tap_on && m_tap.size() < TAP_LIMIT) m_tap.push_back(b);
    }
}

void ByteTap::set_tap(bool on)
{
    compat_lock_guard g(m_tap_mutex);
    m_tap_on = on;
    if (!on) m_tap.clear();
}

void ByteTap::take_tap(std::vector<uint8_t> &out)
{
    out.clear();
    compat_lock_guard g(m_tap_mutex);
    out.swap(m_tap);
}

void ByteTap::add_output_fields(std::vector<DeviceFieldInfo> &r)
{
    r.push_back({"output",    "Последние выведенные байты, старые первыми; output(n) - последние n", true});
    r.push_back({"text",      "Те же байты текстом: ВК пропускается, прочие управляющие - точкой", true});
}

bool ByteTap::get_output_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) const
{
    if (field != "text" && field != "output") return false;
    unsigned int n = (from == 0 && to == 0)? OUTPUT_SIZE : ((from > OUTPUT_SIZE)? OUTPUT_SIZE : from);
    if (n > m_put) n = m_put;
    if (field == "text") {
        out.numeric = false;
        out.text.clear();
        for (unsigned int i = n; i > 0; i--) {
            const uint8_t c = m_output[(m_put - i) & (OUTPUT_SIZE - 1)] & 0177;
            if (c == 015) continue;
            out.text += (c == 012 || (c >= 040 && c < 0177)) ? (char)c : '.';
        }
        return true;
    }
    out.numeric = true;
    out.width = 8;
    for (unsigned int i = n; i > 0; i--)
        out.values.push_back(m_output[(m_put - i) & (OUTPUT_SIZE - 1)]);
    return true;
}
