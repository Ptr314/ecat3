#include <QFile>
#include <QMessageBox>

#include "config.h"

EmulatorConfigDevice::EmulatorConfigDevice(QString device_name, QString device_type)
{
    this->device_name = device_name;
    this->device_type = device_type;
}

EmulatorConfigDevice::~EmulatorConfigDevice()
{

}

void EmulatorConfigDevice::add_parameter(QString new_param_name, QString range_left, QString param_value, QString range_right, QString extended_right)
{
    qDebug() << new_param_name << range_left << param_value << range_right << extended_right;
}

EmulatorConfig::EmulatorConfig()
{
    this->devices_count = 0;
}

EmulatorConfig::~EmulatorConfig()
{
    if (this->devices_count > 0) this->free_devices();
}

EmulatorConfig::EmulatorConfig(QString file_name)
{
    EmulatorConfig();
    this->load_from_file(file_name);
}

void EmulatorConfig::free_devices()
{
    //TODO: Delete devices here
    this->devices_count = 0;
}

QString EmulatorConfig::read_next_entity(QString *config, QString stop = "")
{
    QString s = "";
    QString parser_spaces = " \x09\x0D\x0A";
    QString parser_line = "\x0D\x0A";
    QString parser_border = "=[]{}";
    QString terminator = parser_border + stop;
    QString stop_chars;
    if (!config->isEmpty())
    {
        QChar c = config->at(0);
        config->remove(0, 1);
        //Skipping spaces
        while (!config->isEmpty() && parser_spaces.contains(c))
        {
            c = config->at(0);
            config->remove(0, 1);
        }
        if (!config->isEmpty())
        {

            if (!terminator.contains(c))
            {
                if (c == '"')
                {
                    stop_chars = parser_line + "\"";
                    c = config->at(0);
                    config->remove(0, 1);
                } else {
                    stop_chars = parser_border + parser_line + stop;
                }
                while (!config->isEmpty() && !stop_chars.contains(c))
                {
                    s.append(c);
                    c = config->at(0);
                    config->remove(0, 1);
                }
                if (!config->isEmpty() and c != '"') {
                    *config = QString(c) + *config;
                }
                return s.trimmed();
            } else {
                return c;
            }
        } else {
            return "";
        }
    }
    return "";
}

QString EmulatorConfig::read_extended_entity(QString *config, QString stop)
{
    QString s = "";
    if (!config->isEmpty())
    {
        QChar c = config->at(0);
        config->remove(0, 1);
        while (!config->isEmpty() && !stop.contains(c))
        {
            s = s + QString(c);
            c = config->at(0);
            config->remove(0, 1);
        }
    }
    return s;
}

EmulatorConfigDevice * EmulatorConfig::add_device(QString device_name, QString device_type)
{
    qDebug() << device_name << device_type;
    EmulatorConfigDevice *new_device = new EmulatorConfigDevice(device_name, device_type);
    this->devices[devices_count] = new_device;
    this->devices_count += 1;
    return new_device;
}

void EmulatorConfig::load_from_file(QString file_name, bool system_only)
{
    QString device_name;
    QString device_type;

    qDebug() << "Loading: " + file_name;

    if (this->devices_count > 0) this->free_devices();

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Error reading config file %1").arg(file_name));
        return;
    }
    QString config = QString(file.readAll());
    while(!config.isEmpty())
    {
        device_name = this->read_next_entity(&config, ":");
        if (device_name.isEmpty()) return;
        if (device_name.compare("system")==0)
        {
            device_type = "";
        } else {
            QString s = this->read_next_entity(&config, ":");
            if (s.isEmpty() || s.compare(":")!=0)
            {
                QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - no type found").arg(device_name));
                return;
            }
            device_type = this->read_next_entity(&config);
            if (device_type.isEmpty())
            {
                QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - no type found").arg(device_name));
                return;
            }
        }
        EmulatorConfigDevice * new_device = this->add_device(device_name, device_type);

        QString s = this->read_next_entity(&config);
        if (s.isEmpty() || s.compare("{")!=0)
        {
            QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - no description found").arg(device_name));
            return;
        }

        QString param_name = this->read_next_entity(&config);
        if (param_name.isEmpty())
        {
            QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - incorrect parameters").arg(device_name));
            return;
        }
        while (param_name.compare("}")!=0)
        {
            QString new_param_name = param_name;
            s = this->read_next_entity(&config);
            if (s.isEmpty() || (s.compare("[")!=0 && s.compare("=")!=0))
            {
                QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - incorrect parameters").arg(device_name));
                return;
            }

            //Do we have a range on the left?
            QString range_left;
            if (s.compare("[")==0)
            {
                range_left = "[";
                s = this->read_next_entity(&config);
                while (s.compare("=")!=0)
                {
                    range_left = range_left + s;
                    s = this->read_next_entity(&config);
                    if (s.isEmpty())
                    {
                        QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name).arg(param_name));
                        return;
                    }
                }
            } else {
                range_left = "";
            }

            //Reading a right part
            s = this->read_next_entity(&config);
            if (s.isEmpty())
            {
                QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name).arg(param_name));
                return;
            }
            QString param_value;
            QString range_right;
            if (s.compare("{")!=0)
            {
                param_value = s;
                s = this->read_next_entity(&config);
                if (s.isEmpty())
                {
                    QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name).arg(param_name));
                    return;
                }
                if (s.compare("}")==0)
                {
                    new_device->add_parameter(new_param_name, range_left, param_value, "", "");
                    param_name = s;
                    break;
                }
                range_right = "";
                if (s.compare("[")==0)
                {
                    while(1)
                    {
                        range_right = range_right + s;
                        s = this->read_next_entity(&config);
                        if (s.isEmpty())
                        {
                            QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name).arg(param_name));
                            return;
                        }
                        if (s.compare("]")==0) break;
                    }
                    range_right = range_right + s;
                    s = this->read_next_entity(&config);
                    if (s.isEmpty())
                    {
                        QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name).arg(param_name));
                        return;
                    }
                }
            } else {
                param_value = "";
                range_right = "";
            }

            QString extended_right = "";
            if (s.compare("{")==0)
            {
                QString extended_right = this->read_extended_entity(&config, "}");
                param_name = this->read_next_entity(&config);
                if (param_name.isEmpty())
                {
                    QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - incorrect parameters").arg(device_name));
                    return;
                }
            } else {
                param_name = s;
            }
            new_device->add_parameter(new_param_name, range_left, param_value, range_right, extended_right);
        }

        if (system_only && device_name.compare("system")==0) break;
    }
    file.close();
}

EmulatorConfigDevice * EmulatorConfig::get_device(int i)
{
    return this->devices[i];
}
