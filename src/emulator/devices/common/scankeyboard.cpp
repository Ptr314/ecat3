#include <QRegularExpression>

#include "emulator/utils.h"
#include "scankeyboard.h"

ScanKeyboard::ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    Keyboard(im, cd),
    keys_count(0)
{
    i_scan =   create_interface(8, "scan", MODE_R, SCAN_CALLBACK);
    i_output = create_interface(8, "output", MODE_W);
    i_shift =  create_interface(1, "shift", MODE_W);
    i_ctrl =   create_interface(1, "ctrl", MODE_W);
    i_ruslat = create_interface(1, "ruslat", MODE_W);

    memset(&key_array, _FFFF, sizeof(key_array));

    calculate_out();
}

void ScanKeyboard::load_config(SystemData *sd)
{
    Keyboard::load_config(sd);

    static QRegularExpression re_crlf("[\r\n]");
    static QRegularExpression re_space("[\t ]");

    QString layout = cd->get_parameter("@layout").right_extended.trimmed();
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
                    unsigned int key_code = translate_key(keys[i]);
                    if (key_code == _FFFF)
                        QMessageBox::critical(0, ScanKeyboard::tr("Error"), ScanKeyboard::tr("Unknown key %1").arg(keys[i]));
                    else
                        scan_data[keys_count++] = {
                                                    .key_code = key_code,
                                                    .scan_line = scan,
                                                    .out_line = out
                        };
                }
        }
    }

    code_ctrl = translate_key(cd->get_parameter("ctrl").value);
    code_shift = translate_key(cd->get_parameter("shift").value);
    code_ruslat = translate_key(cd->get_parameter("ruslat").value);

    i_shift->change(1);
    i_ctrl->change(1);
    i_ruslat->change(1);

}

void ScanKeyboard::key_down(unsigned int key)
{
    if (key == code_ctrl)
        i_ctrl->change(0);
    else if (key == code_shift)
        i_shift->change(0);
    else if (key == code_ruslat)
        i_ruslat->change(0);
    else {
        for (unsigned int i=0; i<keys_count; i++)
            if (scan_data[i].key_code == key)
            {
                unsigned int l = scan_data[i].scan_line;
                key_array[l] &= ~create_mask(1, scan_data[i].out_line);
                calculate_out();
                //qDebug() << l << Qt::hex << key_array[l];
            }
    }
}

void ScanKeyboard::key_up(unsigned int key)
{
    if (key == code_ctrl)
        i_ctrl->change(1);
    else if (key == code_shift)
        i_shift->change(1);
    else if (key == code_ruslat)
        i_ruslat->change(1);
    else {
        for (unsigned int i=0; i<keys_count; i++)
            if (scan_data[i].key_code == key)
            {
                unsigned int l = scan_data[i].scan_line;
                key_array[l] |= create_mask(1, scan_data[i].out_line);
                calculate_out();
            }
    }
}

void ScanKeyboard::calculate_out()
{
    unsigned int new_value = _FFFF;
    for (unsigned int i = 0; i<scan_lines; i++)
    {
        unsigned int mask = create_mask(1, i);
        if ((i_scan->value & mask) == 0)
            new_value &= key_array[i];
    }
    i_output->change(new_value);
}

void ScanKeyboard::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    calculate_out();
}


ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new ScanKeyboard(im, cd);
}
