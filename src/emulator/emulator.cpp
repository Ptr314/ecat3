#include <QFileInfo>

#include "core.h"
#include "emulator.h"
#include "emulator/config.h"
#include "emulator/utils.h"

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

    EmulatorConfig * config = new EmulatorConfig(work_path + file_name);

    EmulatorConfigDevice * system = config->get_device("system");
    QFileInfo fi(file_name);
    sd.system_file = file_name;
    sd.system_path = fi.absolutePath();
    sd.system_type = system->get_parameter("type").value;
    sd.system_name = system->get_parameter("name").value;
    sd.system_version = system->get_parameter("version", false).value;
    sd.system_charmap = system->get_parameter("charmap", false).value;
    sd.software_path = fi.absolutePath();
    sd.mapper_cache = parse_numeric_value(this->read_setup("Core", "mapper_cache", "8"));

    sd.allowed_files = system->get_parameter("files", false).value;

    for (unsigned int i=0; i<config->devices_count; i++)
    {
        EmulatorConfigDevice *d = config->get_device(i);
        if (!d->type.isEmpty())
        {
            this->dm->add_device(this->im, d);
        }
    }
    this->dm->load_devices_config(&sd);
    delete config;
    this->loaded = true;
}

void Emulator::register_devices()
{
    dm->register_device("ram", create_ram);
}
