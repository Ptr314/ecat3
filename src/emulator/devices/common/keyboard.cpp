// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract keyboard device

#include "keyboard.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

Keyboard::Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    i_stop(this, im, 1, "stop", MODE_W),
    rus_mode(false)
{
    reset_priority = 100;
}

emulator::Result Keyboard::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    use_remap = read_confg_value(cd, "use_remap", false, true);

    // The drawing of this machine's keyboard. A missing file is not an error:
    // the machine has to start anyway, the frontends just show no keyboard.
    const std::string picture = cd->get_parameter("picture", false).value;
    if (!picture.empty()) m_picture_file = find_file_location(sd, picture);

    // Lamps of the drawing this machine leaves dark, and the lamp of the case
    // latch. The drawing of a keyboard is shared by machines that wire its
    // lamps differently (15ВВВ: the 15ИЭ drives all eight, the Ириша two)
    m_led_off.clear();
    const std::vector<std::string> off = split_string(cd->get_parameter("led_off", false).value, '|', true);
    for (size_t i = 0; i < off.size(); i++) {
        const std::string id = str_trim(off[i]);
        if (!id.empty()) m_led_off.push_back(id);
    }
    m_led_case_lower = str_trim(cd->get_parameter("led_case_lower", false).value);

    //Interfaces start at _FFFF, which a config that inverts the line would read
    //as the key being held down from the moment the machine starts
    i_stop.change(0);

    res = load_remap(sd, "remap_arrows", KEY_REMAP_ARROWS);
    if (!res) return res;
    res = load_remap(sd, "remap_letters", KEY_REMAP_LETTERS);
    if (!res) return res;

    res = load_key_table(sd);
    if (!res) return res;

    return load_groups(sd);
}

emulator::Result Keyboard::load_groups(SystemData *sd)
{
    {
        compat_lock_guard lock(m_host_mutex);
        m_key_group.clear();
        m_group_owner.clear();
        m_group_deferred.clear();
        m_group_blocked.clear();
        m_group_hold = false;
    }
    const std::string name = cd->get_parameter("groups", false).value;
    if (name.empty()) return emulator::Result::ok();

    const std::string file = find_file_location(sd, name);
    const std::string content = file.empty() ? std::string() : dsk_tools::utf8_read_file(file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Error reading key group file")) + "} " + name);

    // A line is a group, the machine's key names separated by spaces. A line
    // "@hold" switches the whole file to the hold mode, see m_group_hold
    std::map<std::string, int> groups;
    bool hold = false;
    int count = 0;
    const std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t li = 0; li < lines.size(); li++) {
        std::string line = str_trim(lines[li]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;
        if (line == "@hold") {
            hold = true;
            continue;
        }
        for (size_t c = 0; c < line.size(); c++)
            if (line[c] == '\t') line[c] = ' ';
        const std::vector<std::string> words = split_string(line, ' ', true);
        for (size_t w = 0; w < words.size(); w++) {
            const std::string id = str_trim(words[w]);
            if (id.empty()) continue;
            bool known = false;
            for (size_t i = 0; i < m_key_ids.size() && !known; i++) known = (m_key_ids[i] == id);
            if (!known || groups.count(id) != 0)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Key group file entry is incorrect")) + "} " + id + " (" + file + ")");
            groups[id] = count;
        }
        count++;
    }

    compat_lock_guard lock(m_host_mutex);
    m_key_group.swap(groups);
    m_group_owner.assign(count, _FFFF);
    m_group_deferred.assign(count, 0);
    m_group_hold = hold;
    return emulator::Result::ok();
}

bool Keyboard::group_filter_locked(unsigned int k, unsigned int code, bool press, unsigned int &release)
{
    release = _FFFF;
    if (m_key_group.empty()) return false;

    for (size_t i = 0; i < m_group_blocked.size(); i++)
        if (m_group_blocked[i].first == k) {
            //Swallowed when it went down: its release is swallowed too, and so
            //is a second press of it before that
            if (!press) {
                const int g = m_group_blocked[i].second;
                m_group_blocked.erase(m_group_blocked.begin() + i);
                //The last key held in a group whose own key has been let go:
                //only now does the machine see that key come up
                if (m_group_deferred[g] && !group_busy_locked(g)) {
                    release = m_group_owner[g];
                    m_group_owner[g] = _FFFF;
                    m_group_deferred[g] = 0;
                }
            }
            return true;
        }

    if (!press) {
        for (size_t g = 0; g < m_group_owner.size(); g++)
            if (m_group_owner[g] == k && !m_group_deferred[g]) {
                //With other keys of the group still held the hold mode keeps
                //this one down for the machine (1801ВП1-014: the line it pulls
                //low stays low until every key is up)
                if (m_group_hold && group_busy_locked((int)g)) {
                    m_group_deferred[g] = 1;
                    return true;
                }
                m_group_owner[g] = _FFFF;
            }
        return false;
    }

    const std::map<std::string, int>::const_iterator it = m_key_group.find(host_key_id(code));
    if (it == m_key_group.end()) return false;
    unsigned int &owner = m_group_owner[it->second];
    if (owner == _FFFF || (owner == k && !m_group_deferred[it->second])) {
        owner = k;
        return false;
    }
    m_group_blocked.push_back(std::make_pair(k, it->second));
    m_blocked_count++;
    return true;
}

bool Keyboard::group_busy_locked(int g) const
{
    for (size_t i = 0; i < m_group_blocked.size(); i++)
        if (m_group_blocked[i].second == g) return true;
    return false;
}

bool Keyboard::last_host_swallowed()
{
    compat_lock_guard lock(m_host_mutex);
    return m_last_host_swallowed;
}

unsigned int Keyboard::last_host_released()
{
    compat_lock_guard lock(m_host_mutex);
    return m_last_host_released;
}

// A host key let go for the machine: the code it went down as comes up
void Keyboard::host_release(unsigned int k)
{
    unsigned int code = _FFFF;
    {
        compat_lock_guard lock(m_host_mutex);
        for (size_t i = 0; i < m_host_down.size(); i++)
            if (m_host_down[i].first == k) {
                code = m_host_down[i].second;
                m_host_down.erase(m_host_down.begin() + i);
                break;
            }
    }
    if (code == _FFFF) return;
    key_up(code);
    //A host modifier is not the key a "once" modifier waits for: Shift let
    //go on the host must not drop АР2 latched on the drawing
    if (k != EmuKey::Shift && k != EmuKey::Control && k != EmuKey::Alt
        && k != EmuKey::CapsLock && k != EmuKey::NumLock && k != EmuKey::ScrollLock)
        release_once_modifiers();
}

emulator::Result Keyboard::load_remap(SystemData *sd, const std::string &param, int mode)
{
    m_remap[mode].clear();
    const std::string name = cd->get_parameter(param, false).value;
    if (name.empty()) return emulator::Result::ok();

    const std::string file = find_file_location(sd, name);
    const std::string content = file.empty() ? std::string() : dsk_tools::utf8_read_file(file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Error reading remap file")) + "} " + name);

    // "host key: host key", both by their script names. The colon is looked
    // for after the first character, so ":" itself can be remapped
    const std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t li = 0; li < lines.size(); li++) {
        std::string line = str_trim(lines[li]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;
        const size_t colon = line.find(':', 1);
        const unsigned int from = colon == std::string::npos ? _FFFF : translate_key_name(str_trim(line.substr(0, colon)));
        const unsigned int to = colon == std::string::npos ? _FFFF : translate_key_name(str_trim(line.substr(colon + 1)));
        if (from == _FFFF || to == _FFFF)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Remap file entry is incorrect")) + "} " + line);
        m_remap[mode].push_back(std::make_pair(from, to));
    }
    return emulator::Result::ok();
}

unsigned int Keyboard::remap_locked(unsigned int key) const
{
    const std::vector<std::pair<unsigned int, unsigned int> > &table = m_remap[m_remap_mode];
    for (size_t i = 0; i < table.size(); i++)
        if (table[i].first == key) {
            //A keypad key reaches a keyboard without a keypad as the plain one,
            //the way Emulator::key_event() hands it over
            return keypad_keys() ? table[i].second : (table[i].second & ~(unsigned int)EmuKey::Keypad);
        }
    return key;
}

void Keyboard::set_remap_mode(int mode)
{
    compat_lock_guard lock(m_host_mutex);
    m_remap_mode = (mode >= 0 && mode < KEY_REMAP_COUNT) ? mode : KEY_REMAP_NONE;
}

unsigned int Keyboard::remap_key(unsigned int key)
{
    compat_lock_guard lock(m_host_mutex);
    return remap_locked(key);
}

void Keyboard::key_event(unsigned int key, unsigned int native_key, bool press, bool host)
{
    unsigned int k;
    if (known_key(key))
        k = key;
    else if (known_key(native_key))
        k = native_key;
    else return;

    unsigned int code = _FFFF;
    bool swallowed = false;
    unsigned int deferred = _FFFF;     // a key the group held down, let go now
    {
        compat_lock_guard lock(m_host_mutex);
        if (host) {
            //The code the key went down as decides the group, the same way the
            //release below lifts that code
            unsigned int down = _FFFF;
            for (size_t i = 0; i < m_host_down.size() && down == _FFFF; i++)
                if (m_host_down[i].first == k) down = m_host_down[i].second;
            if (down == _FFFF) down = rus_translate(remap_locked(k));
            swallowed = group_filter_locked(k, down, press, deferred);
            m_last_host_swallowed = swallowed;
            m_last_host_released = deferred;
        }
        if (!swallowed) {
            for (size_t i = 0; i < m_host_down.size(); i++)
                if (m_host_down[i].first == k) {
                    code = m_host_down[i].second;
                    if (!press) m_host_down.erase(m_host_down.begin() + i);
                    break;
                }
            if (code == _FFFF) {
                code = rus_translate(host ? remap_locked(k) : k);
                if (press) m_host_down.push_back(std::make_pair(k, code));
            }
        }
    }

    if (swallowed) {
        //The swallowed key was the last one holding its group, and the key
        //that took the group was let go before it: that one comes up now
        if (deferred != _FFFF) host_release(deferred);
        return;
    }

    if (press)
        key_down(code);
    else {
        key_up(code);
        //A host modifier is not the key a "once" modifier waits for: Shift let
        //go on the host must not drop АР2 latched on the drawing
        if (k != EmuKey::Shift && k != EmuKey::Control && k != EmuKey::Alt
            && k != EmuKey::CapsLock && k != EmuKey::NumLock && k != EmuKey::ScrollLock)
            release_once_modifiers();
    }
}

bool Keyboard::known_key(unsigned int code)
{
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (KEYS[i].code == code)
            return true;

    return false;
}

unsigned int translate_key_name(const std::string &key)
{
    std::string key_lower = str_tolower(key);
    for (unsigned int i=0; i<sizeof(KEYS)/sizeof(KeyDescription); i++)
        if (str_tolower(KEYS[i].name) == key_lower)
            return KEYS[i].code;

    return _FFFF;
}

std::string key_name(unsigned int code)
{
    const unsigned int count = sizeof(KEYS)/sizeof(KeyDescription);
    for (unsigned int i=0; i<count; i++)
        if (KEYS[i].code == code && KEYS[i].name.length() == 1)
            return KEYS[i].name;
    for (unsigned int i=0; i<count; i++)
        if (KEYS[i].code == code)
            return KEYS[i].name;
    return "";
}

void text_to_keys(const std::string &text, Keyboard * kbd, bool escapes,
                  std::vector<unsigned int> &keys, std::vector<bool> &shift,
                  std::vector<std::string> * unknown)
{
    //Cyrillic goes through the keyboard's РУС/ЛАТ switch: pressed before the
    //first Russian letter, again before the next character that is not one
    //(nor a space), and at the end the switch is left as it was found
    //A text of ASCII alone leaves the switch alone: a script may have turned
    //РУС on itself and type Russian with the Latin keys
    bool has_cyrillic = false;
    for (size_t i = 0; i < text.length(); i++)
        if (static_cast<unsigned char>(text[i]) >= 0x80) has_cyrillic = true;
    const unsigned int rus_key = (kbd != nullptr && has_cyrillic) ? kbd->rus_switch_key() : _FFFF;
    const bool rus_start = kbd != nullptr && kbd->rus_now();
    bool rus = rus_start;
    for (size_t i = 0; i < text.length(); i++)
    {
        const char ch = text[i];
        std::string name;

        if (static_cast<unsigned char>(ch) >= 0x80)
        {
            //A UTF-8 sequence: no key has a name outside ASCII, the keyboard
            //may know a key that types the letter
            size_t j = i + 1;
            while (j < text.length() && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80) j++;
            const unsigned char lead = static_cast<unsigned char>(ch);
            unsigned int cp = (lead >= 0xF0) ? (lead & 0x07) : (lead >= 0xE0) ? (lead & 0x0F) : (lead & 0x1F);
            for (size_t k = i + 1; k < j; k++) cp = (cp << 6) | (static_cast<unsigned char>(text[k]) & 0x3F);
            unsigned int key = 0;
            bool sh = false;
            if (kbd != nullptr && rus_key != _FFFF && kbd->key_for_char(cp, key, sh)) {
                if (!rus) { keys.push_back(rus_key); shift.push_back(false); rus = true; }
                keys.push_back(key);
                shift.push_back(sh);
            } else if (unknown != nullptr)
                unknown->push_back(text.substr(i, j - i));
            i = j - 1;
            continue;
        }
        //Back to Latin for anything but a space: in РУС the punctuation keys
        //of a ЙЦУКЕН layout are letters
        if (rus && rus_key != _FFFF && ch != ' ') {
            keys.push_back(rus_key); shift.push_back(false); rus = false;
        }

        if (escapes && ch == '\\' && i + 1 < text.length())
        {
            //Escapes, so that a line feed can be typed from a single line script
            const char n = text[++i];
            if      (n == 'n') name = "ret";
            else if (n == 't') name = "tab";
            else name = std::string(1, n);
        }
        else if (ch == ' ') name = "space";
        else if (!escapes && ch == '\r') continue;
        else if (!escapes && ch == '\n') name = "ret";
        else if (!escapes && ch == '\t') name = "tab";
        else name = std::string(1, ch);

        const unsigned int code = translate_key_name(name);
        if (code == _FFFF) {
            if (unknown != nullptr) unknown->push_back(name);
            continue;
        }
        keys.push_back(code);
        shift.push_back(kbd != nullptr && (name.size() == 1 ? kbd->needs_shift_char(name[0], code) : kbd->needs_shift(code)));
    }
    if (rus != rus_start) { keys.push_back(rus_key); shift.push_back(false); }
}

unsigned int Keyboard::translate_key(const std::string &key)
{
    return translate_key_name(key);
}

void Keyboard::set_rus(bool new_rus)
{
    rus_mode = new_rus;
}

unsigned int Keyboard::rus_translate(unsigned int code)
{
    if (rus_mode && use_remap) {
        for (auto i : RUS_REMAP)
            if (i[0] == code) return i[1];
        return code;
    }
    return code;
}

void Keyboard::register_key_id(const std::string &id, KeyRole role)
{
    for (size_t i = 0; i < m_key_ids.size(); i++)
        if (m_key_ids[i] == id) return;
    m_key_ids.push_back(id);
    if (role != KEY_ROLE_NORMAL)
        m_key_roles.push_back(std::make_pair(id, role));
}

KeyRole Keyboard::key_role(const std::string &id) const
{
    for (size_t i = 0; i < m_key_roles.size(); i++)
        if (m_key_roles[i].first == id) return m_key_roles[i].second;
    return KEY_ROLE_NORMAL;
}

// A latching key stays down on the picture until it is pressed again: the
// machine keeps the state itself, so releasing it would be a lie.
bool Keyboard::is_latching(const std::string &id) const
{
    switch (key_role(id)) {
        case KEY_ROLE_RUS_TOGGLE:
        case KEY_ROLE_RUS_ON:
        case KEY_ROLE_RUS_OFF:
        case KEY_ROLE_CASE_LOWER:
        case KEY_ROLE_CASE_UPPER:
        case KEY_ROLE_TOGGLE:
            return true;
        default:
            return false;
    }
}

bool Keyboard::toggled(const std::string &id) const
{
    compat_lock_guard lock(m_held_mutex);
    for (size_t i = 0; i < m_toggled.size(); i++)
        if (m_toggled[i] == id) return true;
    return false;
}

void Keyboard::set_toggled(const std::string &id, bool on)
{
    {
        compat_lock_guard lock(m_held_mutex);
        size_t i = 0;
        while (i < m_toggled.size() && m_toggled[i] != id) i++;
        if (i < m_toggled.size()) {
            if (on) return;
            m_toggled.erase(m_toggled.begin() + i);
        } else {
            if (!on) return;
            m_toggled.push_back(id);
        }
    }
    toggles_changed();
}

Keyboard::ClickMode Keyboard::click_mode(const std::string &id) const
{
    switch (key_role(id)) {
        case KEY_ROLE_SHIFT:
        case KEY_ROLE_CTRL:
        case KEY_ROLE_ALT:
        case KEY_ROLE_MODIFIER:
            return CLICK_TOGGLE;
        case KEY_ROLE_RUS_TOGGLE:
        case KEY_ROLE_RUS_ON:
        case KEY_ROLE_RUS_OFF:
        case KEY_ROLE_CASE_UPPER:
        case KEY_ROLE_CASE_LOWER:
        case KEY_ROLE_TOGGLE:
            return CLICK_TAP;
        default:
            return CLICK_HOLD;
    }
}

void Keyboard::note_id(const std::string &id, bool press)
{
    if (id.empty()) return;
    compat_lock_guard lock(m_held_mutex);
    for (size_t i = 0; i < m_ids_held.size(); i++)
        if (m_ids_held[i] == id) {
            if (!press) m_ids_held.erase(m_ids_held.begin() + i);
            return;
        }
    if (press) m_ids_held.push_back(id);
}

// A latching key is not "held" by anyone - the machine keeps the state. Its
// place on the drawing therefore lights up from that state, not from a press,
// which is also why it is derived here instead of being stored.
std::vector<std::string> Keyboard::ids_held() const
{
    std::vector<std::string> r;
    {
        compat_lock_guard lock(m_held_mutex);
        r = m_ids_held;
        r.insert(r.end(), m_toggled.begin(), m_toggled.end());
    }
    for (size_t i = 0; i < m_key_roles.size(); i++) {
        bool on = false;
        switch (m_key_roles[i].second) {
            case KEY_ROLE_RUS_TOGGLE:
            case KEY_ROLE_RUS_ON:    on = rus_mode; break;
            case KEY_ROLE_RUS_OFF:   on = !rus_mode; break;
            case KEY_ROLE_CASE_LOWER: on = m_case_lower; break;
            case KEY_ROLE_CASE_UPPER: on = !m_case_lower; break;
            default: continue;
        }
        if (on) r.push_back(m_key_roles[i].first);
    }
    return r;
}

// Two lamps for one register: the Агат has РУС and ЛАТ drawn side by side, and
// only one of them burns. A machine with a single lamp declares just that one
// in its drawing and gets it dark in the other alphabet. Each lamp names the
// key it makes redundant, so a drawing that has both would not say the same
// thing twice; one toggling key answers for both lamps.
std::vector<Keyboard::Indicator> Keyboard::indicators() const
{
    Indicator rus = {"led_rus", rus_mode, ""};
    Indicator lat = {"led_lat", !rus_mode, ""};
    for (size_t i = 0; i < m_key_roles.size(); i++)
        switch (m_key_roles[i].second) {
            case KEY_ROLE_RUS_ON:     rus.key = m_key_roles[i].first; break;
            case KEY_ROLE_RUS_OFF:    lat.key = m_key_roles[i].first; break;
            case KEY_ROLE_RUS_TOGGLE: rus.key = lat.key = m_key_roles[i].first; break;
            default: break;
        }

    std::vector<Indicator> r;
    r.push_back(rus);
    r.push_back(lat);

    if (!m_led_case_lower.empty()) {
        Indicator lower = {m_led_case_lower, m_case_lower, ""};
        for (size_t i = 0; i < m_key_roles.size(); i++)
            if (m_key_roles[i].second == KEY_ROLE_CASE_LOWER) lower.key = m_key_roles[i].first;
        r.push_back(lower);
    }
    for (size_t i = 0; i < m_led_off.size(); i++) {
        Indicator off = {m_led_off[i], false, ""};
        r.push_back(off);
    }
    return r;
}

// A modifier marked "once" stays latched only until the next key. It is let
// go after that key's release, so the key itself still goes out with it. The
// frontends see it gone from ids_held() and drop their latch
void Keyboard::release_once_modifiers()
{
    if (m_once_ids.empty()) return;
    std::vector<std::string> latched;
    {
        compat_lock_guard lock(m_held_mutex);
        latched = m_once_latched;
    }
    //What only looks held - the same contact closed by a key of the host,
    //which note_id() lights up under this name - is not a latch of the drawing
    for (size_t i = 0; i < latched.size(); i++)
        key_event_id(latched[i], false);
}

void Keyboard::key_event_id(const std::string &id, bool press)
{
    const KeyRole role = key_role(id);

    for (size_t i = 0; i < m_once_ids.size(); i++)
        if (m_once_ids[i] == id) {
            compat_lock_guard lock(m_held_mutex);
            for (size_t j = 0; j < m_once_latched.size(); j++)
                if (m_once_latched[j] == id) { m_once_latched.erase(m_once_latched.begin() + j); break; }
            if (press) m_once_latched.push_back(id);
            break;
        }

    // A latching key acts on the press alone. Toggling on the release too
    // cancels the press out - the same reason ScanKeyboard::key_up() only
    // releases the РУС/ЛАТ line instead of flipping the register back.
    switch (role) {
        case KEY_ROLE_SHIFT:
            set_shift_state(press);
            break;
        case KEY_ROLE_CTRL:
            set_ctrl_state(press);
            break;
        case KEY_ROLE_RUS_TOGGLE:
            if (press) set_rus(!rus_mode);
            break;
        case KEY_ROLE_RUS_ON:
            if (press) set_rus(true);
            break;
        case KEY_ROLE_RUS_OFF:
            if (press) set_rus(false);
            break;
        case KEY_ROLE_CASE_LOWER:
            if (press) m_case_lower = true;
            break;
        case KEY_ROLE_CASE_UPPER:
            if (press) m_case_lower = false;
            break;
        case KEY_ROLE_TOGGLE:
            if (press) set_toggled(id, !toggled(id));
            break;
        case KEY_ROLE_ALT:
            set_alt_state(press);
            break;
        case KEY_ROLE_STOP:
            i_stop.change(press ? 1 : 0);
            break;
        case KEY_ROLE_REPEAT:
            repeat_key(id, press);
            break;
        case KEY_ROLE_RESET:
            //The СБР of an Агат restarts the machine, which is exactly what
            //Break does in the GUI: Emulator::reset() is this same call.
            //Only together with УПР, as on the machine itself, so that a stray
            //click on the drawing does not throw the work away
            if (press && ctrl_state()) im->dm->reset_devices(false);
            break;
        default:
            break;
    }

    // A key may be both: on the БК РУС and ЛАТ switch the alphabet and send
    // their own codes ($0E and $0F). send_key_id() does nothing for an id the
    // table has no entry for, so the momentary modifiers fall through quietly.
    send_key_id(id, press);

    // Latching keys light up from the state they produced, handled in ids_held()
    if (!is_latching(id)) note_id(id, press);

    // The key a "once" modifier was latched for has been let go. Other
    // modifiers and the latches do not count: СУ+АР2+U is still one chord
    if (!press && (role == KEY_ROLE_NORMAL || role == KEY_ROLE_REPEAT || role == KEY_ROLE_STOP))
        release_once_modifiers();
}

emulator::Result Keyboard::parse_key_table(const std::vector<std::string> &body, const std::string &file)
{
    (void)body;
    return emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "This keyboard type has no native key table support")) + "} " + file);
}

// The native key table names the keys of the machine itself. Its header
// declares the modifiers; the rest of the file is laid out per keyboard type,
// so only the split is done here.
emulator::Result Keyboard::load_key_table(SystemData *sd)
{
    const std::string name = cd->get_parameter("keys", false).value;
    if (name.empty()) return emulator::Result::ok();

    const std::string file = find_file_location(sd, name);
    if (file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Key table file not found")) + "} " + name);

    const std::string content = dsk_tools::utf8_read_file(file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Error reading key table file")) + "} " + file);

    static const struct { const char * name; KeyRole role; } HEADER[] = {
        {"shift",     KEY_ROLE_SHIFT},
        {"ctrl",      KEY_ROLE_CTRL},
        {"rus",       KEY_ROLE_RUS_TOGGLE},
        {"rus-on",    KEY_ROLE_RUS_ON},
        {"rus-off",   KEY_ROLE_RUS_OFF},
        {"rus-line",  KEY_ROLE_RUS_LINE},
        {"case-upper", KEY_ROLE_CASE_UPPER},
        {"case-lower", KEY_ROLE_CASE_LOWER},
        {"alt",       KEY_ROLE_ALT},
        {"hold",      KEY_ROLE_MODIFIER},
        {"stop",      KEY_ROLE_STOP},
        {"repeat",    KEY_ROLE_REPEAT},
        {"reset",     KEY_ROLE_RESET},
        {"toggle",    KEY_ROLE_TOGGLE},
        {nullptr,     KEY_ROLE_NORMAL}
    };

    std::vector<std::string> body;
    std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t li = 0; li < lines.size(); li++)
    {
        std::string line = str_trim(lines[li]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;

        bool taken = false;
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string left = str_tolower(str_trim(line.substr(0, colon)));
            for (int h = 0; HEADER[h].name != nullptr; h++)
                if (left == HEADER[h].name) {
                    //The key name, then its flags: "alt: key_ar2 once"
                    std::string value = str_trim(line.substr(colon + 1));
                    for (size_t c = 0; c < value.size(); c++)
                        if (value[c] == '\t') value[c] = ' ';
                    std::vector<std::string> words = split_string(value, ' ', true);
                    const std::string id = words.empty() ? std::string() : str_trim(words[0]);
                    register_key_id(id, HEADER[h].role);
                    for (size_t w = 1; w < words.size(); w++) {
                        const std::string flag = str_tolower(str_trim(words[w]));
                        if (flag.empty()) continue;
                        //Only a modifier the drawing latches with a click can let go by itself
                        const bool clicked_on = HEADER[h].role == KEY_ROLE_SHIFT
                                             || HEADER[h].role == KEY_ROLE_CTRL
                                             || HEADER[h].role == KEY_ROLE_ALT
                                             || HEADER[h].role == KEY_ROLE_MODIFIER;
                        if (flag == "once" && clicked_on)
                            m_once_ids.push_back(id);
                        else
                            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                                "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Unknown flag of a modifier in the key table")) + "} " + line + " (" + file + ")");
                    }
                    taken = true;
                    break;
                }
        }
        if (!taken) body.push_back(line);
    }

    if (body.empty()) return emulator::Result::ok();
    return parse_key_table(body, file);
}

// A reset forgets what was held: the key a script or a drawing left pressed is
// gone, and so are the latches. Leaving a modifier behind is what made a cold
// restart look like a dead keyboard - every key kept arriving as a control
// code, while nothing showed as pressed any more.
void Keyboard::reset(bool cool)
{
    ComputerDevice::reset(cool);
    m_case_lower = false;
    m_reset_count++;
    {
        compat_lock_guard lock(m_host_mutex);
        m_host_down.clear();
        m_group_owner.assign(m_group_owner.size(), _FFFF);
        m_group_deferred.assign(m_group_deferred.size(), 0);
        m_group_blocked.clear();
    }
    compat_lock_guard lock(m_held_mutex);
    m_ids_held.clear();
    m_once_latched.clear();
}

void Keyboard::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.b("rus", rus_mode);
    w.b("case_lower", m_case_lower);
    w.n("reset_count", m_reset_count);
    {
        compat_lock_guard lock(m_held_mutex);
        std::string t;
        for (size_t i = 0; i < m_toggled.size(); i++) t += (i ? "|" : "") + m_toggled[i];
        if (!t.empty()) w.s("toggled", t);
    }
    //The lists of keys the host holds down are not written. Nobody is holding
    //a key when a snapshot is opened, and one that came back down would stay
    //down for good, auto repeat included
}

emulator::Result Keyboard::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    r.b("rus", rus_mode);
    r.b("case_lower", m_case_lower);
    r.u("reset_count", m_reset_count);
    //No key means no switch was on (save_state() leaves an empty string out):
    //keeping what reset() turned on would bring ЛИН and ДУП of the 15ИЭ back
    std::string t;
    r.s("toggled", t);
    {
        compat_lock_guard lock(m_held_mutex);
        m_toggled = split_string(t, '|', true);
    }
    toggles_changed();
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> Keyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"rus", "1 when the keyboard is in the Rus register", false});
    r.push_back({"pressed", "Ids of the machine keys currently held", false});
    r.push_back({"leds",    "Lamps of the drawing that are lit",      false});
    r.push_back({"blocked", "Host key presses swallowed by a key group", false});
    return r;
}

std::vector<DeviceCommandInfo> Keyboard::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"host", "key, 0|1", "Presses (1) or releases (0) a key as if typed on the host: remap and key groups apply"});
    return r;
}

emulator::Result Keyboard::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "host") {
        const std::vector<std::string> p = split_params(parameters);
        const unsigned int key = p.empty() ? _FFFF : translate_key_name(str_trim(p[0]));
        if (key == _FFFF || p.size() < 2)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Keyboard|" + std::string(QT_TRANSLATE_NOOP("Keyboard", "Command 'host' expects a key name and 0 or 1")) + "}");
        key_event(key, key, parse_numeric_value(p[1]) != 0, true);
        return emulator::Result::ok();
    }
    return ComputerDevice::send_command(command, parameters);
}

bool Keyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //Typing that comes out in the wrong alphabet is almost always this
    if (field == "rus")
    {
        out.numeric = true;
        out.values.push_back(rus_mode ? 1 : 0);
        return true;
    }

    if (field == "blocked")
    {
        compat_lock_guard lock(m_host_mutex);
        out.numeric = true;
        out.values.push_back(m_blocked_count);
        return true;
    }

    //Ids, not codes: this is the machine's own naming, the one the picture uses
    if (field == "pressed")
    {
        const std::vector<std::string> held = ids_held();
        out.numeric = false;
        for (size_t i = 0; i < held.size(); i++) {
            if (i > 0) out.text += ",";
            out.text += held[i];
        }
        return true;
    }

    //What the drawing shows, whether or not this machine's picture has the lamp
    if (field == "leds")
    {
        const std::vector<Indicator> ind = indicators();
        out.numeric = false;
        for (size_t i = 0; i < ind.size(); i++)
            if (ind[i].lit) {
                if (!out.text.empty()) out.text += ",";
                out.text += ind[i].id;
            }
        return true;
    }

    return ComputerDevice::get_field(field, from, to, out);
}