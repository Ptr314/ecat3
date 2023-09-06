#include "core.h"

DeviceManager::DeviceManager()
{
    this->registered_devices_count = 0;

    this->device_count = 2;
    this->error_message = "";
    this->error_device = nullptr;
}

DeviceManager::~DeviceManager()
{
    this->clear();
}

void DeviceManager::clear()
{
    //TODO: Implement this
}

void DeviceManager::register_device(QString device_type, CreateDeviceFunc func)
{
    this->registered_devices[this->registered_devices_count].type = device_type;
    this->registered_devices[this->registered_devices_count].create_func = func;

    this->registered_devices_count++;
}

void DeviceManager::add_device(InterfaceManager *im, EmulatorConfigDevice *d)
{
    QString name = d->device_name;
    unsigned int index;
    if (name.compare("cpu")==0)
        index=0;
    else
    if (name.compare("mapper")==0)
        index=1;
    else
        index = this->device_count++;

    CreateDeviceFunc create_func = nullptr;
    for (unsigned int i=0; i < this->registered_devices_count; i++)
    {
        if (this->registered_devices[i].type == d->device_type) create_func = this->registered_devices[i].create_func;
    }

    if (create_func != nullptr)
    {
        this->devices[index]->device_type = d->device_type;
        this->devices[index]->device_name = name;
        this->devices[index]->device = create_func(im, d);
    } else
        QMessageBox::critical(0, DeviceManager::tr("Error"), DeviceManager::tr("Can't create device %1").arg(name));
}

InterfaceManager::InterfaceManager(DeviceManager *dm)
{
    this->dm = dm;
}

InterfaceManager::~InterfaceManager()
{
    this->clear();
}

void InterfaceManager::clear()
{
    //TODO: Implement this
}

ComputerDevice * create_port(InterfaceManager *im, EmulatorConfigDevice *cd){
    //TODO: Implement this
    return nullptr;
}
