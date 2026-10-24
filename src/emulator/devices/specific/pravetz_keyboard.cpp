// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Правец-8 keyboard (82: Apple ][ latch, 8А/8С: the CM650 one)

#include "pravetz_keyboard.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

static const size_t QUEUE_LIMIT = 64;

PravetzKeyboard::PravetzKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    Keyboard(im, cd)
    , i_pb(this, im, 3, "pb", MODE_W)
    , i_led_caps(this, im, 1, "led_caps", MODE_W)
    , i_led_cl(this, im, 1, "led_cl", MODE_W)
{
    m_clocked = true;   //the codes are handed over in clock()
}

emulator::Result PravetzKeyboard::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    m_has_kd7 = read_confg_value(cd, "kd7", false, true);

    const std::string iou = cd->get_parameter("iou", false).value;
    if (!iou.empty()) m_iou = dynamic_cast<Apple2IOU*>(im->dm->get_device_by_name(iou));

    struct { const char * param; const char * def; unsigned int * key; } keys[] = {
        {"ruslat", "f10", &m_key_ruslat},
        {"caps",   "caps", &m_key_caps},
        {"reset",  "f12", &m_key_reset},
        {"f1",     "f1",  &m_key_f1},
        {"f2",     "f2",  &m_key_f2},
    };
    for (auto &k : keys) {
        const std::string n = read_confg_value(cd, k.param, false, std::string(k.def));
        *k.key = (n.empty() || str_tolower(n) == "none") ? _FFFF : translate_key(n);
    }

    const std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PravetzKeyboard|" + std::string(QT_TRANSLATE_NOOP("PravetzKeyboard", "Keyboard map file is expected")) + "} " + name);
    const std::string content = dsk_tools::utf8_read_file(map_file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PravetzKeyboard|" + std::string(QT_TRANSLATE_NOOP("PravetzKeyboard", "Error reading map file")) + "} " + map_file);

    // "host[/mods]: code", mods of S, R and 8. The slash key is "slash"
    const std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = str_trim(lines[i]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;
        const size_t colon = line.find(':', 1);
        bool ok = colon != std::string::npos;
        std::string left, mods, value;
        if (ok) {
            left = str_trim(line.substr(0, colon));
            value = str_trim(line.substr(colon + 1));
            const size_t slash = left.find('/', 1);
            if (slash != std::string::npos) {
                mods = str_toupper(str_trim(left.substr(slash + 1)));
                left = str_trim(left.substr(0, slash));
                for (size_t j = 0; j < mods.size(); j++)
                    if (std::string("SR8").find(mods[j]) == std::string::npos) ok = false;
            }
        }
        Entry e;
        e.host = ok ? translate_key(left) : _FFFF;
        if (!ok || e.host == _FFFF || value.empty())
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{PravetzKeyboard|" + std::string(QT_TRANSLATE_NOOP("PravetzKeyboard", "Map file entry is incorrect")) + "} " + line);
        try {
            e.code = parse_numeric_value(value);
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{PravetzKeyboard|" + std::string(QT_TRANSLATE_NOOP("PravetzKeyboard", "Invalid value in the map file")) + "} " + line);
        }
        e.shift = mods.find('S') != std::string::npos;
        e.rus = mods.find('R') != std::string::npos;
        e.mode8 = mods.find('8') != std::string::npos;
        for (size_t j = 0; j < m_map.size(); j++)
            if (m_map[j].host == e.host && m_map[j].shift == e.shift && m_map[j].rus == e.rus && m_map[j].mode8 == e.mode8)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{PravetzKeyboard|" + std::string(QT_TRANSLATE_NOOP("PravetzKeyboard", "Duplicate entry in the map file")) + "} " + line);
        m_map.push_back(e);
    }

    m_repeat_delay = m_system_clock / 2;
    m_repeat_period = m_system_clock / 15;
    i_pb.change(0);
    update_leds();
    return emulator::Result::ok();
}

// The layout, when the config lists the maps it offers (map_1, map_2, ...
// titled map_title_N): ГОСТ, БДС or ЯВЕРТЪ on the same keyboard
ConfigFields PravetzKeyboard::get_config_fields()
{
    ConfigField f;
    f.name = "map";
    const std::string label = str_trim(cd->get_parameter("map_label", false).value);
    f.title = label.empty() ? std::string(QT_TRANSLATE_NOOP("ConfigFields", "Keyboard layout")) : label;
    f.type = CONFIG_FIELD_CHOICE;
    for (unsigned n = 1; ; n++) {
        const std::string file = str_trim(cd->get_parameter("map_" + std::to_string(n), false).value);
        if (file.empty()) break;
        const std::string title = str_trim(cd->get_parameter("map_title_" + std::to_string(n), false).value);
        f.values.push_back({file, title.empty() ? file : title});
    }
    if (f.values.empty()) return {};
    f.def = f.values[0].value;
    return {f};
}

void PravetzKeyboard::reset(bool cold)
{
    Keyboard::reset(cold);
    {
        compat_lock_guard lock(m_queue_mutex);
        m_queue.clear();
        m_queued = false;
    }
    // The keyboard is reset with the machine: Latin, capitals (5.9)
    m_strobe = false;
    m_held = 0;
    m_down.clear();
    m_shift = m_ctrl = false;
    m_pb = 0;
    i_pb.change(0);
    Keyboard::set_rus(false);
    if (cold) m_caps = false;
    update_leds();
}

void PravetzKeyboard::set_rus(bool new_rus)
{
    Keyboard::set_rus(new_rus);
    update_leds();
}

void PravetzKeyboard::update_leds()
{
    i_led_caps.change(m_caps ? 1 : 0);
    i_led_cl.change(rus_mode ? 1 : 0);
}

void PravetzKeyboard::key_down(unsigned int key)
{
    compat_lock_guard lock(m_queue_mutex);
    if (m_queue.size() < QUEUE_LIMIT) m_queue.push_back(std::make_pair(key, true));
    m_queued = true;
}

void PravetzKeyboard::key_up(unsigned int key)
{
    compat_lock_guard lock(m_queue_mutex);
    if (m_queue.size() < QUEUE_LIMIT) m_queue.push_back(std::make_pair(key, false));
    m_queued = true;
}

void PravetzKeyboard::clock(unsigned int counter)
{
    if (m_held > 0 && m_repeat_delay > 0) {
        m_repeat_clock += counter;
        if (m_repeat_clock >= m_repeat_delay) {
            m_repeat_clock -= m_repeat_period;
            m_strobe = true;
        }
    }
    if (!m_queued) return;
    std::deque<std::pair<unsigned int, bool> > q;
    {
        compat_lock_guard lock(m_queue_mutex);
        q.swap(m_queue);
        m_queued = false;
    }
    for (size_t i = 0; i < q.size(); i++) process(q[i].first, q[i].second);
}

// The best entry: Shift, then the alphabet, then the code table as asked,
// each falling back to the plain entry. A symbol of the host's upper register
// ($, +) has only a Shift entry, and a script types it without Shift
int PravetzKeyboard::find(unsigned int key, bool shift, bool rus, bool mode8) const
{
    for (int si = 0; si < 2; si++) {
        const bool s = (si == 0) ? shift : !shift;
        for (int r = rus ? 1 : 0; r >= 0; r--)
            for (int e = mode8 ? 1 : 0; e >= 0; e--)
                for (size_t i = 0; i < m_map.size(); i++)
                    if (m_map[i].host == key && m_map[i].shift == s
                        && m_map[i].rus == (r != 0) && m_map[i].mode8 == (e != 0))
                        return int(i);
    }
    return -1;
}

void PravetzKeyboard::process(unsigned int key, bool press)
{
    if (key == EmuKey::Shift)   { m_shift = press; return; }
    if (key == EmuKey::Control) { m_ctrl = press; return; }
    if (key == m_key_f1 || key == m_key_f2) {
        const unsigned bit = (key == m_key_f1) ? 1 : 2;
        m_pb = press ? (m_pb | bit) : (m_pb & ~bit);
        i_pb.change(m_pb);
        return;
    }
    if (key == m_key_ruslat) { if (press) set_rus(!rus_mode); return; }
    if (key == m_key_caps)   { if (press) { m_caps = !m_caps; update_leds(); } return; }
    if (key == m_key_reset) {
        // RESET works only together with CTRL
        if (press && m_ctrl) im->dm->reset_devices(false);
        return;
    }

    if (!press) {
        // Only a key that sent a code is held for AKD
        for (size_t i = 0; i < m_down.size(); i++)
            if (m_down[i] == key) { m_down.erase(m_down.begin() + i); break; }
        m_held = static_cast<unsigned int>(m_down.size());
        return;
    }

    const bool mode8 = m_has_kd7 && m_iou != nullptr && m_iou->softsw();
    const int i = find(key, m_shift, rus_mode, mode8);
    if (i < 0) return;
    const Entry &e = m_map[i];
    unsigned int code = e.code;
    if (code == 0) return;      // a key the table says sends nothing here

    if (m_ctrl) {
        // CTRL takes the Latin letter of the key whatever the alphabet:
        // $80-$9F
        const int l = find(key, false, false, false);
        const unsigned int latin = (l >= 0) ? m_map[l].code : code;
        if (latin >= 0xC0 && latin <= 0xDF) code = latin & 0x9F;
    } else if (mode8 && !e.mode8 && !e.shift && code >= 0xC0 && code <= 0xFE) {
        // A letter of the 8-bit table: small unless Shift or Caps Lock
        if (!(m_shift ^ m_caps)) code -= 0x80;
    } else if (!m_has_kd7) {
        code |= 0x80;
    }

    m_latch = code & 0xFF;
    if (!m_has_kd7) m_latch |= 0x80;
    m_strobe = true;
    m_repeat_clock = 0;
    bool known = false;
    for (size_t j = 0; j < m_down.size(); j++) if (m_down[j] == key) known = true;
    if (!known) m_down.push_back(key);
    m_held = static_cast<unsigned int>(m_down.size());
    m_codes++;
}

// TYPE: in the 8-bit table a letter key gives the small letter, so a capital
// needs Shift (or, with Caps Lock on, the small one does). The 7-bit table
// has capitals only
bool PravetzKeyboard::needs_shift_char(char ch, unsigned int key)
{
    (void)key;
    const bool lower = ch >= 'a' && ch <= 'z', upper = ch >= 'A' && ch <= 'Z';
    if (!lower && !upper) return false;
    const bool mode8 = m_has_kd7 && m_iou != nullptr && m_iou->softsw();
    return mode8 && (upper != m_caps);
}

// TYPE of a Russian letter: the key whose Cyrillic code it is. The capitals
// sit at $E0-$FE in the order of KOI-8, but the Правец has Ъ at $F9 and Ы
// only in the 8-bit table ($7F, the capital $5F). Ё is typed as Е
bool PravetzKeyboard::key_for_char(unsigned int codepoint, unsigned int &key, bool &shift)
{
    static const unsigned int ORDER[31] = {
        0x42E, 0x410, 0x411, 0x426, 0x414, 0x415, 0x424, 0x413, 0x425, 0x418, 0x419,
        0x41A, 0x41B, 0x41C, 0x41D, 0x41E, 0x41F, 0x42F, 0x420, 0x421, 0x422, 0x423,
        0x416, 0x412, 0x42C, 0x42A, 0x417, 0x428, 0x42D, 0x429, 0x427
    };
    unsigned int cp = codepoint;
    if (cp == 0x401 || cp == 0x451) cp = (cp == 0x401) ? 0x415 : 0x435;
    bool upper;
    if (cp >= 0x410 && cp <= 0x42F) upper = true;
    else if (cp >= 0x430 && cp <= 0x44F) { upper = false; cp -= 0x20; }
    else return false;

    const bool mode8 = m_has_kd7 && m_iou != nullptr && m_iou->softsw();
    if (cp == 0x42B) {
        // Ы: an entry of the 8-bit table, sent as it is
        if (!mode8) return false;
        const unsigned int want = upper ? 0x5F : 0x7F;
        for (size_t i = 0; i < m_map.size(); i++)
            if (m_map[i].rus && m_map[i].mode8 && (m_map[i].code & 0xFF) == want) {
                key = m_map[i].host;
                shift = m_map[i].shift;
                return true;
            }
        return false;
    }
    unsigned int code = 0;
    for (unsigned int i = 0; i < 31; i++) if (ORDER[i] == cp) code = 0xE0 + i;
    if (code == 0) return false;
    // The 82 (and 8М) keeps Ю, Ш, Щ, Ч on the codes of @ [ ] ^
    if (!m_has_kd7) {
        if (cp == 0x42E) code = 0xC0;
        else if (cp == 0x428) code = 0xDB;
        else if (cp == 0x429) code = 0xDD;
        else if (cp == 0x427) code = 0xDE;
    }

    for (size_t i = 0; i < m_map.size(); i++)
        if (m_map[i].rus && !m_map[i].mode8 && (m_map[i].code & 0xFF) == code) {
            key = m_map[i].host;
            // The 8-bit table makes the letter small unless Shift (or Caps
            // Lock) asks for the capital; the 7-bit one has capitals only
            shift = mode8 ? (upper != m_caps) : m_map[i].shift;
            return true;
        }
    return false;
}

std::vector<DeviceFieldInfo> PravetzKeyboard::get_device_fields()


{
    std::vector<DeviceFieldInfo> r = Keyboard::get_device_fields();
    r.push_back({"latch",  "The last code, bit 7 the eighth bit (KD7)", false});
    r.push_back({"strobe", "1 until the program takes the code",       false});
    r.push_back({"caps",   "Caps Lock",                                false});
    r.push_back({"cl",     "C/L Lock: 1 for the Cyrillic alphabet",    false});
    r.push_back({"codes",  "Codes sent since the start",               false});
    return r;
}

bool PravetzKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "latch" || field == "strobe" || field == "caps" || field == "cl" || field == "codes") {
        out.numeric = true;
        if (field == "latch") out.values.push_back(m_latch);
        else if (field == "strobe") out.values.push_back(m_strobe ? 1 : 0);
        else if (field == "caps") out.values.push_back(m_caps ? 1 : 0);
        else if (field == "cl") out.values.push_back(rus_mode ? 1 : 0);
        else out.values.push_back(m_codes);
        return true;
    }
    return Keyboard::get_field(field, from, to, out);
}

void PravetzKeyboard::save_state(StateWriter &w)
{
    Keyboard::save_state(w);
    w.u("latch", m_latch, 8);
    w.b("strobe", m_strobe);
    w.b("caps", m_caps);
    w.b("rus", rus_mode);
    w.n("codes", m_codes);
}

emulator::Result PravetzKeyboard::load_state(const StateReader &r)
{
    emulator::Result res = Keyboard::load_state(r);
    if (!res) return res;
    r.u("latch", m_latch);
    r.b("strobe", m_strobe);
    r.b("caps", m_caps);
    r.b("rus", rus_mode);
    r.u("codes", m_codes);
    m_held = 0;
    m_down.clear();
    m_shift = m_ctrl = false;
    m_pb = 0;
    return emulator::Result::ok();
}

// The host is holding nothing now: F1 and F2 come back up
void PravetzKeyboard::state_restored()
{
    Keyboard::state_restored();
    i_pb.change(0);
    update_leds();
}

ComputerDevice * create_pravetz_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PravetzKeyboard(im, cd);
}
