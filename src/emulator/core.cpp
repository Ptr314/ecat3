#include <QException>
#include <QRandomGenerator>

#include "core.h"
#include "emulator/utils.h"

//----------------------- class Interface -------------------------------//

Interface::Interface(
    ComputerDevice * device,
    InterfaceManager * im,
    unsigned int size,
    QString name,
    unsigned int mode,
    unsigned int callback_id
):
    old_value(-1),
    edge_value(-1),
    im(im),
    callback_id(callback_id),
    value(-1),
    name(name),
    device(device),
    size(size),
    mode(mode),
    linked(0),
    linked_bits(0)
{
    im->register_interface(this);
}

//----------------------- class DeviceManager -------------------------------//

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
    QString name = d->name;
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
        if (this->registered_devices[i].type == d->type) create_func = this->registered_devices[i].create_func;
    }

    if (create_func != nullptr)
    {
        this->devices[index].device_type = d->type;
        this->devices[index].device_name = name;
        this->devices[index].device = create_func(im, d);
    } else
        QMessageBox::critical(0, DeviceManager::tr("Error"), DeviceManager::tr("Can't create device %1").arg(name));
}

DeviceDescription * DeviceManager::get_device(unsigned int i)
{
    return &(this->devices[i]);
}

void DeviceManager::load_devices_config(SystemData *sd)
{
    for (unsigned int i=0; i < this->device_count; i++)
    {
        this->get_device(i)->device->load_config(sd);
    }
}

//----------------------- class InterfaceManager -------------------------------//

InterfaceManager::InterfaceManager(DeviceManager *dm):interfaces_count(0), dm(dm){}

InterfaceManager::~InterfaceManager()
{
    this->clear();
}

void InterfaceManager::register_interface(Interface *i)
{
    this->interfaces[this->interfaces_count++] = i;
}

void InterfaceManager::clear()
{
    //TODO: Implement this
}

//----------------------- class ComputerDevice -------------------------------//

ComputerDevice::ComputerDevice(InterfaceManager *im, EmulatorConfigDevice *cd):
    device_type(cd->type),
    device_name(cd->name),
    im(im),
    cd(cd)
{
    //TODO: taking clock from config

    this->clock_miltiplier = 1;
    this->clock_divider = 1;
    this->clock_stored = 0;
}

void ComputerDevice::clock([[maybe_unused]] unsigned int counter)
{
    //Does nothing by default, by may be overridden
}

void ComputerDevice::system_clock(unsigned int counter)
{
    if (this->clock_miltiplier == this->clock_divider)
        this->clock(counter);
    else {
        this->clock_stored += counter * this->clock_miltiplier;
        unsigned int internal_clock = this->clock_stored / this->clock_divider;
        if (internal_clock > 0)
        {
            this->clock(internal_clock);
            this->clock_stored -= internal_clock * this->clock_divider;
        }
    }
}

void ComputerDevice::load_config(SystemData *sd)
{
    //TODO: Implement
}

Interface * ComputerDevice::create_interface(unsigned int size, QString name, unsigned int mode, unsigned int callback_id)
{
    return new Interface(this, im, size, name, mode, callback_id);
}

void ComputerDevice::interface_callback([[maybe_unused]] unsigned int callback_id, [[maybe_unused]] unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    //Does nothing by default, by may be overridden
}

//----------------------- class Memory -------------------------------//

Memory::Memory(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd),
    can_read(false),
    can_write(false),
    buffer(nullptr),
    size(0),
    fill(0),
    read_callback(nullptr),
    write_callback(nullptr)
{
    this->address = this->create_interface(16, "address", MODE_R, 1);
    this->data = this->create_interface(8, "data", MODE_W);
}

Memory::~Memory()
{
    //TODO: Destroy buffer here
}

unsigned int Memory::get_value(unsigned int address)
{
    //TODO: Implement
    return 0;
}

void Memory::set_value(unsigned int address)
{
    //TODO: Implement
}

void Memory::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    //TODO: Implement
}

void Memory::set_size(unsigned int value)
{
    if (this->buffer != nullptr) delete [] buffer;
    buffer = new short[value];
    this->size = value;

    QRandomGenerator *rg = QRandomGenerator::global();

    for (unsigned int i=0; i < value; i++) buffer[i]=rg->bounded(255);
}

//----------------------- class RAM -------------------------------//

RAM::RAM(InterfaceManager *im, EmulatorConfigDevice *cd):
    Memory(im, cd)
{
    this->can_read = true;
    this->can_write = true;
}

void RAM::load_config(SystemData *sd)
{
    Memory::load_config(sd);
    this->set_size(parse_numeric_value(this->cd->get_parameter("size").value));

    try {
        this->fill = parse_numeric_value(this->cd->get_parameter("fill").value);
    } catch (QException &e) {
        this->fill = 0;
    }

    this->reset(true);

}

void RAM::reset(bool cold)
{
    if (cold && this->buffer!=nullptr) memset(this->buffer, this->fill, this->size);
}

ComputerDevice * create_ram(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new RAM(im, cd);
}
