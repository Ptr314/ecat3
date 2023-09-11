#ifndef EMULATOR_H
#define EMULATOR_H

#include <QString>
#include <QSettings>

#include "core.h"

class Emulator
{
private:
    QString work_path;
    QSettings *settings;
    bool loaded;
    InterfaceManager *im;
    SystemData sd;

    void register_devices();

public:
    DeviceManager *dm;

    Emulator(QString work_path, QString ini_file);

    QString read_setup(QString section, QString ident, QString def_val);
    void load_config(QString file_name);
};

#endif // EMULATOR_H
