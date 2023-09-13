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
    bool result = ((this->value & 1) != 0) && ((this->edge_value & 1) == 0);
    this->edge_value = this->value;
    return result;
}

bool Interface::neg_edge()
{
    bool result = ((this->value & 1) == 0) && ((this->edge_value & 1) != 0);
    this->edge_value = this->value;
    return result;
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
    for (unsigned int i=0; i < this->device_count; i++)
        delete this->devices[i].device;

    this->device_count = 2;

    memset(&this->devices, 0, sizeof(this->devices));
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

    //this->device_count++;
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
    for (unsigned int i=0; i < this->device_count; i++)
        if (this->devices[i].device->name == name)
            return i;

    this->error(nullptr, DeviceManager::tr("Device %1 not found").arg(name));
    return (unsigned int)(-1);
}

void DeviceManager::reset_devices(bool cold)
{
    for (unsigned int i=0; i < this->device_count; i++)
        this->devices[i].device->reset(cold);
}

void DeviceManager::clock(unsigned int counter)
{
    //Except CPU
    for (unsigned int i=1; i < this->device_count; i++)
        this->devices[i].device->clock(counter);
}

void DeviceManager::error(ComputerDevice *d, QString message)
{
    this->error_device = d;
    this->error_message = message;
    throw QException();
}

void DeviceManager::error_clear()
{
    this->error_device = nullptr;
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
    for (unsigned int i=0; i < this->interfaces_count; i++)
        delete this->interfaces[i];

    this->interfaces_count = 0;

    memset(&this->interfaces, 0, sizeof(this->interfaces));
}

Interface * InterfaceManager::get_interface_by_name(QString device_name, QString interface_name)
{
    for (unsigned int i=0; i<this->interfaces_count; i++)
        if (this->interfaces[i]->device->name == device_name && this->interfaces[i]->name == interface_name)
            return this->interfaces[i];

    QMessageBox::critical(0, InterfaceManager::tr("Error"), InterfaceManager::tr("Interface '%1:%2' not found").arg(device_name, interface_name));
    return nullptr;
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

void ComputerDevice::load_config([[maybe_unused]] SystemData * sd)
{
    LinkData ld;

    for (unsigned int i = 0; i < this->cd->parameters_count; i++)
    {
        QString parameter_name = this->cd->parameters[i].name;
        qDebug() << this->name << " " << parameter_name;
        if (parameter_name.at(0) == '~')
        {
            QString interface_name = parameter_name.removeFirst();
            if (interface_name.isEmpty()) QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect interface definition for %1").arg(this->name));

            ld.s.i = this->im->get_interface_by_name(this->name, interface_name);

            QString connection = this->cd->parameters[i].value;
            if (connection.isEmpty()) QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect connection for %1:%2").arg(this->name, connection));

            int p = connection.indexOf('.');
            if (p < 0) QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect connection for %1:%2").arg(this->name, connection));
            QString connected_device = connection.first(p);
            QString connected_interface = connection.last(connection.length()-p-1);

            ld.d.i = this->im->get_interface_by_name(connected_device, connected_interface);

            QString source_bits = this->cd->parameters[i].left_range;
            if (source_bits.isEmpty())
            {
                ld.s.shift = 0;
                ld.s.mask = create_mask(ld.s.i->get_size(), 0);
            } else {
                source_bits = source_bits.mid(1, source_bits.length()-2); //remove brackets
                unsigned int bit_1, bit_2;
                convert_range(source_bits, &bit_1, &bit_2);
                ld.s.shift = bit_1;
                ld.s.mask = create_mask(bit_2 - bit_1 + 1, bit_1);
            }

            QString dest_bits = this->cd->parameters[i].right_range;
            if (dest_bits.isEmpty())
            {
                ld.d.shift = 0;
                ld.d.mask = create_mask(ld.d.i->get_size(), 0);
            } else {
                dest_bits = dest_bits.mid(1, dest_bits.length()-2); //remove brackets
                unsigned int bit_1, bit_2;
                convert_range(dest_bits, &bit_1, &bit_2);
                ld.d.shift = bit_1;
                ld.d.mask = create_mask(bit_2 - bit_1 + 1, bit_1);
            }
            ld.s.i->connect(ld.s, ld.d);
        }
    }

}

Interface * ComputerDevice::create_interface(unsigned int size, QString name, unsigned int mode, unsigned int callback_id)
{
    return new Interface(this, im, size, name, mode, callback_id);
}

void ComputerDevice::interface_callback([[maybe_unused]] unsigned int callback_id, [[maybe_unused]] unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    //Does nothing by default, but may be overridden
}

void ComputerDevice::memory_callback([[maybe_unused]] unsigned int callback_id, [[maybe_unused]] unsigned int address)
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
    read_callback(0),
    write_callback(0)
{
    this->i_address = this->create_interface(16, "address", MODE_R, 1);
    this->i_data = this->create_interface(8, "data", MODE_W);
}

Memory::~Memory()
{
    if (this->buffer != nullptr) delete [] buffer;
}

unsigned int Memory::get_value(unsigned int address)
{
    if (this->read_callback != 0)
        this->memory_callback_device->memory_callback(this->read_callback, address);

    if (this->can_read && address<this->size)
        return this->buffer[address];
    else
        return 0xFF;
}

void Memory::set_value(unsigned int address, unsigned int value)
{
    if (this->write_callback != 0)
        this->memory_callback_device->memory_callback(this->write_callback, address);

    if (this->can_write && address < this->size)
        this->buffer[address] = (uint8_t)value;
}

void Memory::interface_callback([[maybe_unused]] unsigned int callback_id, unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    unsigned int address = new_value & create_mask(this->i_address->get_size(), 0);
    if (address < this->size and this->auto_output) this->i_data->change(this->buffer[address]);
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

void Memory::set_memory_callback(ComputerDevice * d, unsigned int callback_id, unsigned int mode)
{
    this->memory_callback_device = d;
    if ((mode & MODE_R) != 1) this->read_callback = callback_id;
    if ((mode & MODE_W) != 1) this->write_callback = callback_id;
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

    QString file_name = sd->system_path + this->cd->get_parameter("image").value;
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
    ComputerDevice(im, cd),
    ranges_count(0),
    ports_count(0),
    ports_to_mem(false),
    first_range(1),
    cancel_init_mask(0),
    read_cache_items(0),
    write_cache_items(0)

{
    this->i_address = this->create_interface(16, "address", MODE_R);
    this->i_config = this->create_interface(8, "config", MODE_R, MM_CONFIG);
}

void MemoryMapper::load_config(SystemData *sd)
{
    LinkData ld;

    ComputerDevice::load_config(sd);

    //TODO: Implement
    this->cache_size = sizeof(this->read_cache_items) / sizeof(MapperCacheEntry);

    QString config_device = this->cd->get_parameter("config", false).value;

    if (!config_device.isEmpty())
    {
        ld.d.i = this->im->get_interface_by_name(config_device, "value");
        ld.d.shift = 0;
        ld.d.mask = create_mask(ld.d.i->get_size(), 0);

        ld.s.i = this->im->get_interface_by_name(this->name, "config");
        ld.s.i->set_size(ld.d.i->get_size());
        ld.s.shift = 0;
        ld.s.mask = create_mask(ld.s.i->get_size(), 0);

        ld.s.i->connect(ld.s, ld.d);
    } else
        this->i_config->change(0);

    this->ports_to_mem = this->cd->get_parameter("portstomemory", false).value == "1";

    this->ports_mask = (this->cd->get_parameter("wideports", false).value == "1")?(unsigned int)(-1):0xFF;

    QString m = this->cd->get_parameter("cancelinit", false).value;
    this->cancel_init_mask = (!m.isEmpty())?parse_numeric_value(m):0;

    //Loading ranges
    QString parameter_name, mask, range, c, a;
    unsigned int index;
    int p;

    for (unsigned int i = 0; i < this->cd->parameters_count; i++)
    {
        parameter_name = this->cd->parameters[i].name;
        if (parameter_name == "@memory" || parameter_name == "@port")
        {
            MapperRange mr;

            range = this->cd->parameters[i].left_range;
            p = range.indexOf("][");
            if (p>=0)
            {
                c = range.left(p);
                a = range.right(range.length()-p-3);
                p = c.indexOf(":");
                if (p >= 0)
                {
                    mask = c.right(c.length()-p-1);
                    c = c.left(p);
                } else
                    mask = "";
            } else {
                c = "";
                a = range.mid(1, range.length()-2);
                mask = "";
            }

            if (c=="*")
            {
                index = 0;
                this->first_range = 0;
                c = "";
            } else {
                index = ++this->ranges_count;
            }

            try {
                mr.config_mask = parse_numeric_value(mask);
            } catch (QException &e) {
                mr.config_mask = create_mask(this->i_config->get_size(), 0);
            }

            try {
                mr.config_value = parse_numeric_value(c);
            } catch (QException &e) {
                mr.config_value = 0;
                mr.config_mask = 0;
            }

            p = a.indexOf("-");
            if (p>=0)
            {
                mr.range_begin = parse_numeric_value(a.left(p));
                mr.range_end = parse_numeric_value(a.right(a.length() - p - 1));
            } else {
                if (parameter_name == "@port")
                {
                    mr.range_begin = parse_numeric_value(a);
                    mr.range_end = mr.range_begin;
                } else
                    QMessageBox::critical(0, MemoryMapper::tr("Error"), MemoryMapper::tr("Incorrect range for '%1'").arg(parameter_name));
            }

            mr.device = this->im->dm->get_device_by_name(this->cd->parameters[i].value);

            range = this->cd->parameters[i].right_range;
            try {
               mr.base = parse_numeric_value(range.mid(1, range.length()-2));
            } catch (QException &e) {
                mr.base = 0;
            }

            m = this->cd->extended_parameter(i, "mode");
            if (m == "r") mr.mode = MODE_R;
            else if (m == "w") mr.mode = MODE_W;
            else mr.mode = MODE_RW;

            try {
                mr.address_mask = parse_numeric_value(this->cd->extended_parameter(i, "addr_mask"));
                mr.address_value = parse_numeric_value(this->cd->extended_parameter(i, "addr_value"));
            } catch (QException &e) {
                mr.address_mask = 0;
                mr.address_value = 0;
            }

            //Disable cache for complicated entries
            mr.cache = (mr.address_mask == 0) && (this->cache_size > 0);

            if (parameter_name == "@memory")
                this->ranges[index] = mr;
            else
                this->ports[this->ports_count++] = mr;
        }
    }

    //Also we need to disable cache for ranges crossing uncached ones
    if (this->cache_size > 0)
        for (unsigned int i = this->first_range; i < this->ranges_count; i++)
            if (!this->ranges[i].cache)
                for (unsigned int j = this->first_range; j < this->ranges_count; j++)
                    if (
                         !(
                            (
                                (this->ranges[j].range_end < this->ranges[i].range_begin)
                                ||
                                (this->ranges[j].range_begin > this->ranges[i].range_end)
                            )
                            &&
                            (this->ranges[j].config_mask == this->ranges[i].config_mask)
                            &&
                            (this->ranges[j].config_value == this->ranges[i].config_value)
                          )
                       ) this->ranges[j].cache = false;
}

void MemoryMapper::reset([[maybe_unused]] bool cold)
{
    if (this->cancel_init_mask != 0) this->first_range = 0;
    this->read_cache_items = 0;
    this->write_cache_items = 0;
}

void MemoryMapper::interface_callback([[maybe_unused]] unsigned int callback_id, [[maybe_unused]] unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    this->read_cache_items = 0;
    this->write_cache_items = 0;
}

void MemoryMapper::add_cache_entry(MapperCacheEntry * cache, unsigned int * cache_items, MapperRange * range)
{
    //TODO: Implement
}

void MemoryMapper::sort_cache()
{
    //TODO: Implement
}

unsigned int MemoryMapper::read(unsigned int address)
{
    //TODO: Implement
}

void MemoryMapper::write(unsigned int address, unsigned int value)
{
    //TODO: Implement
}

unsigned int MemoryMapper::read_port(unsigned int address)
{
    //TODO: Implement
}

void MemoryMapper::write_port(unsigned int address, unsigned int value)
{
    //TODO: Implement
}


//----------------------- class Display -------------------------------//

Display::Display(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
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
