#include <QException>
#include <QRandomGenerator>

#include "core.h"
#include "emulator/utils.h"

MapperCacheEntry MapperCache[15];

//----------------------- class Interface -------------------------------//

Interface::Interface(
    ComputerDevice * device,
    InterfaceManager * im,
    unsigned int size,
    QString name,
    unsigned int mode,
    unsigned int callback_id
):
    size(size),
    mode(mode),
    old_value(-1),
    edge_value(-1),
    im(im),
    callback_id(callback_id),
    value(-1),
    name(name),
    linked(0),
    linked_bits(0),
    device(device)
{
    im->register_interface(this);
}

void Interface::connect(LinkedInterface s, LinkedInterface d)
{
    int index = -1;

    for (unsigned int i=0; i < this->linked; i++)
        if (this->linked_interfaces[i].d.i == d.i) index = i;

    if (index < 0)
    {
        this->linked_interfaces[this->linked].s = s;
        this->linked_interfaces[this->linked].d = d;
        d.i->connect(d, s);
        this->linked_bits |= s.mask;
        this->linked++;
    }
}

void Interface::set_size(unsigned int new_size)
{
    this->size = new_size;
    this->mask = create_mask(new_size, 0);
}

unsigned int Interface::Interface::get_size()
{
    return this->size;
}

void Interface::set_mode(unsigned int new_mode)
{
    unsigned int prev_mode = this->mode;
    this->mode = new_mode;
    //Если интерфейс переключился с вывода на ввод, нужно правильно
    //установить его значение, если оно контролируется другим интерфейсом.
    //Поэтому просматриваем все соединенные интерфейсы, и если один из них
    //находится в режиме вывода, то имитируем установку его значения,
    //чтобы правильно выставились значения на текущем интерфейсе
    if (new_mode == MODE_R && prev_mode == MODE_W)
    {
        for (unsigned int i=0; i>this->linked; i++)
        {
            Interface * li = this->linked_interfaces[i].d.i;
            if (li->mode == MODE_W) li->change(li->value);
        }
    }
    if (new_mode == MODE_OFF) this->change(_FFFF);
}

void Interface::change(unsigned int new_value)
{
    if (this->mode == MODE_W)
    {
        this->old_value = this->value;
        this->value = new_value;
        for (unsigned int i=0; i < this->linked; i++)
        {
            this->linked_interfaces[i].d.i->changed(this->linked_interfaces[i], new_value);
        }
    } else {
        if (this->mode == MODE_OFF)
        {
            this->im->dm->error(this->device, Interface::tr("Interface '%1' is in OFF state, writing is impossible").arg(this->name));
        }
    }
}

void Interface::changed(LinkData link, unsigned int value)
{
    if (this->mode == MODE_R)
    {
        unsigned int new_value = this->value && ~link.d.mask; //Set expected bits to 0
        new_value |=  ((value & link.s.mask) >> link.s.shift) << link.d.shift;
        this->old_value = this->value;
        this->value = new_value;

        if (callback_id > 0) this->device->interface_callback(callback_id, new_value, this->old_value);
    }
}

void Interface::clear()
{
    this->change(_FFFF);
}

bool Interface::pos_edge()
{
    //TODO: Implement
}

bool Interface::neg_edge()
{
    //TODO: Implement
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
    //TODO: Implement
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
        QMessageBox::critical(0, DeviceManager::tr("Error"), DeviceManager::tr("Can't create device %1:%2").arg(name, d->type));
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

ComputerDevice * DeviceManager::get_device_by_name(QString name, bool required)
{
    for (unsigned int i=0; i < this->device_count; i++)
    {
        if (this->devices[i].device->name == name) return this->devices[i].device;
    }
    if (required)
        throw QException();
    else
        return nullptr;
}

unsigned int DeviceManager::get_device_index(QString name)
{
    //TODO: Implement
}

void DeviceManager::reset_devices(bool is_cold)
{
    //TODO: Implement
}

void DeviceManager::clock(unsigned int counter)
{
    //TODO: Implement
}

void DeviceManager::error(ComputerDevice *d, QString message)
{
    this->error_device = d;
    this->error_message = message;
    throw QException();
}

void DeviceManager::error_clear()
{
    //TODO: Implement
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
    //TODO: Implement
}

//----------------------- class ComputerDevice -------------------------------//

ComputerDevice::ComputerDevice(InterfaceManager *im, EmulatorConfigDevice *cd):
    type(cd->type),
    name(cd->name),
    cd(cd),
    im(im)
{
    try {
        QString s = cd->get_parameter("clock").value;
        if (s.isEmpty())
        {
            QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect clock value for '%1'").arg(this->name));
            throw QException();
        } else {
            int pos = s.indexOf("/");
            if (pos>0) {
                this->clock_miltiplier = parse_numeric_value(s.first(pos));
                this->clock_divider = parse_numeric_value(s.last(s.length()-pos-1));
            } else {
                this->clock_miltiplier = parse_numeric_value(s);
                this->clock_divider = 1;
            }
        }
    } catch (QException &e) {
        this->clock_miltiplier = 1;
        this->clock_divider = 1;
    }

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
    //TODO: ! Implement
    qDebug() << "Core::ComputerDevice::load_config";

}

Interface * ComputerDevice::create_interface(unsigned int size, QString name, unsigned int mode, unsigned int callback_id)
{
    return new Interface(this, im, size, name, mode, callback_id);
}

void ComputerDevice::interface_callback([[maybe_unused]] unsigned int callback_id, [[maybe_unused]] unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    //Does nothing by default, but may be overridden
}

void ComputerDevice::reset([[maybe_unused]] bool cold)
{
    //Does nothing by default, but may be overridden
}

//----------------------- class Memory -------------------------------//

Memory::Memory(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd),
    can_read(false),
    can_write(false),
    auto_output(false),
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
    if (this->buffer != nullptr) delete [] buffer;
}

unsigned int Memory::get_value(unsigned int address)
{
    if (this->read_callback != nullptr) {
        //TODO: implement
    }

    if (this->can_read && address<this->size)
        return this->buffer[address];
    else
        return 0xFF;
}

void Memory::set_value(unsigned int address, unsigned int value)
{
    if (this->write_callback != nullptr) {
        //TODO: implement
    }

    if (this->can_write && address < this->size)
        this->buffer[address] = (uint8_t)value;
}

void Memory::interface_callback([[maybe_unused]] unsigned int callback_id, unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    unsigned int address = new_value & create_mask(this->address->get_size(), 0);
    if (address < this->size and this->auto_output) this->data->change(this->buffer[address]);
}

void Memory::set_size(unsigned int value)
{
    if (this->buffer != nullptr) delete [] buffer;
    buffer = new uint8_t[value];
    this->size = value;

    QRandomGenerator *rg = QRandomGenerator::global();

    for (unsigned int i=0; i < value; i++) buffer[i]=rg->bounded(255);
}

unsigned int Memory::get_size()
{
    return this->size;
}

void Memory::set_callback(MemoryCallbackFunc f, unsigned int mode)
{
    //TODO: Implement
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

//----------------------- class ROM -------------------------------//

ROM::ROM(InterfaceManager *im, EmulatorConfigDevice *cd):
    Memory(im, cd)
{
    this->can_read = true;
    this->can_write = false;
    this->auto_output = true;
}

void ROM::load_config(SystemData *sd)
{
    Memory::load_config(sd);
    this->set_size(parse_numeric_value(this->cd->get_parameter("size").value));

    try {
        this->fill = parse_numeric_value(this->cd->get_parameter("fill").value);
    } catch (QException &e) {
        this->fill = 0xFF;
    }

    if (this->buffer!=nullptr) memset(this->buffer, this->fill, this->size);

    QString file_name = sd->software_path + this->cd->get_parameter("image").value;
    QFile file(file_name);
    if (file.open(QIODevice::ReadOnly)){
        unsigned int file_size = file.size();
        if (file_size > this->size)
        {
            QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("ROM image file for '%1' is too big").arg(this->name));
            throw QException();
        }
        QByteArray data = file.readAll();
        memcpy(this->buffer, data.constData(), file_size);
        file.close();
    } else {
        QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("Can't open ROM image file '%1'").arg(file_name));
        throw QException();
    }
}

//----------------------- class Port -------------------------------//

Port::Port(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd),
    value(0)
{
    try {
        this->size = parse_numeric_value(this->cd->get_parameter("size").value);
    } catch (QException &e) {
        this->size = 8;
    }

    try {
        this->flip_mask = parse_numeric_value(this->cd->get_parameter("flipmask").value);
    } catch (QException &e) {
        this->flip_mask = (unsigned int)(-1);
    }

    this->i_input = this->create_interface(this->size, "data", MODE_R);
    this->i_data = this->create_interface(this->size, "value", MODE_W);

    this->i_access = this->create_interface(1, "access", MODE_W);
    this->i_flip = this->create_interface(1, "flip", MODE_R, 1);

}

void Port::interface_callback([[maybe_unused]] unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    //Flip interface only
    if ((old_value & 1) != 0 && (new_value & 1) == 0) this->set_value(0, this->value ^ this->flip_mask);
}

unsigned int Port::get_value([[maybe_unused]] unsigned int address)
{
    return this->value;
}

void Port:: set_value([[maybe_unused]] unsigned int address, unsigned int value)
{
    this->i_access->change(0);
    this->value = value;
    this->i_data->change(value);
    this->i_access->change(1);
}

void Port::reset([[maybe_unused]] bool cold)
{
    this->set_value(0, 0);
}

//----------------------- class PortAddress -------------------------------//

PortAddress::PortAddress(InterfaceManager *im, EmulatorConfigDevice *cd):
    Port(im, cd)
{}

void PortAddress::set_value(unsigned int address, [[maybe_unused]] unsigned int value)
{
    this->i_access->change(0);
    this->value = address;
    this->i_data->change(address);
    this->i_access->change(1);
}

//----------------------- class CPU -------------------------------//

CPU::CPU(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    reset_mode(true),
    debug(DEBUG_OFF),
    break_count(0)

{
    try {
        clock = parse_numeric_value(this->cd->get_parameter("clock").value);
    } catch (QException &e) {
        QMessageBox::critical(0, CPU::tr("Error"), CPU::tr("No CPU clock value found"));
    }
}

void CPU::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);
    this->mm = (MemoryMapper*)this->im->dm->get_device_by_name("mapper");
}

bool CPU::check_breakpoint(unsigned int address)
{
    //TODO: Implement
}

void CPU::add_breakpoint(unsigned int address)
{
    //TODO: Implement
}

void CPU::remove_breakpoint(unsigned int address)
{
    //TODO: Implement
}

void CPU::clear_breakpoints()
{
    //TODO: Implement
}

void CPU::reset(bool cold)
{
    this->reset_mode = true;
}

//----------------------- class MemoryMapper -------------------------------//

MemoryMapper::MemoryMapper(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    //TODO: Implement
}

void MemoryMapper::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);

    //TODO: Implement
}

void MemoryMapper::reset(bool cold)
{
    //TODO: Implement
}

//----------------------- Creation functions -------------------------------//

ComputerDevice * create_ram(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new RAM(im, cd);
}

ComputerDevice * create_rom(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new ROM(im, cd);
}

ComputerDevice * create_memory_mapper(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new MemoryMapper(im, cd);
}

ComputerDevice * create_port(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Port(im, cd);
}

ComputerDevice * create_port_address(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new PortAddress(im, cd);
}
