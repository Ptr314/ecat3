#ifndef CONFIG_H
#define CONFIG_H

#include <QObject>
#include <QString>
#include <QFile>

struct EmulatorConfigParameter {
    QString name;
    QString left_range;
    QString value;
    QString right_range;
    QString right_extended;
};

class EmulatorConfigDevice: public QObject
{
    Q_OBJECT

public:
    EmulatorConfigDevice(QString name, QString type);
    ~EmulatorConfigDevice();

    QString name;
    QString type;
    unsigned int parameters_count;
    EmulatorConfigParameter parameters[100];


    void add_parameter(QString name, QString left_range, QString value, QString right_range, QString right_extended);
    QString extended_parameter(unsigned int i, QString expected_name);
    EmulatorConfigParameter get_parameter(QString name, bool required = true);
    EmulatorConfigParameter get_parameter(unsigned int id);
};

class EmulatorConfig: public QObject
{
    Q_OBJECT

public:
    EmulatorConfig();
    EmulatorConfig(QString file_name);
    ~EmulatorConfig();

    unsigned int devices_count;

    void load_from_file(QString file_name, bool system_only = false);
    void free_devices();

    EmulatorConfigDevice * get_device(int i);
    EmulatorConfigDevice * get_device(QString name);

private:
    EmulatorConfigDevice *devices[100];

    QString read_next_entity(QString *config, QString stop);
    QString read_extended_entity(QString *config, QString stop);
    EmulatorConfigDevice *add_device(QString device_name, QString device_type);
};

#endif // CONFIG_H
