#ifndef EMULATOR_H
#define EMULATOR_H

#include <QString>
#include <QSettings>

#include "core.h"

class Emulator
{
private:
    QString work_path;
    QString data_path;
    QSettings *settings;
    bool loaded;
    InterfaceManager *im;
    SystemData sd;

    QChar * charmap[256];

    void register_devices();

public:
    DeviceManager *dm;

    Emulator(QString work_path, QString data_path, QString ini_file);

    QString read_setup(QString section, QString ident, QString def_val);
    void load_config(QString file_name);
    void load_charmap();
    QChar * translate_char(unsigned int system_code);
};

#endif // EMULATOR_H
