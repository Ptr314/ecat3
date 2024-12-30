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
    , key_map_len(0)
    , i_ruslat(this, im, 1, "ruslat", MODE_W)
{
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
                QStringList parts = line.split(':', Qt::SkipEmptyParts);
                if (parts.length() != 2) {
                    QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Map file entry '%1' is incorrect").arg(line));
                    return;
                }
                QStringList left_parts = parts.at(0).split('/', Qt::SkipEmptyParts);
                if (left_parts.length() < 1 || left_parts.length() > 2) {
                    QMessageBox::critical(0, MapKeyboard::tr("Error"), MapKeyboard::tr("Map file entry '%1' is incorrect").arg(line));
                    return;
                }

                QString key = left_parts.at(0).trimmed();
                QString modificators = (left_parts.length()>1)?left_parts.at(1).trimmed():"";
                QString value = parts.at(1).trimmed();
                key_map[key_map_len++] = {
                    .key_code = translate_key(key),
                    .value = parse_numeric_value(value),
                    .shift = (modificators.indexOf('S') >= 0),
                    .ctrl = (modificators.indexOf('C') >= 0),
                };
            };
        };
    }
    port_value = dynamic_cast<Port*>(im->dm->get_device_by_name(cd->get_parameter("port-value").value));

    QString s = cd->get_parameter("port-ruslat", false).value;
    if (!s.isEmpty()) {
        port_ruslat = dynamic_cast<Port*>(im->dm->get_device_by_name(s));
        code_ruslat = translate_key(cd->get_parameter("ruslat").value);
    } else
        port_ruslat = nullptr;

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

void MapKeyboard::key_down(unsigned int key)
{
    if (key == Qt::Key_Control)
        ctrl_pressed = true;
    else if (key == Qt::Key_Shift)
        shift_pressed = true;
    else if (key == code_ruslat)
        set_rus(!rus_mode);
    else {
        for (unsigned int i=0; i<key_map_len; i++)
            if (
                       key_map[i].key_code == key
                    && key_map[i].ctrl     == ctrl_pressed
                    && key_map[i].shift    == shift_pressed
                )
            {
                port_value->set_value(key_map[i].value, key_map[i].value); // To use both port & port-address
            }
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
