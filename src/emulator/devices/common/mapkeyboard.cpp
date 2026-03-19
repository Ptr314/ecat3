// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port-based keyboard device

#include <QException>

#include "emulator/utils.h"
#include "mapkeyboard.h"
#include "emulator/utils.h"

MapKeyboard::MapKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , shift_pressed(false)
    , ctrl_pressed(false)
    , code_ruslat(0)
    , ruslat_bit(1)
    , i_ruslat(this, im, 1, "ruslat", MODE_W)
    , i_ready(this, im, 1, "ready", MODE_W)
{
    m_rus_switches[0] = 0;
    m_rus_switches[1] = 0;
}

void MapKeyboard::load_config(SystemData *sd)
{
    Keyboard::load_config(sd);

    QString map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.isEmpty())
        QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Keyboard map file is expected"));
    else {
        QFile file(map_file);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Error reading map file %1").arg(map_file));
            return;
        }

        QTextStream in(&file);
        while (!in.atEnd())
        {
            QString line = in.readLine();
            if (!line.isEmpty()) {
                QStringList parts = line.split(':', skip_empty_parts);
                if (parts.length() != 2) {
                    QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Map file entry '%1' is incorrect").arg(line));
                    return;
                }
                QStringList left_parts = parts.at(0).split('/', skip_empty_parts);
                if (left_parts.length() < 1 || left_parts.length() > 2) {
                    QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Map file entry '%1' is incorrect").arg(line));
                    return;
                }

                QString key = left_parts.at(0).trimmed();
                QString modificators = (left_parts.length()>1)?left_parts.at(1).trimmed():"";
                QString value = parts.at(1).trimmed();
                key_map.push_back({
                    translate_key(key),
                    parse_numeric_value(value),
                    (modificators.indexOf('S') >= 0),
                    (modificators.indexOf('C') >= 0),
                    (modificators.indexOf('R') >= 0)
                });
            };
        };
    }
    port_value = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("port-value").value));

    QString s = cd->get_parameter("port-ruslat", false).value;
    if (!s.isEmpty()) {
        port_ruslat = dynamic_cast<Port*>(im->dm->get_device_by_name(s));
    } else
        port_ruslat = nullptr;

    QString rl = cd->get_parameter("ruslat", false).value;
    if (!rl.isEmpty()) {
        code_ruslat = translate_key(rl);
    }

    try {
        rus_value = read_confg_value(cd, "rus-on", false, (unsigned int)1);
    } catch (QException e) {
        QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("rus-on should be 0 or 1"));
    }

    try {
        ruslat_bit = read_confg_value(cd, "rus-bit", false, (unsigned int)0);
    } catch (QException e) {
        QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("rus-bit should be a number"));
    }

    const QString mode_str = cd->get_parameter("rusmode", false).value.toLower();
    if (mode_str.isEmpty() || mode_str == "pin") {
        m_use_pin = true;
        m_use_codes = false;
    } else if (mode_str == "both"){
        m_use_pin = true;
        m_use_codes = true;
    } else if (mode_str == "code"){
        m_use_pin = false;
        m_use_codes = true;
    } else
        QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Incorrect keyboard rusmode %1").arg(mode_str));

    QString rs = cd->get_parameter("rus_switches", false).value;
    if (!rs.isEmpty()) {
        QStringList rs_parts = rs.split('/', skip_empty_parts);
        if (rs_parts.length() == 2) {
            m_rus_switches[0] = parse_numeric_value(rs_parts.at(0).trimmed());
            m_rus_switches[1] = parse_numeric_value(rs_parts.at(1).trimmed());
            m_has_rus_switches = true;
        } else {
            QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("rus_switches should have two values separated by '/'"));
        }
    }

    i_ready.change(1);
}

void MapKeyboard::set_rus(bool new_rus)
{
    Keyboard::set_rus(new_rus);

    unsigned int ruslat_state = new_rus?rus_value:(rus_value ^ 1);
    if (port_ruslat != nullptr) {
        unsigned int port_value = (port_ruslat->get_value(0) & ~(1 << ruslat_bit)) | (ruslat_state  << ruslat_bit);
        port_ruslat->set_value(port_value, port_value); // Alow using both port & port-address
    }
    i_ruslat.change(ruslat_state);
}

void MapKeyboard::send_key(unsigned int value)
{
    port_value->set_value(value, value); // To use both port & port-address
    i_ready.change(0);
    i_ready.change(1);
}

void MapKeyboard::key_down(unsigned int key)
{
    if (key == Qt::Key_Control)
        ctrl_pressed = true;
    else if (key == Qt::Key_Shift)
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
        if (found_with_rus || found_no_rus) send_key(key_map[key_index].value);
    }
}

void MapKeyboard::key_up(unsigned int key)
{
    if (key == Qt::Key_Control)
        ctrl_pressed = false;
    else if (key == Qt::Key_Shift)
        shift_pressed = false;
}

void MapKeyboard::reset(bool cool)
{
    Keyboard::reset(cool);

    if (code_ruslat != 0)
        if (code_ruslat == Qt::Key_CapsLock)
            set_rus(checkCapsLock());
        else
            set_rus(false);
    else
        set_rus(false);
}

ComputerDevice * create_mapkeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new MapKeyboard(im, cd);
}
