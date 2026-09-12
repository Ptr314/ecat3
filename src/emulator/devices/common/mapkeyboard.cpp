// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port-based keyboard device

#include "emulator/utils.h"
#include "mapkeyboard.h"
#include "dsk_tools/dsk_tools.h"

MapKeyboard::MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , shift_pressed(false)
    , ctrl_pressed(false)
    , code_ruslat(0)
    , ruslat_bit(1)
    , i_ruslat(this, im, 1, "ruslat", MODE_W)
    , i_ready(this, im, 1, "ready", MODE_W)
    , i_pressed(this, im, 1, "pressed", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
{
    m_rus_switches[0] = 0;
    m_rus_switches[1] = 0;
}

// One line of either table: "name[/modifiers]: value". The grammar is shared,
// only the left hand side means a different thing in each: a host key name in
// the .map file, a key of the machine itself in the native table.
//
// Every rule below is here because the lax version passed a typo on in
// silence. split_string() drops empty tokens, so "/S" used to parse as the key
// "S" with no modifiers, and "A//S" as "A" - a line that looks like Shift and
// is not. A modifier letter no one knows ("B/X") counted as no modifier at
// all, and the entry stayed in the table sending the wrong code. The caller
// says which letters it knows: a map file has S, C and R, the native table
// adds the case latch L. Case is not part of the spelling ("up/r" means what
// it looks like), but an unknown or repeated letter is an error.
static bool parse_map_line(const std::string &line, const std::string &known_mods,
                           std::string &name, std::string &mods, std::string &value)
{
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return false;

    value = str_trim(line.substr(colon + 1));
    if (value.empty() || value.find(':') != std::string::npos) return false;

    const std::string left = str_trim(line.substr(0, colon));
    const size_t slash = left.find('/');
    name = str_trim(left.substr(0, (slash == std::string::npos) ? left.size() : slash));
    if (name.empty()) return false;

    mods.clear();
    if (slash == std::string::npos) return true;

    const std::string tail = str_toupper(str_trim(left.substr(slash + 1)));
    if (tail.empty() || tail.find('/') != std::string::npos) return false;
    for (size_t i = 0; i < tail.size(); i++)
    {
        if (known_mods.find(tail[i]) == std::string::npos) return false;
        if (mods.find(tail[i]) != std::string::npos) return false;
        mods += tail[i];
    }
    return true;
}

emulator::Result MapKeyboard::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Keyboard map file is expected")) + "}");
    else {
        std::string content = dsk_tools::utf8_read_file(map_file);
        if (content.empty()) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Error reading map file")) + "} " + map_file);
        }

        //The source line of every entry, kept only while the file is read: it
        //is what a duplicate is reported against
        std::vector<std::string> key_lines;

        std::vector<std::string> lines = split_string(content, '\n', true);
        for (size_t li = 0; li < lines.size(); li++)
        {
            //Comments are cut the way the native key table cuts them: the two
            //files share a grammar, and a map that cannot explain itself
            //invites the next reader to "fix" a code back. No entry is lost to
            //this: the only name "//" could collide with is the slash key
            //spelled "/", and a name cannot be written that way here - the
            //part before the "/" would be empty, which parse_map_line()
            //refuses. A map spells it "slash", as the key table does.
            std::string line = str_trim(lines[li]);
            const size_t comment = line.find("//");
            if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
            if (!line.empty()) {
                std::string key, modificators, value;
                if (!parse_map_line(line, "SCR", key, modificators, value)) {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Map file entry is incorrect")) + "} " + line);
                }

                //A name the key table does not know reached the machine as
                //_FFFF, a code no key ever sends: the line was in the file, in
                //the table, and dead. The joystick already refused such a line
                const unsigned int key_code = translate_key(key);
                if (key_code == _FFFF)
                    return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Unknown key in the map file")) + "} " + line);

                //parse_numeric_value() throws, and nothing on the way out of a
                //config load catches it: one mistyped digit used to end the
                //whole emulator with "terminate called", naming neither the
                //file nor the line
                unsigned int code;
                try {
                    code = parse_numeric_value(value);
                } catch (const std::exception &) {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Invalid value in the map file")) + "} " + line);
                }

                const bool shift = (modificators.find('S') != std::string::npos);
                const bool ctrl  = (modificators.find('C') != std::string::npos);
                const bool rus   = (modificators.find('R') != std::string::npos);

                //The first entry of a pair wins, so the second one is a line the
                //author believes in and the machine never reads. The line it
                //collides with is named too: the table matches by key code, and
                //a name has synonyms - "minus" and "-" are one key, so are
                //"enter" and "ret2" - so the two lines need not look alike
                for (size_t i = 0; i < key_map.size(); i++)
                    if (key_map[i].key_code == key_code && key_map[i].shift == shift
                        && key_map[i].ctrl == ctrl && key_map[i].rus == rus)
                        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Duplicate entry in the map file")) + "} " + line + " (== " + key_lines[i] + ")");

                key_map.push_back({key_code, code, shift, ctrl, rus});
                key_lines.push_back(line);
            }
        }
    }
    port_value = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("port-value").value));

    std::string s = cd->get_parameter("port-ruslat", false).value;
    if (!s.empty()) {
        port_ruslat = dynamic_cast<Port*>(im->dm->get_device_by_name(s));
    } else
        port_ruslat = nullptr;

    std::string rl = cd->get_parameter("ruslat", false).value;
    if (!rl.empty()) {
        code_ruslat = translate_key(rl);
    }

    try {
        rus_value = read_confg_value(cd, "rus-on", false, (unsigned int)1);
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "rus-on should be 0 or 1")) + "}");
    }

    try {
        ruslat_bit = read_confg_value(cd, "rus-bit", false, (unsigned int)0);
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "rus-bit should be a number")) + "}");
    }

    const std::string mode_str = str_tolower(cd->get_parameter("rusmode", false).value);
    if (mode_str.empty() || mode_str == "pin") {
        m_use_pin = true;
        m_use_codes = false;
    } else if (mode_str == "both"){
        m_use_pin = true;
        m_use_codes = true;
    } else if (mode_str == "code"){
        m_use_pin = false;
        m_use_codes = true;
    } else
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Incorrect keyboard rusmode")) + "} " + mode_str);

    std::string rs = cd->get_parameter("rus_switches", false).value;
    if (!rs.empty()) {
        std::vector<std::string> rs_parts = split_string(rs, '/', true);
        if (rs_parts.size() == 2) {
            m_rus_switches[0] = parse_numeric_value(str_trim(rs_parts[0]));
            m_rus_switches[1] = parse_numeric_value(str_trim(rs_parts[1]));
            m_has_rus_switches = true;
        } else {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "rus_switches should have two values separated by '/'")) + "}");
        }
    }

    m_vector = read_confg_value(cd, "vector", false, _FFFF);
    if (m_vector != _FFFF) {
        m_alt_vector = read_confg_value(cd, "alt-vector", false, m_vector);
        std::vector<std::string> codes = split_string(cd->get_parameter("alt-codes", false).value, ',', true);
        for (size_t i = 0; i < codes.size(); i++) {
            const std::string c = str_trim(codes[i]);
            if (!c.empty()) m_alt_codes.push_back(parse_numeric_value(c));
        }
        i_vector.change(m_vector);
    }

    i_ready.change(1);

    //Both tables exist by now, so the two namings can be matched up
    build_code_to_id();

    return emulator::Result::ok();
}

void MapKeyboard::set_rus(bool new_rus)
{
    Keyboard::set_rus(new_rus);

    unsigned int ruslat_state = new_rus?rus_value:(rus_value ^ 1);
    if (port_ruslat != nullptr) {
        //Reading the port to modify one bit of it must not pulse its access
        //line: that is a strobe the CPU produces, not the keyboard
        unsigned int port_value = (port_ruslat->get_direct(0) & ~(1 << ruslat_bit)) | (ruslat_state  << ruslat_bit);
        port_ruslat->set_value_word(port_value, port_value); // Alow using both port & port-address
    }
    i_ruslat.change(ruslat_state);
}

// The controller routes a code through the second vector when АР2 is held, when
// the code is one it always sends that way (alt-codes), or when the table marks
// the key with bit 7: a code of the controller has seven bits, so the eighth is
// free to say "second vector" for a key wired as a chord, like ГРАФ on the БК0010.
bool MapKeyboard::alt_vector_for(unsigned int code, bool alt) const
{
    if (alt || code > 0x7F) return true;
    for (size_t i = 0; i < m_alt_codes.size(); i++)
        if (m_alt_codes[i] == code) return true;
    return false;
}

void MapKeyboard::send_key(unsigned int value, bool alt)
{
    m_last_value = value;
    m_last_alt = alt;
    if (m_vector != _FFFF) {
        //The processor takes the vector when the request goes up, so it has to
        //be on the line before the ready pulse below
        i_vector.change(alt_vector_for(value, alt) ? m_alt_vector : m_vector);
        value &= 0x7F;
    }
    port_value->set_value_word(value, value); // To use both port & port-address
    i_ready.change(0);
    i_ready.change(1);
}

// ПОВТ does not carry a code of its own: it makes the keyboard repeat the last
// one. Sending it again and keeping the "a key is held" line up is enough -
// the БК monitor's own auto repeat takes it from there.
void MapKeyboard::repeat_key(const std::string &id, bool press)
{
    if (press) {
        if (m_last_value == _FFFF) return;
        bool known = false;
        for (size_t i = 0; i < ids_down.size(); i++)
            if (ids_down[i] == id) { known = true; break; }
        if (!known) {
            ids_down.push_back(id);
            update_pressed();
        }
        send_key(m_last_value, m_last_alt);
    } else {
        for (size_t i = 0; i < ids_down.size(); i++)
            if (ids_down[i] == id) {
                ids_down.erase(ids_down.begin() + i);
                update_pressed();
                break;
            }
    }
}

// Some machines have a line telling whether any key is held at the moment,
// separate from the code of the last key pressed. The БК firmware uses it for
// the auto repeat, so a key that is never seen as held is dropped again right
// after it has been read.
void MapKeyboard::update_pressed()
{
    i_pressed.change((keys_held.empty() && ids_down.empty())? 0 : 1);
}

void MapKeyboard::key_down(unsigned int key)
{
    bool known = false;
    for (size_t i = 0; i < keys_held.size(); i++)
        if (keys_held[i] == key) { known = true; break; }
    if (!known) {
        keys_held.push_back(key);
        update_pressed();
    }

    //So that typing on the real keyboard lights the drawing up as well
    note_id(id_of_code(key), true);

    if (key == EmuKey::Control)
        ctrl_pressed = true;
    else if (key == EmuKey::Shift)
        shift_pressed = true;
    else if (key == code_ruslat) {
        set_rus(!rus_mode);
        if (m_use_codes && m_has_rus_switches) {
            send_key(m_rus_switches[rus_mode?0:1]);
        }
    }else {
        bool found_with_rus = false;
        bool found_no_rus = false;
        unsigned key_index = 0;
        if (m_use_codes)
            for (size_t i=0; i<key_map.size(); i++)
                if (       key_map[i].key_code == key
                        && key_map[i].ctrl     == ctrl_pressed
                        && key_map[i].shift    == shift_pressed
                        && (key_map[i].rus == rus_mode || !m_use_codes)
                    )
                {
                    key_index = i;
                    found_with_rus = true;
                    break;
                }
        if (!found_with_rus) {
            for (size_t i=0; i<key_map.size(); i++)
                if (       key_map[i].key_code == key
                        && key_map[i].ctrl     == ctrl_pressed
                        && key_map[i].shift    == shift_pressed
                    )
                {
                    key_index = i;
                    found_no_rus = true;
                    break;
                }
        }
        //АР2 latched on the drawing applies to the host keyboard as well
        if (found_with_rus || found_no_rus) send_key(key_map[key_index].value, alt_pressed);
    }
}

// A symbol of the upper register has a map entry with Shift and none without
// it, so pressing the key alone gives nothing. Letters have both entries and
// are typed as they are.
bool MapKeyboard::needs_shift(unsigned int key)
{
    bool plain = false, shifted = false;
    for (size_t i = 0; i < key_map.size(); i++)
        if (key_map[i].key_code == key && !key_map[i].ctrl) {
            if (key_map[i].shift) shifted = true; else plain = true;
        }
    return shifted && !plain;
}

void MapKeyboard::key_up(unsigned int key)
{
    for (size_t i = 0; i < keys_held.size(); i++)
        if (keys_held[i] == key) {
            keys_held.erase(keys_held.begin() + i);
            update_pressed();
            break;
        }

    if (key == EmuKey::Control)
        ctrl_pressed = false;
    else if (key == EmuKey::Shift)
        shift_pressed = false;

    note_id(id_of_code(key), false);
}

emulator::Result MapKeyboard::parse_key_table(const std::vector<std::string> &body, const std::string &file)
{
    for (size_t i = 0; i < body.size(); i++)
    {
        std::string name, mods, value;
        if (!parse_map_line(body[i], "SCRL", name, mods, value))
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Key table entry is incorrect")) + "} " + body[i] + " (" + file + ")");

        //A header line spelled wrong ("shft: key_shift") falls through to here,
        //where the id would be "shft" and the value a key name: without this
        //the value threw out of the whole load instead of naming the line
        unsigned int code;
        try {
            code = parse_numeric_value(value);
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Invalid value in the key table")) + "} " + body[i] + " (" + file + ")");
        }

        const bool shift = (mods.find('S') != std::string::npos);
        const bool ctrl  = (mods.find('C') != std::string::npos);
        const bool rus   = (mods.find('R') != std::string::npos);
        const bool lcase = (mods.find('L') != std::string::npos);

        //find_id_entry() takes the first match, so a second line for the same
        //key and the same modifiers is one the drawing will never send
        for (size_t j = 0; j < id_map.size(); j++)
            if (id_map[j].id == name && id_map[j].shift == shift && id_map[j].ctrl == ctrl
                && id_map[j].rus == rus && id_map[j].lcase == lcase)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{MapKeyboard|" + std::string(QT_TRANSLATE_NOOP("MapKeyboard", "Duplicate entry in the key table")) + "} " + body[i] + " (" + file + ")");

        id_map.push_back({name, code, shift, ctrl, rus, lcase});
        register_key_id(name);
    }
    return emulator::Result::ok();
}

// An entry matching the Rus register wins, otherwise the register is ignored -
// that is how a machine with no separate Rus entries, like the БК, keeps working.
int MapKeyboard::find_id_entry(const std::string &id, bool shift, bool lcase, bool match_rus) const
{
    for (size_t i = 0; i < id_map.size(); i++)
        if (id_map[i].id == id
            && id_map[i].ctrl  == ctrl_pressed
            && id_map[i].shift == shift
            && id_map[i].lcase == lcase
            && (!match_rus || id_map[i].rus == rus_mode))
            return int(i);
    return -1;
}

void MapKeyboard::send_key_id(const std::string &id, bool press)
{
    if (!press) {
        for (size_t i = 0; i < ids_down.size(); i++)
            if (ids_down[i] == id) {
                ids_down.erase(ids_down.begin() + i);
                update_pressed();
                break;
            }
        return;
    }

    //The momentary shift wins over the latch: it is the key the user is
    //holding right now. The latch applies only where the table declares a /L
    //entry, so a key without one keeps sending what it always sends.
    const bool want_shift = shift_pressed;
    const bool want_lcase = !shift_pressed && case_shift();

    int found = find_id_entry(id, want_shift, want_lcase, true);
    if (found < 0) found = find_id_entry(id, want_shift, want_lcase, false);
    if (found < 0 && want_lcase) {
        //No letter-case form: this key is not one the latch touches
        found = find_id_entry(id, want_shift, false, true);
        if (found < 0) found = find_id_entry(id, want_shift, false, false);
    }
    if (found < 0) return;

    bool known = false;
    for (size_t i = 0; i < ids_down.size(); i++)
        if (ids_down[i] == id) { known = true; break; }
    if (!known) {
        ids_down.push_back(id);
        update_pressed();
    }

    //АР2 does not have entries of its own: the code stays the same and goes
    //through the second vector, where the БК firmware makes its graphics and
    //screen-control codes out of it
    send_key(id_map[found].value, alt_pressed);
}

// Two entries that send the same byte under the same modifiers describe the
// same key of the machine, which is all the highlight needs to follow typing
// on the real keyboard. Aliases (ret and ret2 on the БК) map to the same id.
void MapKeyboard::build_code_to_id()
{
    for (size_t i = 0; i < key_map.size(); i++)
        for (size_t j = 0; j < id_map.size(); j++)
            if (key_map[i].value == id_map[j].value
                && key_map[i].shift == id_map[j].shift
                && key_map[i].ctrl  == id_map[j].ctrl
                && key_map[i].rus   == id_map[j].rus
                && !id_map[j].lcase)
            {
                code_to_id.push_back(std::make_pair(key_map[i].key_code, id_map[j].id));
                break;
            }
}

std::string MapKeyboard::id_of_code(unsigned int code) const
{
    for (size_t i = 0; i < code_to_id.size(); i++)
        if (code_to_id[i].first == code) return code_to_id[i].second;
    return "";
}

void MapKeyboard::reset(bool cool)
{
    Keyboard::reset(cool);

    keys_held.clear();
    ids_down.clear();
    //The modifiers go with them: a key held across the reset is forgotten, and
    //a modifier that survived would silently rewrite everything typed after
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    m_last_value = _FFFF;
    m_last_alt = false;
    if (m_vector != _FFFF) i_vector.change(m_vector);
    update_pressed();

    if (code_ruslat != 0)
        if (code_ruslat == EmuKey::CapsLock)
            set_rus(checkCapsLock());
        else
            set_rus(false);
    else
        set_rus(false);
}

std::vector<DeviceFieldInfo> MapKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = Keyboard::get_device_fields();
    r.push_back({"shift", "1 while Shift is held",                     false});
    r.push_back({"ctrl",  "1 while Ctrl is held",                      false});
    r.push_back({"held",  "Codes of the keys currently down",          false});
    r.push_back({"count", "How many keys are currently down",          false});
    if (m_vector != _FFFF)
        r.push_back({"vector", "Interrupt vector of the last code",    false});
    return r;
}

bool MapKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "vector" && m_vector != _FFFF)
    {
        out.numeric = true;
        out.values.push_back(i_vector.value);
        return true;
    }

    if (field == "shift" || field == "ctrl")
    {
        out.numeric = true;
        out.values.push_back(((field == "shift") ? shift_pressed : ctrl_pressed) ? 1 : 0);
        return true;
    }

    //A key that a script pressed and never released is invisible otherwise,
    //and it changes everything typed afterwards
    if (field == "held")
    {
        out.numeric = true;
        //The codes match Qt::Key, so the modifiers live above bit 16 and a
        //narrower width would quietly cut them off
        out.width = 32;
        for (size_t i = 0; i < keys_held.size(); i++) out.values.push_back(keys_held[i]);
        if (keys_held.empty()) out.values.push_back(0);
        return true;
    }

    if (field == "count")
    {
        out.numeric = true;
        out.values.push_back(static_cast<unsigned int>(keys_held.size()));
        return true;
    }

    return Keyboard::get_field(field, from, to, out);
}

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new MapKeyboard(im, cd);
}
