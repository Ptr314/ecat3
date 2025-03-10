#include <QException>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    #include <QRandomGenerator>
#endif

#include <SDL.h>

#include "core.h"
#include "emulator/utils.h"

MapperCacheEntry MapperCache[15];

#define PORT_FLIP  1
#define PORT_RESET 2

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

void Interface::connect(LinkedInterface s, LinkedInterface d, bool invert)
{
    int index = -1;

    for (unsigned int i=0; i < linked; i++)
        if (linked_interfaces[i].d.i == d.i) index = i;

    if (index < 0)
    {
#ifdef LOG_INTERFACES
        qDebug() << "CONNECT " + s.i->device->name +":" + s.i->name + " TO " + d.i->device->name +":" + d.i->name;
#endif
        linked++;
        linked_interfaces[linked-1].s = s;
        linked_interfaces[linked-1].d = d;
        linked_interfaces[linked-1].inversion = invert?_FFFF:0;
        d.i->connect(d, s, invert);
        linked_bits |= s.mask;
    }
}

void Interface::set_size(unsigned int new_size)
{
    size = new_size;
    mask = create_mask(new_size, 0);
}

unsigned int Interface::Interface::get_size()
{
    return size;
}

void Interface::set_mode(unsigned int new_mode)
{
    unsigned int prev_mode = mode;
    mode = new_mode;
    //Если интерфейс переключился с вывода на ввод, нужно правильно
    //установить его значение, если оно контролируется другим интерфейсом.
    //Поэтому просматриваем все соединенные интерфейсы, и если один из них
    //находится в режиме вывода, то имитируем установку его значения,
    //чтобы правильно выставились значения на текущем интерфейсе
    if (new_mode == MODE_R && prev_mode == MODE_W)
    {
        for (unsigned int i=0; i>linked; i++)
        {
            Interface * li = linked_interfaces[i].d.i;
            if (li->mode == MODE_W) li->change(li->value);
        }
    }
    if (new_mode == MODE_OFF) change(_FFFF);
}

unsigned int Interface::get_mode()
{
    return mode;
}


void Interface::change(unsigned int new_value)
{
    if (mode == MODE_W)
    {
        old_value = value;
        value = new_value;
        for (unsigned int i=0; i < linked; i++)
        {
            linked_interfaces[i].d.i->changed(linked_interfaces[i], new_value ^ linked_interfaces[i].inversion);
        }
    } else {
        if (mode == MODE_OFF)
        {
            im->dm->error(device, Interface::tr("Interface '%1' is in OFF state, writing is impossible").arg(name));
        }
    }
}

void Interface::changed(LinkData link, unsigned int value)
{
    if (mode == MODE_R)
    {
        unsigned int new_value = this->value & ~link.d.mask; //Set expected bits to 0
        new_value |=  ((value & link.s.mask) >> link.s.shift) << link.d.shift;
        this->old_value = this->value;
        this->value = new_value;

        if (callback_id > 0) device->interface_callback(callback_id, new_value, this->old_value);
    }
}

void Interface::clear()
{
    change(_FFFF);
}

bool Interface::pos_edge()
{
    bool result = ((value & 1) != 0) && ((edge_value & 1) == 0);
    edge_value = value;
    return result;
}

bool Interface::neg_edge()
{
    bool result = ((value & 1) == 0) && ((edge_value & 1) != 0);
    edge_value = value;
    return result;
}

void Interface::pull(unsigned int new_value)
{
    value = new_value;
}

//----------------------- class DeviceManager -------------------------------//

DeviceManager::DeviceManager(Logger *l)
{
    registered_devices_count = 0;

    device_count = 2;
    error_message = "";
    error_device = nullptr;

    logger = l;
}

DeviceManager::~DeviceManager()
{
    clear();
}

void DeviceManager::clear()
{
    for (unsigned int i=0; i < device_count; i++)
       delete devices[i].device;

    device_count = 2;

    //memset(&devices, 0, sizeof(devices));
}

void DeviceManager::register_device(QString device_type, CreateDeviceFunc func)
{
    registered_devices[registered_devices_count].type = device_type;
    registered_devices[registered_devices_count].create_func = func;

    registered_devices_count++;
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
        index = device_count++;

    CreateDeviceFunc create_func = nullptr;
    for (unsigned int i=0; i < registered_devices_count; i++)
    {
        if (registered_devices[i].type == d->type) create_func = registered_devices[i].create_func;
    }

    if (create_func != nullptr)
    {
        devices[index].device_type = d->type;
        devices[index].device_name = name;
        devices[index].device = create_func(im, d);
    } else
        QMessageBox::critical(0, DeviceManager::tr("Error"), DeviceManager::tr("Can't create device %1:%2").arg(name, d->type));
}

DeviceDescription * DeviceManager::get_device(unsigned int i)
{
    return &(devices[i]);
}

void DeviceManager::load_devices_config(SystemData *sd)
{
    for (unsigned int i=0; i < device_count; i++)
    {
        get_device(i)->device->load_config(sd);
    }
}

ComputerDevice * DeviceManager::get_device_by_name(QString name, bool required)
{
    for (unsigned int i=0; i < device_count; i++)
    {
        if (devices[i].device->name == name) return devices[i].device;
    }
    if (required)
    {
        qDebug() << "Exception: DeviceManager::get_device_by_name " << name;
        throw QException();
    } else
        return nullptr;
}

unsigned int DeviceManager::get_device_index(QString name)
{
    for (unsigned int i=0; i < device_count; i++)
        if (devices[i].device->name == name)
            return i;

    error(nullptr, DeviceManager::tr("Device %1 not found").arg(name));
    return (unsigned int)(-1);
}

void DeviceManager::reset_devices(bool cold)
{
    int devlist[MAX_DEVICES];
    for (int i=0; i < device_count; i++)
        devlist[i] = i;

    for (int i=0; i< device_count; i++)
        for (int j=0; j < device_count - i - 1; j++)
            if (devices[devlist[j]].device->reset_priority > devices[devlist[j+1]].device->reset_priority) {
                int t = devlist[j];
                devlist[j] = devlist[j+1];
                devlist[j+1] = t;
            }

    for (unsigned int i=0; i < device_count; i++)
        devices[devlist[i]].device->reset(cold);
}

void DeviceManager::clock(unsigned int counter)
{
    //Except CPU
    for (unsigned int i=1; i < device_count; i++)
        devices[i].device->system_clock(counter);
}

void DeviceManager::error(ComputerDevice *d, QString message)
{
    error_device = d;
    error_message = message;
    qDebug() << "Exception DeviceManager::error " << d->name << " " << message;
    throw QException();
}

void DeviceManager::error_clear()
{
    error_device = nullptr;
}

void DeviceManager::logs(QString s)
{
    qDebug() << s;
    if (logger != nullptr)
    {
        CPU * cpu = dynamic_cast<CPU*>(get_device(0)->device);

        if (cpu != nullptr)
        {
            QString out = QString("%1: ").arg(cpu->get_pc(), 4, 16, QChar('0')) + s;
            logger->logs(out);
        } else
            logger->logs(s);
    }
}

bool DeviceManager::log_available()
{
    return (logger != nullptr) && logger->log_available();
}

QVector<ComputerDevice*> DeviceManager::find_devices_by_class(QString class_to_find)
{
    QVector<ComputerDevice*> found;

    for (unsigned int i=0; i < device_count; i++)
    {
        if (devices[i].device->belongs_to_class(class_to_find)) found.append(devices[i].device);
    }

    return found;
}

//----------------------- class InterfaceManager -------------------------------//

InterfaceManager::InterfaceManager(DeviceManager *dm):interfaces_count(0), dm(dm){}

InterfaceManager::~InterfaceManager()
{
    clear();
}

void InterfaceManager::register_interface(Interface *i)
{
    interfaces[interfaces_count++] = i;
}

void InterfaceManager::clear()
{
    // Interfaces are now static, and are destroyed along with their owners.
    // for (unsigned int i=0; i < interfaces_count; i++)
    //     delete interfaces[i];

    interfaces_count = 0;

    memset(&interfaces, 0, sizeof(interfaces));
}

Interface * InterfaceManager::get_interface_by_name(QString device_name, QString interface_name, bool required)
{
    for (unsigned int i=0; i<interfaces_count; i++)
        if (interfaces[i]->device->name == device_name && interfaces[i]->name == interface_name)
            return interfaces[i];
    if (required)
        QMessageBox::critical(0, InterfaceManager::tr("Error"), InterfaceManager::tr("Interface '%1:%2' not found").arg(device_name, interface_name));
    return nullptr;
}

//----------------------- class ComputerDevice -------------------------------//

ComputerDevice::ComputerDevice(InterfaceManager *im, EmulatorConfigDevice *cd):
    type(cd->type),
    name(cd->name),
    device_class("generic_device"),
    cd(cd),
    im(im),
    reset_priority(0)
{
    try {
        QString s = cd->get_parameter("clock").value;
        if (s.isEmpty())
        {
            QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect clock value for '%1'").arg(name));
            throw QException();
        } else {
            int pos = s.indexOf("/");
            if (pos>0) {
                clock_miltiplier = parse_numeric_value(s.left(pos));
                clock_divider = parse_numeric_value(s.right(s.length()-pos-1));
            } else {
                clock_miltiplier = parse_numeric_value(s);
                clock_divider = 1;
            }
        }
    } catch (QException &e) {
        clock_miltiplier = 1;
        clock_divider = 1;
    }

    clock_stored = 0;
}

void ComputerDevice::clock([[maybe_unused]] unsigned int counter)
{
    //Does nothing by default, but may be overridden
}

void ComputerDevice::system_clock(unsigned int counter)
{
    if (clock_miltiplier == clock_divider)
        clock(counter);
    else {
        clock_stored += counter * clock_miltiplier;
        unsigned int internal_clock = clock_stored / clock_divider;
        if (internal_clock > 0)
        {
            clock(internal_clock);
            clock_stored -= internal_clock * clock_divider;
        }
    }
}

void ComputerDevice::load_config([[maybe_unused]] SystemData * sd)
{

    for (unsigned int i = 0; i < cd->parameters_count; i++)
    {
        QString parameter_name = cd->parameters[i].name;
        if (parameter_name.at(0) == '~')
        {

            QString interface_name = parameter_name.remove(0, 1);
            if (interface_name.isEmpty()) QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect interface definition for %1").arg(name));

            QString connection = cd->parameters[i].value;
            Interface * interface = im->get_interface_by_name(name, interface_name);

            try {
                unsigned int pull_value = parse_numeric_value(connection);
                interface->pull(pull_value);
                continue;
            } catch (QException &e) {}

            LinkData ld;
            bool inverted;

            ld.s.i = interface;

            if (connection.isEmpty()) QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect connection for %1:%2").arg(name, connection));

            if (connection.at(0) == '!') {
                connection = connection.right(connection.length()-1);
                inverted = true;
            } else
                inverted = false;

            int p = connection.indexOf('.');
            if (p < 0) QMessageBox::critical(0, ComputerDevice::tr("Error"), ComputerDevice::tr("Incorrect connection for %1:%2").arg(name, connection));
            QString connected_device = connection.left(p);
            QString connected_interface = connection.right(connection.length()-p-1);

            ld.d.i = im->get_interface_by_name(connected_device, connected_interface);

            QString source_bits = cd->parameters[i].left_range;
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

            QString dest_bits = cd->parameters[i].right_range;
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
            ld.s.i->connect(ld.s, ld.d, inverted);

        }
    }
#ifdef LOGGER
    log_mm = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper"));
#endif
}

// Interface * ComputerDevice::create_interface(unsigned int size, QString name, unsigned int mode, unsigned int callback_id)
// {
//     return new Interface(this, im, size, name, mode, callback_id);
// }

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

void ComputerDevice::logs(QString s)
{
    im->dm->logs(this->name + ": " + s);
}

bool ComputerDevice::log_available()
{
    return im->dm->log_available();
}

bool ComputerDevice::belongs_to_class(QString class_to_check)
{
    return device_class == class_to_check;
}


//----------------------- class AddressableDevice -------------------------------//

unsigned int AddressableDevice::get_size()
{
    return this->addresable_size;
}

//----------------------- class Memory -------------------------------//

Memory::Memory(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , auto_output(false)
    , buffer(nullptr)
    , fill(0)
    , random_fill(false)
    , read_callback(0)
    , write_callback(0)
    , i_address(this, im, 16, "address", MODE_R, 1)
    , i_data(this, im, 8, "data", MODE_W)
{
}

Memory::~Memory()
{
    if (buffer != nullptr) delete [] buffer;
}

unsigned int Memory::get_value(unsigned int address)
{
    if (read_callback != 0)
        memory_callback_device->memory_callback(read_callback, address);

    if (can_read && address < get_size())
        return buffer[address];
    else
        return 0xFF;
}

void Memory::set_value(unsigned int address, unsigned int value, bool force)
{
    if (write_callback != 0)
        memory_callback_device->memory_callback(write_callback, address);

    if ((can_write || force) && address < get_size())
        buffer[address] = (uint8_t)value;
}

void Memory::interface_callback([[maybe_unused]] unsigned int callback_id, unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    unsigned int address = new_value & create_mask(i_address.get_size(), 0);
    if (address < get_size() and auto_output) i_data.change(buffer[address]);

    // if (name == "rom-card-mapper")
    //     logs(QString::number(address, 2) + QString::number(buffer[address], 2));
}

void Memory::set_size(unsigned int value)
{
    if (buffer != nullptr) delete [] buffer;
    buffer = new uint8_t[value];
    addresable_size = value;

    i_address.set_size(ceil(log2(addresable_size)));

    QRandomGenerator *rg = QRandomGenerator::global();

    for (unsigned int i=0; i < value; i++) buffer[i]=rg->bounded(255);
}

void Memory::set_memory_callback(ComputerDevice * d, unsigned int callback_id, unsigned int mode)
{
    memory_callback_device = d;
    if ((mode & MODE_R) != 0) read_callback = callback_id;
    if ((mode & MODE_W) != 0) write_callback = callback_id;
}

uint8_t * Memory::get_buffer()
{
    return buffer;
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
    set_size(parse_numeric_value(this->cd->get_parameter("size").value));

    try {
        QString s = cd->get_parameter("fill").value.toLower();
        random_fill = (s == "random");
        if (!random_fill) fill = parse_numeric_value(s);
    } catch (QException &e) {
        fill = 0;
    }
    //memset(buffer, fill, get_size());

    reset(true);
}

void RAM::reset(bool cold)
{
    if (cold && buffer!=nullptr) {
        if (!random_fill) {
            memset(buffer, fill, get_size());
        } else {
            QRandomGenerator *rg = QRandomGenerator::global();
            for (unsigned int i=0; i < get_size(); i++) buffer[i]=rg->bounded(255);
        }
    }
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

    try {
        this->fill = parse_numeric_value(this->cd->get_parameter("fill").value);
    } catch (QException &e) {
        this->fill = 0xFF;
    }

    QString image = cd->get_parameter("image", false).value;

    if (!image.isEmpty()) {

        set_size(parse_numeric_value(cd->get_parameter("size").value));
        if (buffer!=nullptr) memset(buffer, fill, get_size());

        QString file_name = find_file_location(sd, image);
        if (file_name.isEmpty())
            QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("File '%1' not found").arg(file_name));
        else
        if (file_name.endsWith(".hex", Qt::CaseInsensitive))
        {
            QFile file(file_name);
            if (!file.open(QIODevice::ReadOnly)) {
                QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("Error reading HEX file %1").arg(file_name));
                return;
            }

            unsigned int index = 0;
            QTextStream in(&file);
            while (!in.atEnd())
            {
                QString line = in.readLine();
                unsigned int len = parse_numeric_value("$" + line.mid(1, 2));
                unsigned int addr = parse_numeric_value("$" + line.mid(3, 4));
                unsigned int type = parse_numeric_value("$" + line.mid(7, 2));
                //qDebug() << len << Qt::hex << addr << type;
                if (type == 0)
                {
                    for (unsigned int j=0; j< len; j++)
                        buffer[index+j] = parse_numeric_value("$" + line.mid(9+j*2, 2));
                    index += len;
                }
                //QStringList parts = line.split(u'\x09', Qt::SkipEmptyParts);
            }
            file.close();
        } else {
            QFile file(file_name);
            if (file.open(QIODevice::ReadOnly)){
                unsigned int file_size = file.size();
                if (file_size > this->get_size())
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
    } else {
        QString data_str = cd->get_parameter("data").right_extended;
        QStringList values = data_str.split(',', Qt::SkipEmptyParts);
        set_size(values.size());
        for (int i=0; i<values.size(); i++)
            buffer[i] = parse_numeric_value(values.at(i));
    }
}

//----------------------- class Port -------------------------------//

Port::Port(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , default_value(0)
    , mask(_FFFF)
    , i_input(this, im, 8, "data", MODE_R)
    , i_data(this, im, 8, "value", MODE_W)
    , i_access(this, im, 1, "access", MODE_W)
    , i_flip(this, im, 1, "flip", MODE_R, PORT_FLIP)
    , i_reset(this, im, 1, "reset", MODE_R, PORT_RESET)

{
    try {
        size = parse_numeric_value(this->cd->get_parameter("size").value);
    } catch (QException &e) {
        size = 8;
    }

    i_input.set_size(size);
    i_data.set_size(size);

    try {
        default_value = parse_numeric_value(this->cd->get_parameter("default").value);
    } catch (QException &e) {
        default_value = 0;
    }

    try {
        flip_mask = parse_numeric_value(cd->get_parameter("flipmask").value);
    } catch (QException &e) {
        flip_mask = _FFFF;
    }

    try {
        mask = parse_numeric_value(cd->get_parameter("mask").value);
    } catch (QException &e) {
        mask = _FFFF;
    }

    try {
        constant_value = parse_numeric_value(cd->get_parameter("constant_return").value);
        has_constant_return = true;
    } catch (QException &e) {
        has_constant_return = false;
    }

    value = default_value;
}

void Port::interface_callback([[maybe_unused]] unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
        case PORT_FLIP:
            if ((old_value & 1) != 0 && (new_value & 1) == 0) set_value(0, value ^ flip_mask);
            break;
        default: // PORT_RESET
            if ((old_value & 1) != 0 && (new_value & 1) == 0) this->value = default_value;
            break;
    }
}

unsigned int Port::get_value([[maybe_unused]] unsigned int address)
{
#ifdef LOG_PORTS
    if (name != "port-video" && name != "port-kbd")
        logs("GET " + QString::number(value, 16));
#endif
    i_access.change(0);
    i_access.change(1);
    if (!has_constant_return) return value;
    else return constant_value;
}

unsigned int Port::get_direct()
{
    return value;
}

void Port::set_value([[maybe_unused]] unsigned int address, unsigned int value, bool force)
{
#ifdef LOG_PORTS
    logs(QString("SET %1=%2").arg(address, 2, 16, QChar('0')).arg(value, 2, 16, QChar('0')));
#endif
    i_access.change(0);
    this->value = (value & mask) | (this->value & ~mask);
    i_data.change(this->value);
    i_access.change(1);
}

void Port::reset([[maybe_unused]] bool cold)
{
    set_value(0, default_value);
}

//----------------------- class PortAddress -------------------------------//

PortAddress::PortAddress(InterfaceManager *im, EmulatorConfigDevice *cd):
    Port(im, cd)
{
    try {
        store_on_read = read_confg_value(cd, "store_on_read", false, false);
    } catch (QException &e) {
        QMessageBox::critical(0, PortAddress::tr("Error"), PortAddress::tr("Incorrect parameters for '%1'").arg(name));
    }
}

void PortAddress::set_value(unsigned int address, [[maybe_unused]] unsigned int value, bool force)
{
#ifdef LOG_PORTS
    logs(QString("SET %1=%2").arg(address, 2, 16, QChar('0')).arg(value, 2, 16, QChar('0')));
    if (address == 0xC1) {
        int i=0;
        i++;
    }
#endif
    i_access.change(0);
    this->value = (address & mask) | (this->value & ~mask);
    i_data.change(this->value);
    i_access.change(1);
}

unsigned int PortAddress::get_value([[maybe_unused]] unsigned int address)
{
    if (store_on_read) {
        this->value = (address & mask) | (this->value & ~mask);
        i_data.change(this->value);
    }
    return Port::get_value(address);
}


void PortAddress::reset([[maybe_unused]] bool cold)
{
    set_value(default_value, 0);
}


//----------------------- class CPU -------------------------------//

CPU::CPU(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , reset_mode(true)
    , i_address(this, im, 16, "address", MODE_W, 1)
    , i_data(this, im, 8, "data", MODE_RW)
#ifdef CPU_STOPPED
    , debug(DEBUG_STOPPED)
#else
    , debug(DEBUG_OFF)
#endif
    , break_count(0)
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

    QString s = cd->get_parameter("stopped", false).value;
    if (s=="1") debug = (DEBUG_STOPPED);

    mm = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper"));
}

bool CPU::check_breakpoint(unsigned int address)
{
    for (unsigned int i=0; i<break_count; i++)
        if (breakpoints[i] == address) return true;

    return false;
}

void CPU::add_breakpoint(unsigned int address)
{
    if (!check_breakpoint(address))
        if (break_count < sizeof(breakpoints) / sizeof(breakpoints[0]))
            breakpoints[break_count++] = address;
}

void CPU::remove_breakpoint(unsigned int address)
{
    for (unsigned int i=0; i<break_count; i++)
        if (breakpoints[i] == address)
        {
            for (unsigned int j=i; j<break_count-1; j++)
                breakpoints[j] = breakpoints[j+1];
            break_count--;
            return;
        }
}

void CPU::clear_breakpoints()
{
    break_count = 0;
}

void CPU::reset(bool cold)
{
    this->reset_mode = true;
}

//----------------------- class MemoryMapper -------------------------------//

MemoryMapper::MemoryMapper(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , ranges_count(0)
    , ports_count(0)
    , ports_to_mem(false)
    , first_range(1)
    , cancel_init_mask(0)
    , read_cache_items(0)
    , write_cache_items(0)
    , i_address(this, im, 16, "address", MODE_R)
    , i_config(this, im, 8, "config", MODE_R, MM_CONFIG)

{

    addresable_size = 0x10000;
}

void MemoryMapper::load_config(SystemData *sd)
{
    LinkData ld;

    ComputerDevice::load_config(sd);

    this->cache_size = sizeof(this->read_cache_items) / sizeof(MapperCacheEntry);

    QString config_device = this->cd->get_parameter("config", false).value;

    if (!config_device.isEmpty())
    {
        Interface * i_cfg = im->get_interface_by_name(config_device, "out", false);
        if (i_cfg == nullptr)
            i_cfg = im->get_interface_by_name(config_device, "value");

        ld.d.i = i_cfg;
        ld.d.shift = 0;
        ld.d.mask = create_mask(ld.d.i->get_size(), 0);

        ld.s.i = this->im->get_interface_by_name(this->name, "config");
        ld.s.i->set_size(ld.d.i->get_size());
        ld.s.shift = 0;
        ld.s.mask = create_mask(ld.s.i->get_size(), 0);

        ld.s.i->connect(ld.s, ld.d);
    } else
        this->i_config.change(0);

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
                c = range.mid(1, p-1);
                a = range.mid(p+2, range.length()-p-3);
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
                mr.config_mask = create_mask(this->i_config.get_size(), 0);
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
        for (unsigned int i = this->first_range; i <= this->ranges_count; i++)
            if (!this->ranges[i].cache)
                for (unsigned int j = this->first_range; j <= this->ranges_count; j++)
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
    //TODO: Cache
}

void MemoryMapper::sort_cache()
{
    //TODO: Cache
}

AddressableDevice * MemoryMapper::map(
                                        MapperArray * map_ranges,
                                        unsigned int index_from,
                                        unsigned int index_to,
                                        unsigned int config,
                                        unsigned int address,
                                        unsigned int mode,
                                        unsigned int * address_on_device,
                                        unsigned int * range_index
                                    )
{
    for (unsigned int i = index_from; i <= index_to; i++)
    {
        MapperRange * mr = &(*map_ranges)[i];
        if (
            ( (config & mr->config_mask) == mr->config_value )
            &&
            (address >= mr->range_begin)
            &&
            (address <= mr->range_end)
            &&
            ( (address & mr->address_mask) == mr->address_value)
            &&
            ( (mr->mode & mode) != 0)
            )
        {
            * address_on_device = address - mr->range_begin + mr->base;
            * range_index = i;
            return dynamic_cast<AddressableDevice*>(mr->device);
        }
    }
    return nullptr;
}

AddressableDevice * MemoryMapper::map_memory(
                                                unsigned int config,
                                                unsigned int address,
                                                unsigned int mode,
                                                unsigned int * address_on_device,
                                                unsigned int * range_index
                                            )
{
    return this->map(&(this->ranges), this->first_range, this->ranges_count, config, address, mode, address_on_device, range_index);
}

AddressableDevice * MemoryMapper::map_port(
                                                unsigned int config,
                                                unsigned int address,
                                                unsigned int mode,
                                                unsigned int * address_on_device,
                                                unsigned int * range_index
                                            )
{
    return this->map(&(this->ports), 0, this->ports_count-1, config, address, mode, address_on_device, range_index);
}

unsigned int MemoryMapper::read(unsigned int address)
{
    //TODO: Cache
    // for (unsigned int i = 0; i < this->read_cache_items; i++)
#ifdef LOG_MAPPER
    if (address >= 0xE000 && address < 0xF800) logs(QString("R %1").arg(address, 4, 16, QChar('0')));
#endif

    if ((this->first_range == 0) && ((address & this->cancel_init_mask) != 0))
    {
        this->first_range = 1;
        this->read_cache_items = 0;
        this->write_cache_items = 0;
    }

    unsigned int address_on_device, range_index;
    AddressableDevice * d = this->map(&(this->ranges), this->first_range, this->ranges_count, this->i_config.value, address, MODE_R, &address_on_device, &range_index);
    if (d != nullptr)
    {
        //TODO: Cache
        //if (mr->cache) this->add_cache_entry(); //this->ranges[range_index]
        return d->get_value(address_on_device);

    } else
        return _FFFF;
}

void MemoryMapper::write(unsigned int address, unsigned int value)
{
    //TODO: Cache
    // for (unsigned int i = 0; i < this->write_cache_items; i++)

#ifdef LOG_MAPPER
    if (address >= 0xE000 && address <= 0xFFFF) logs(QString("W %1").arg(address, 4, 16, QChar('0')));
#endif

    unsigned int address_on_device, range_index;
    AddressableDevice * d = this->map(&(this->ranges), this->first_range, this->ranges_count, this->i_config.value, address, MODE_W, &address_on_device, &range_index);
    if (d != nullptr)
    {
        //TODO: Cache
        //if (mr->cache) this->add_cache_entry(); //this->ranges[range_index]
        d->set_value(address_on_device, value);
    }
}

unsigned int MemoryMapper::read_port(unsigned int address)
{
    if (this->ports_to_mem) {
        return(this->read(address));
    } else {
        unsigned int a = address & this->ports_mask;
        unsigned int address_on_device, range_index;
        AddressableDevice * d = this->map(&(this->ports), 0, this->ports_count-1, this->i_config.value, a, MODE_R, &address_on_device, &range_index);
        if (d != nullptr)
        {
            return d->get_value(address_on_device);

        } else
            return _FFFF;
    }
}

void MemoryMapper::write_port(unsigned int address, unsigned int value)
{
    if (this->ports_to_mem) {
        this->write(address, value);
    } else {
        unsigned int a = address & this->ports_mask;
        unsigned int address_on_device, range_index;
        AddressableDevice * d = this->map(&(this->ports), 0, this->ports_count-1, this->i_config.value, a, MODE_R, &address_on_device, &range_index);
        if (d != nullptr)
        {
            d->set_value(address_on_device, value);
        }
    }
}

unsigned int MemoryMapper::get_value(unsigned int address)
{
    return read(address);
}

void MemoryMapper::set_value(unsigned int address, unsigned int value, bool force)
{
    write(address, value);
}


//----------------------- class Display -------------------------------//

GenericDisplay::GenericDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    sx(0),
    sy(0),
    //texture(nullptr),
    surface(nullptr),
    screen_valid(false),
    was_updated(true),
    render_pixels(nullptr)
{}

void GenericDisplay::set_surface(SDL_Surface * surface)
{
    this->surface = surface;
    render_pixels = surface->pixels;
    line_bytes = sx*4;
}


void GenericDisplay::validate(bool force_render)
{
    if (!screen_valid || force_render) render_all(force_render);
}

void GenericDisplay::reset(bool cold)
{
    screen_valid = false;
    was_updated = true;
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
