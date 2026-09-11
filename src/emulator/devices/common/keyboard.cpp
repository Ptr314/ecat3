// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Abstract keyboard device

#include "keyboard.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"

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

    //Interfaces start at _FFFF, which a config that inverts the line would read
    //as the key being held down from the moment the machine starts
    i_stop.change(0);

    return load_key_table(sd);
}

void Keyboard::key_event(unsigned int key, unsigned int native_key, bool press)
{
    unsigned int k;
    if (known_key(key))
        k = key;
    else if (known_key(native_key))
        k = native_key;
    else return;
    if (press)
        key_down(rus_translate(k));
    else
        key_up(rus_translate(k));
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
            return true;
        default:
            return false;
    }
}

Keyboard::ClickMode Keyboard::click_mode(const std::string &id) const
{
    switch (key_role(id)) {
        case KEY_ROLE_SHIFT:
        case KEY_ROLE_CTRL:
        case KEY_ROLE_ALT:
            return CLICK_TOGGLE;
        case KEY_ROLE_RUS_TOGGLE:
        case KEY_ROLE_RUS_ON:
        case KEY_ROLE_RUS_OFF:
        case KEY_ROLE_CASE_UPPER:
        case KEY_ROLE_CASE_LOWER:
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
    return r;
}

void Keyboard::key_event_id(const std::string &id, bool press)
{
    const KeyRole role = key_role(id);

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
            //Break does in the GUI: Emulator::reset() is this same call
            if (press) im->dm->reset_devices(false);
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
        {"stop",      KEY_ROLE_STOP},
        {"repeat",    KEY_ROLE_REPEAT},
        {"reset",     KEY_ROLE_RESET},
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
                    register_key_id(str_trim(line.substr(colon + 1)), HEADER[h].role);
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
    compat_lock_guard lock(m_held_mutex);
    m_ids_held.clear();
}

std::vector<DeviceFieldInfo> Keyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"rus", "1 when the keyboard is in the Rus register", false});
    r.push_back({"pressed", "Ids of the machine keys currently held", false});
    return r;
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

    return ComputerDevice::get_field(field, from, to, out);
}