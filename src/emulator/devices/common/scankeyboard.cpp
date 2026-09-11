// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scanning matrix-based keyboard device

#include <cstring>

#include "emulator/utils.h"
#include "emulator/config.h"
#include "scankeyboard.h"
#include "dsk_tools/dsk_tools.h"

#define SCAN_CALLBACK 1
#define LED_CALLBACK 2

//Both tables of this keyboard are laid out as the matrix: one line per output
//line, one whitespace separated column per scan line
static std::vector<std::string> split_columns(const std::string &line)
{
    std::vector<std::string> parts;
    std::string token;
    for (size_t ci = 0; ci < line.size(); ci++) {
        if (line[ci] == ' ' || line[ci] == '\t' || line[ci] == '\r') {
            if (!token.empty()) { parts.push_back(token); token.clear(); }
        } else {
            token += line[ci];
        }
    }
    if (!token.empty()) parts.push_back(token);
    return parts;
}

ScanKeyboard::ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , i_scan(this, im, 8, "scan", MODE_R, SCAN_CALLBACK)
    , i_output(this, im, 8, "output", MODE_W)
    , i_shift(this, im, 1, "shift", MODE_W)
    , i_ctrl(this, im, 1, "ctrl", MODE_W)
    , i_ruslat(this, im, 1, "ruslat", MODE_W)
    , i_ruslat_led(this, im, 1, "ruslat_led", MODE_R, LED_CALLBACK)

{
    memset(&key_array, _FFFF, sizeof(key_array));
}

emulator::Result ScanKeyboard::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{ScanKeyboard|" + std::string(QT_TRANSLATE_NOOP("ScanKeyboard", "Keyboard map file is expected")) + "}");
    else {
        std::string layout = dsk_tools::utf8_read_file(map_file);
        if (layout.empty()) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{ScanKeyboard|" + std::string(QT_TRANSLATE_NOOP("ScanKeyboard", "Error reading map file")) + "} " + map_file);
        }

        std::vector<std::string> lines = split_string(layout, '\n', true);
        out_lines = lines.size();
        for (unsigned int out = 0; out < out_lines; out++)
        {
            std::string line = str_trim(lines[out]);
            if (line.empty()) continue;
            std::vector<std::string> parts = split_columns(line);
            scan_lines = parts.size();
            for (unsigned int scan=0; scan<scan_lines; scan++)
            {
                std::vector<std::string> keys = split_string(parts[scan], '|', true);
                for (size_t i=0; i< keys.size(); i++)
                    if (keys[i] != "__")
                    {
                        int shift_state = SHIFT_STATE_KEEP;
                        std::string key_name;
                        size_t key_len = keys[i].length();
                        if (key_len > 1 && keys[i].back() == '_') {
                            key_name = keys[i].substr(0, key_len-1);
                            shift_state = SHIFT_STATE_OFF;
                        } else
                        if (key_len > 1 && keys[i].back() == '^') {
                            key_name = keys[i].substr(0, key_len-1);
                            shift_state = SHIFT_STATE_ON;
                        } else
                            key_name = keys[i];
                        unsigned int key_code = translate_key(key_name);
                        if (key_code == _FFFF)
                            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{ScanKeyboard|" + std::string(QT_TRANSLATE_NOOP("ScanKeyboard", "Unknown key")) + "} " + keys[i]);
                        else
                            scan_data.push_back({key_code, scan, out, shift_state});
                    }
            }
        }
    }

    //The native table was read before the map (by Keyboard::load_config), so
    //only now can it be checked against the matrix. A key outside it would be
    //dead on the drawing, and one shifted by a row would close another contact
    for (size_t i = 0; i < id_data.size(); i++)
        if (id_data[i].scan_line >= scan_lines || id_data[i].out_line >= out_lines)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{ScanKeyboard|" + std::string(QT_TRANSLATE_NOOP("ScanKeyboard", "Key table does not fit the matrix")) + "} " + id_data[i].id);

    code_ctrl = translate_key(cd->get_parameter("ctrl").value);
    code_shift = translate_key(cd->get_parameter("shift").value);
    code_ruslat = translate_key(cd->get_parameter("ruslat").value);

    i_shift.change(1);
    i_ctrl.change(1);
    i_ruslat.change(1);

    return emulator::Result::ok();
}

// The native table has the layout of the map file, with the machine's key names
// in place of the host's. Its position is all a key of the drawing needs: it
// closes that contact of the matrix and nothing else - no forced Shift like the
// map file entries have, the drawing has a Shift of its own. Blank lines and
// comments are gone by now, so the lines of the body are the output lines.
emulator::Result ScanKeyboard::parse_key_table(const std::vector<std::string> &body, const std::string &file)
{
    const unsigned int max_scan = sizeof(key_array) / sizeof(key_array[0]);
    const unsigned int max_out = sizeof(key_array[0]) * 8;
    for (unsigned int out = 0; out < body.size(); out++)
    {
        const std::vector<std::string> parts = split_columns(body[out]);
        if (out >= max_out || parts.size() > max_scan)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{ScanKeyboard|" + std::string(QT_TRANSLATE_NOOP("ScanKeyboard", "Key table does not fit the matrix")) + "} " + body[out] + " (" + file + ")");
        for (unsigned int scan = 0; scan < parts.size(); scan++)
        {
            if (parts[scan] == "__") continue;
            id_data.push_back({parts[scan], scan, out});
            register_key_id(parts[scan]);
        }
    }
    return emulator::Result::ok();
}

std::string ScanKeyboard::id_at(unsigned int scan, unsigned int out) const
{
    for (size_t i = 0; i < id_data.size(); i++)
        if (id_data[i].scan_line == scan && id_data[i].out_line == out) return id_data[i].id;
    return "";
}

std::string ScanKeyboard::role_id(KeyRole role) const
{
    for (size_t i = 0; i < m_key_roles.size(); i++)
        if (m_key_roles[i].second == role) return m_key_roles[i].first;
    return "";
}

void ScanKeyboard::send_key_id(const std::string &id, bool press)
{
    //РУС/ЛАТ is not in the matrix: it has a line of its own, which the firmware
    //polls, flipping its own flag and the indicator. So the key is held for as
    //long as the pointer holds it - a press and release in one instant would
    //pass by between two polls unseen - and the register is left to the machine
    if (key_role(id) == KEY_ROLE_RUS_LINE) {
        i_ruslat.change(press ? 0 : 1);
        return;
    }

    for (size_t i = 0; i < id_data.size(); i++)
        if (id_data[i].id == id) {
            const unsigned int mask = create_mask(1, id_data[i].out_line);
            if (press)
                key_array[id_data[i].scan_line] &= ~mask;
            else
                key_array[id_data[i].scan_line] |= mask;
            calculate_out();
            return;
        }
}

//stored_shift follows every decision about Shift: a host key with a forced
//Shift restores it on release, and must not bring back a state the drawing
//has changed since
void ScanKeyboard::set_shift_state(bool pressed)
{
    stored_shift = pressed ? 0 : 1;
    i_shift.change(stored_shift);
}

void ScanKeyboard::set_ctrl_state(bool pressed)
{
    i_ctrl.change(pressed ? 0 : 1);
}

void ScanKeyboard::key_down(unsigned int key)
{
    //qDebug() << "DOWN" << Qt::hex << key;
    if (key == code_ctrl) {
        i_ctrl.change(0);
        note_id(role_id(KEY_ROLE_CTRL), true);
    } else if (key == code_shift) {
        stored_shift = 0;
        i_shift.change(0);
        note_id(role_id(KEY_ROLE_SHIFT), true);
    } else if (key == code_ruslat) {
        i_ruslat.change(0);
        set_rus(!rus_mode);
        note_id(role_id(KEY_ROLE_RUS_LINE), true);
    } else {
        for (size_t i=0; i<scan_data.size(); i++)
            if (scan_data[i].key_code == key)
            {
                //qDebug() << "SCAN INDEX" << i;
                if (scan_data[i].shift_state != SHIFT_STATE_KEEP) {
                    stored_shift = i_shift.value;
                    i_shift.change(scan_data[i].shift_state==SHIFT_STATE_ON?0:1);
                    // qDebug() << "SHIFT " << ((scan_data[i].shift_state==SHIFT_STATE_ON)?0:1);
                }

                unsigned int l = scan_data[i].scan_line;
                key_array[l] &= ~create_mask(1, scan_data[i].out_line);
                calculate_out();
                //qDebug() << l << Qt::hex << key_array[l];

                //So that typing on the real keyboard lights the drawing up as well
                note_id(id_at(l, scan_data[i].out_line), true);
            }
    }
}

void ScanKeyboard::key_up(unsigned int key)
{
    //qDebug() << "UP" << key;
    if (key == code_ctrl) {
        i_ctrl.change(1);
        note_id(role_id(KEY_ROLE_CTRL), false);
    } else if (key == code_shift) {
        stored_shift = 1;
        i_shift.change(1);
        note_id(role_id(KEY_ROLE_SHIFT), false);
    } else if (key == code_ruslat) {
        //Only the press toggles the register. Toggling on the release too
        //cancels the press out, and what the machine is left with is whatever
        //its own indicator line happened to say - which is why the Орион
        //configs used to invert ruslat_led to get the right letters out
        i_ruslat.change(1);
        note_id(role_id(KEY_ROLE_RUS_LINE), false);
    } else {
        for (size_t i=0; i<scan_data.size(); i++)
            if (scan_data[i].key_code == key)
            {
                unsigned int l = scan_data[i].scan_line;
                key_array[l] |= create_mask(1, scan_data[i].out_line);
                calculate_out();

                if (scan_data[i].shift_state != SHIFT_STATE_KEEP) {
                    i_shift.change(stored_shift);
                    // qDebug() << "SHIFT " << stored_shift;

                }

                note_id(id_at(l, scan_data[i].out_line), false);
            }
    }
}

// A reset forgets what was held, as it does for the other keyboard type. The
// drawing drops its latches on a reset without releasing them, so a Shift or a
// Control it had engaged would otherwise stay down in the matrix while the
// picture shows it free.
void ScanKeyboard::reset(bool cool)
{
    Keyboard::reset(cool);
    memset(&key_array, _FFFF, sizeof(key_array));
    stored_shift = 1;
    i_shift.change(1);
    i_ctrl.change(1);
    i_ruslat.change(1);
    calculate_out();
}

void ScanKeyboard::calculate_out()
{
    unsigned int new_value = _FFFF;
    for (unsigned int i = 0; i<scan_lines; i++)
    {
        unsigned int mask = create_mask(1, i);
        if ((i_scan.value & mask) == 0)
            new_value &= key_array[i];
    }
    i_output.change(new_value);
}

// The indicator is taken into the register when the machine scans the matrix,
// not the moment the line moves. The Радио-86РК and Апогей ROMs write all of
// port C with zero on every keyboard poll and set the indicator bit back from
// their own flag right after ($FE7F-$FE89 in the Апогей ROM): the lamp is dark
// for some 20 us, which nobody sees on a real one. Followed instantly, that
// dip lights the drawing's lamp at random and, when a host key lands inside
// it, remaps the key into the other alphabet. The scan write comes before the
// dip and after the restore, so what it sees is the ROM's flag.
//
// Only a change of the line counts: a РУС/ЛАТ typed on the host keyboard flips
// the register at once (key_down), and a scan that comes before the ROM has
// polled that key must not flip it back.
void ScanKeyboard::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == SCAN_CALLBACK) {
        calculate_out();
        if (led_line >= 0 && led_line != led_taken) {
            led_taken = led_line;
            set_rus(led_taken == 1);
        }
    } else
        // LED_CALLBACK
        led_line = (int)(i_ruslat_led.value & 1);
}

void ScanKeyboard::set_rus(bool new_rus)
{
    //qDebug() << "RUS" << new_rus;
    Keyboard::set_rus(new_rus);
}

std::vector<DeviceFieldInfo> ScanKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = Keyboard::get_device_fields();
    r.push_back({"scan",   "Value driven onto the scan lines",           false});
    r.push_back({"out",    "Value the matrix answers with",              false});
    r.push_back({"shift",  "1 while Shift is held",                      false});
    r.push_back({"ctrl",   "1 while Ctrl is held",                       false});
    r.push_back({"ruslat", "State of the Rus/Lat line",                  false});
    r.push_back({"matrix", "The key matrix, one value per scan line. A bit is 0 while its key is down", true});
    r.push_back({"count",  "How many keys of the matrix are down",       false});
    return r;
}

bool ScanKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "scan" || field == "out" || field == "shift" || field == "ctrl" || field == "ruslat")
    {
        out.numeric = true;
        if (field == "scan")        out.values.push_back(i_scan.value);
        else if (field == "out")    out.values.push_back(i_output.value);
        else if (field == "shift")  out.values.push_back(i_shift.value);
        else if (field == "ctrl")   out.values.push_back(i_ctrl.value);
        else                        out.values.push_back(i_ruslat.value);
        return true;
    }

    //The matrix is what a key that does not arrive, or one that never goes up,
    //actually looks like from the machine side
    if (field == "matrix" || field == "count")
    {
        const unsigned int lines = (scan_lines < 15) ? scan_lines : 15;

        if (field == "count")
        {
            //The matrix is active low: a key pulls its bit to zero, and only
            //the columns the machine actually has are wired
            unsigned int n = 0;
            for (unsigned int i = 0; i < lines; i++)
                for (unsigned int b = 0; b < out_lines && b < 16; b++)
                    if (((key_array[i] >> b) & 1) == 0) n++;
            out.numeric = true;
            out.values.push_back(n);
            return true;
        }

        if (to >= lines) to = (lines > 0) ? lines - 1 : 0;
        if (from > to) from = to;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        for (unsigned int i = from; i <= to; i++) out.values.push_back(key_array[i]);
        return true;
    }

    return Keyboard::get_field(field, from, to, out);
}

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new ScanKeyboard(im, cd);
}
