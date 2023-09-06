#ifndef CONFIG_H
#define CONFIG_H

#include <QObject>
#include <QString>
#include <QFile>

class EmulatorConfigDevice: public QObject
{
    Q_OBJECT

public:
    EmulatorConfigDevice(QString device_name, QString device_type);
    ~EmulatorConfigDevice();

    QString device_name;
    QString device_type;

    void add_parameter(QString new_param_name, QString range_left, QString param_value, QString range_right, QString extended_right);

private:

};

class EmulatorConfig: public QObject
{
    Q_OBJECT

public:
    EmulatorConfig();
    EmulatorConfig(QString file_name);
    ~EmulatorConfig();

    int devices_count;

    void load_from_file(QString file_name, bool system_only = false);
    void free_devices();

    EmulatorConfigDevice * get_device(int i);

private:
    EmulatorConfigDevice *devices[100];

    QString read_next_entity(QString *config, QString stop);
    QString read_extended_entity(QString *config, QString stop);
    EmulatorConfigDevice *add_device(QString device_name, QString device_type);
};

#endif // CONFIG_H
