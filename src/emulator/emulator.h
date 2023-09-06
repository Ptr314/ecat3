#ifndef EMULATOR_H
#define EMULATOR_H

#include <QString>
#include <QSettings>

#include "core.h"

class Emulator
{
public:
    Emulator(QString work_path, QString ini_file);

    QString read_setup(QString section, QString ident, QString def_val);
    void load_config(QString file_name);
private:
    QString work_path;
    QSettings *settings;
    bool loaded;
    DeviceManager *dm;
    InterfaceManager *im;

    void register_devices();
};

#endif // EMULATOR_H
