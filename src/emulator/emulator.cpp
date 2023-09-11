#include <QFileInfo>

#include "core.h"
#include "emulator.h"
#include "emulator/config.h"
#include "emulator/utils.h"

#include "emulator/devices/cpu/i8080.h"

Emulator::Emulator(QString work_path, QString data_path, QString ini_file):
    work_path(work_path),
    data_path(data_path),
    loaded(false)
{
    qDebug() << "INI path: " + ini_file;
    this->settings = new QSettings (ini_file, QSettings::IniFormat);
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
    sd.software_path = work_path;
    sd.mapper_cache = parse_numeric_value(this->read_setup("Core", "mapper_cache", "8"));

    sd.allowed_files = system->get_parameter("files", false).value;

    this->load_charmap();

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
    dm->register_device("rom", create_rom);
    dm->register_device("memory_mapper", create_memory_mapper);
    dm->register_device("i8080", create_i8080);
}


void Emulator::load_charmap()
{
    //TODO: delete old charmap objects

    if (!sd.system_charmap.isEmpty())
    {
        QFile f(this->data_path + this->sd.system_charmap + ".chr");
        f.open(QFile::ReadOnly);
        QString s = QString::fromUtf8(f.readAll());

        unsigned int count = 0;
        for (unsigned int i = 0; i < s.length(); i++)
        {
            QChar c = s.at(i);
            if (c != '\x0D' && c != '\x0A') this->charmap[count++] = new QChar(c);
        }
        qDebug() << "Chars loaded: " << count;
    } else
        for (unsigned int i = 0; i < 256; i++) this->charmap[i] = new QChar('.');
}

QChar * Emulator::translate_char(unsigned int char_code)
{
    return this->charmap[char_code];
}
