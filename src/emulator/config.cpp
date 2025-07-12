// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Emulator config functions, source

#include <QFile>
#include <QMessageBox>
#include <QException>

#include "config.h"

EmulatorConfigDevice::EmulatorConfigDevice(QString name, QString type):
    name(name),
    type(type),
    parameters_count(0)
{}

EmulatorConfigDevice::~EmulatorConfigDevice(){}

void EmulatorConfigDevice::add_parameter(QString name, QString left_range, QString value, QString right_range, QString right_extended)
{
    parameters[parameters_count].name = name;
    parameters[parameters_count].left_range = left_range;
    parameters[parameters_count].value = value;
    parameters[parameters_count].right_range = right_range;
    parameters[parameters_count].right_extended = right_extended;

    parameters_count++;
}

EmulatorConfigParameter EmulatorConfigDevice::get_parameter(QString name, bool required)
{
    for (unsigned int i = 0; i < parameters_count; i++)
    {
        if (parameters[i].name == name) return parameters[i];
    }
    if (required)
        throw QException();
    else
        return {"", "", "", "", ""};
}

EmulatorConfig::EmulatorConfig():
    devices_count(0)
{}

EmulatorConfig::~EmulatorConfig()
{
    if (devices_count > 0) free_devices();
}

EmulatorConfig::EmulatorConfig(QString file_name)
{
    EmulatorConfig();
    load_from_file(file_name);
}

void EmulatorConfig::free_devices()
{
    //TODO: why it crashes the system?
    //for(unsigned int i = 0; i < devices_count; i++) delete devices[i];
    devices_count = 0;
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
        if (!config->isEmpty() || !parser_spaces.contains(c))
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
    EmulatorConfigDevice *new_device = new EmulatorConfigDevice(device_name, device_type);
    devices[devices_count++] = new_device;
    return new_device;
}

QString EmulatorConfigDevice::extended_parameter(unsigned int i, QString expected_name)
{
    QString s = parameters[i].right_extended;
    QStringList list = s.split(u',', skip_empty_parts);
    for (int i = 0; i < list.size(); i++)
    {
        QStringList parameter = list.at(i).split(u'=', skip_empty_parts);
        QString name = parameter.at(0).toLower().trimmed();
        QString value = parameter.at(1).toLower().trimmed();
        if (name == expected_name) return value;
    }
    return "";
}



void EmulatorConfig::load_from_file(QString file_name, bool system_only)
{
    QString device_name;
    QString device_type;

    //qDebug() << "Loading: " + file_name;

    if (devices_count > 0) free_devices();

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Error reading config file %1").arg(file_name));
        return;
    }
    QString config = QString(file.readAll());
    while(!config.isEmpty())
    {
        device_name = read_next_entity(&config, ":");
        if (device_name.isEmpty()) return;
        if (device_name.compare("system")==0)
        {
            device_type = "";
        } else {
            QString s = read_next_entity(&config, ":");
            if (s.isEmpty() || s.compare(":")!=0)
            {
                QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - no type found").arg(device_name));
                return;
            }
            device_type = read_next_entity(&config);
            if (device_type.isEmpty())
            {
                QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - no type found").arg(device_name));
                return;
            }
        }
        EmulatorConfigDevice * new_device = add_device(device_name, device_type);

        QString s = read_next_entity(&config);
        if (s.isEmpty() || s.compare("{")!=0)
        {
            QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - no description found").arg(device_name));
            return;
        }

        QString param_name = read_next_entity(&config);
        if (param_name.isEmpty())
        {
            QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device '%1' - incorrect parameters").arg(device_name));
            return;
        }
        while (param_name.compare("}")!=0)
        {
            QString new_param_name = param_name;
            s = read_next_entity(&config);
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
                s = read_next_entity(&config);
                while (s.compare("=")!=0)
                {
                    range_left = range_left + s;
                    s = read_next_entity(&config);
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
            s = read_next_entity(&config);
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
                s = read_next_entity(&config);
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
                        s = read_next_entity(&config);
                        if (s.isEmpty())
                        {
                            QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name, param_name));
                            return;
                        }
                        if (s.compare("]")==0) break;
                    }
                    range_right = range_right + s;
                    s = read_next_entity(&config);
                    if (s.isEmpty())
                    {
                        QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Configuration error for device parameter '%1:%2'").arg(device_name, param_name));
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
                extended_right = read_extended_entity(&config, "}");
                param_name = read_next_entity(&config);
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
    return devices[i];
}

EmulatorConfigDevice * EmulatorConfig::get_device(QString name)
{
    for (unsigned int i=0; i<devices_count; i++)
    {
        if (devices[i]->name == name) return devices[i];
    }
    QMessageBox::critical(0, EmulatorConfig::tr("Error"), EmulatorConfig::tr("Device '%1' not found").arg(name));
    return nullptr;
}

