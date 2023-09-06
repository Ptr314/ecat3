#include "emulator.h"
#include "emulator/config.h"

Emulator::Emulator(QString work_path, QString ini_file)
{
    this->work_path = work_path;
    this->settings = new QSettings (ini_file, QSettings::IniFormat);
    qDebug() << "INI path: " + ini_file;
    this->loaded = false;
}

QString Emulator::read_setup(QString section, QString ident, QString def_val)
{
    QString value = this->settings->value(section + "/" + ident, def_val).toString();
    return value;
}

void Emulator::load_config(QString file_name)
{
    if (this->loaded)
    {
        //Delete loaded machine
        delete this->dm;
        this->loaded = false;
    }

    this->dm = new DeviceManager();
    this->im = new InterfaceManager(this->dm);

    this->register_devices();

    EmulatorConfig *config = new EmulatorConfig(work_path + file_name);

    //config->load_from_file(work_path + file_name);

    //TODO: Здесь обрабока общих параметорв из файла

    for (int i=0; i<config->devices_count; i++)
    {
        EmulatorConfigDevice *d = config->get_device(i);
        if (!d->device_type.isEmpty())
        {
            this->dm->add_device(this->im, d);
        }
    }
}

void Emulator::register_devices()
{
    dm->register_device("port", create_port);
}
