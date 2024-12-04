#include <QRegularExpression>

#include "emulator/utils.h"
#include "scankeyboard.h"

#define SCAN_CALLBACK 1
#define LED_CALLBACK 2


ScanKeyboard::ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , keys_count(0)
    , i_scan(this, im, 8, "scan", MODE_R, SCAN_CALLBACK)
    , i_output(this, im, 8, "output", MODE_W)
    , i_shift(this, im, 1, "shift", MODE_W)
    , i_ctrl(this, im, 1, "ctrl", MODE_W)
    , i_ruslat(this, im, 1, "ruslat", MODE_W)
    , i_ruslat_led(this, im, 1, "ruslat_led", MODE_R, LED_CALLBACK)

{
    memset(&key_array, _FFFF, sizeof(key_array));
}

void ScanKeyboard::load_config(SystemData *sd)
{
    Keyboard::load_config(sd);

    static QRegularExpression re_crlf("[\r\n]");
    static QRegularExpression re_space("[\t ]");

    QString map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.isEmpty())
        QMessageBox::critical(0, ScanKeyboard::tr("Error"), ScanKeyboard::tr("Keyboard map file is expected"));
    else {
        QFile file(map_file);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(0, ScanKeyboard::tr("Error"), ScanKeyboard::tr("Error reading map file %1").arg(map_file));
            return;
        }
        QTextStream in(&file);
        QString layout = in.readAll();

        QStringList lines = layout.split(re_crlf, Qt::SkipEmptyParts);
        out_lines = lines.size();
        for (unsigned int out = 0; out < out_lines; out++)
        {
            QString line = lines[out].trimmed();
            QStringList parts = line.split(re_space, Qt::SkipEmptyParts);
            scan_lines = parts.size();
            for (unsigned int scan=0; scan<scan_lines; scan++)
            {
                QStringList keys = parts[scan].split("|", Qt::SkipEmptyParts);
                for (unsigned int i=0; i< keys.size(); i++)
                    if (keys[i] != "__")
                    {
                        int shift_state = SHIFT_STATE_KEEP;
                        QString key_name;
                        int key_len = keys[i].length();
                        if (key_len > 1 && keys[i].last(1) == "_") {
                            key_name = keys[i].left(key_len-1);
                            shift_state = SHIFT_STATE_OFF;
                        } else
                        if (key_len > 1 && keys[i].last(1) == "^") {
                            key_name = keys[i].left(key_len-1);
                            shift_state = SHIFT_STATE_ON;
                        } else
                            key_name = keys[i];
                        unsigned int key_code = translate_key(key_name);
                        if (key_code == _FFFF)
                            QMessageBox::critical(0, ScanKeyboard::tr("Error"), ScanKeyboard::tr("Unknown key %1").arg(keys[i]));
                        else
                            scan_data[keys_count++] = {
                                                        .key_code = key_code,
                                                        .scan_line = scan,
                                                        .out_line = out,
                                                        .shift_state = shift_state
                            };
                    }
            }
        }
    }

    code_ctrl = translate_key(cd->get_parameter("ctrl").value);
    code_shift = translate_key(cd->get_parameter("shift").value);
    code_ruslat = translate_key(cd->get_parameter("ruslat").value);

    i_shift.change(1);
    i_ctrl.change(1);
    i_ruslat.change(1);

}

void ScanKeyboard::key_down(unsigned int key)
{
    //qDebug() << "DOWN" << Qt::hex << key;
    if (key == code_ctrl)
        i_ctrl.change(0);
    else if (key == code_shift)
        i_shift.change(0);
    else if (key == code_ruslat) {
        i_ruslat.change(0);
        set_rus(!rus_mode);
    } else {
        for (unsigned int i=0; i<keys_count; i++)
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
            }
    }
}

void ScanKeyboard::key_up(unsigned int key)
{
    //qDebug() << "UP" << key;
    if (key == code_ctrl)
        i_ctrl.change(1);
    else if (key == code_shift)
        i_shift.change(1);
    else if (key == code_ruslat) {
        i_ruslat.change(1);
        set_rus(!rus_mode);
    } else {
        for (unsigned int i=0; i<keys_count; i++)
            if (scan_data[i].key_code == key)
            {
                unsigned int l = scan_data[i].scan_line;
                key_array[l] |= create_mask(1, scan_data[i].out_line);
                calculate_out();

                if (scan_data[i].shift_state != SHIFT_STATE_KEEP) {
                    i_shift.change(stored_shift);
                    // qDebug() << "SHIFT " << stored_shift;

                }

            }
    }
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

void ScanKeyboard::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == SCAN_CALLBACK)
        calculate_out();
    else
        // LED_CALLBACK
        set_rus((i_ruslat_led.value & 1) == 1);
}

void ScanKeyboard::set_rus(bool new_rus)
{
    //qDebug() << "RUS" << new_rus;
    Keyboard::set_rus(new_rus);
}

ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new ScanKeyboard(im, cd);
}
