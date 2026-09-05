// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Emulator core classes, source

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include "dsk_tools/dsk_tools.h"
// MSVC resolves the "utils.h" inside dsk_tools.h against the includer's directory,
// where it finds emulator/utils.h. Pull in the real one explicitly.
#include "libs/dsk_tools/src/utils.h"

#ifdef RENDERER_SDL2
    #include <SDL.h>
#endif

#include "core.h"
#include "emulator/utils.h"

MapperCacheEntry MapperCache[15];

#define PORT_FLIP  1
#define PORT_RESET 2
#define PORT_INPUT 3

//----------------------- class Interface -------------------------------//

Interface::Interface(
    ComputerDevice * device,
    InterfaceManager * im,
    unsigned int size,
    const std::string &name,
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
        for (unsigned int i=0; i<linked; i++)
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
            im->dm->error(device, "Interface '" + name + "' is in OFF state, writing is impossible");
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

DeviceManager::DeviceManager()
{
    registered_devices_count = 0;

    device_count = 2;
    error_message = "";
    error_device = nullptr;

}

DeviceManager::~DeviceManager()
{
    clear();
}

void DeviceManager::clear()
{
    for (unsigned int i=0; i < device_count; i++)
       devices[i].device.reset();  // unique_ptr handles deletion automatically

    device_count = 2;

    //memset(&devices, 0, sizeof(devices));
}

void DeviceManager::register_device(const std::string &device_type, CreateDeviceFunc func)
{
    registered_devices[registered_devices_count].type = device_type;
    registered_devices[registered_devices_count].create_func = func;

    registered_devices_count++;
}

emulator::Result DeviceManager::add_device(InterfaceManager *im, EmulatorConfigDevice *d)
{
    unsigned int index;
    if (d->name == "cpu")
        index=0;
    else
    if (d->name == "mapper")
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
        devices[index].device_name = d->name;
        devices[index].device.reset(create_func(im, d));  // Wrap raw pointer in unique_ptr
    } else
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DeviceManager|" + std::string(QT_TRANSLATE_NOOP("DeviceManager", "Can't create device")) + "} " + d->name + ":" + d->type);

    return emulator::Result::ok();
}

DeviceDescription * DeviceManager::get_device(unsigned int i)
{
    return &(devices[i]);
}

emulator::Result DeviceManager::load_devices_config(SystemData *sd)
{
    for (unsigned int i=0; i < device_count; i++)
    {
        emulator::Result res = get_device(i)->device->load_config(sd);
        if (!res) return res;
    }
    return emulator::Result::ok();
}

ComputerDevice * DeviceManager::get_device_by_name(const std::string &name, bool required)
{
    for (unsigned int i=0; i < device_count; i++)
    {
        if (devices[i].device->name == name) return devices[i].device.get();
    }
    if (required)
    {
        std::cerr << "Exception: DeviceManager::get_device_by_name " << name << std::endl;
        throw std::runtime_error("Device not found: " + name);
    } else
        return nullptr;
}

unsigned int DeviceManager::get_device_index(const std::string &name)
{
    for (unsigned int i=0; i < device_count; i++)
        if (devices[i].device->name == name)
            return i;

    error(nullptr, "Device " + name + " not found");
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

    for (unsigned int i=0; i < device_count; i++) {
        ComputerDevice * d = devices[devlist[i]].device.get();
        if (d->get_reset_behavior(cold)) d->reset(cold);
    }
}

void DeviceManager::clock(unsigned int counter)
{
    global_clock_counter += counter;
    //Except CPU
    for (unsigned int i=1; i < device_count; i++)
        devices[i].device->system_clock(counter);
}

void DeviceManager::error(ComputerDevice *d, const std::string &message)
{
    error_device = d;
    error_message = message;
    std::cerr << "Exception DeviceManager::error " << d->name << " " << message << std::endl;
    throw std::runtime_error(message);
}

void DeviceManager::error_clear()
{
    error_device = nullptr;
}

std::vector<ComputerDevice*> DeviceManager::find_devices_by_class(const std::string &class_to_find)
{
    std::vector<ComputerDevice*> found;

    for (unsigned int i=0; i < device_count; i++)
    {
        if (devices[i].device->belongs_to_class(class_to_find)) found.push_back(devices[i].device.get());
    }

    return found;
}

//----------------------- class InterfaceManager -------------------------------//

InterfaceManager::InterfaceManager(DeviceManager *dm): dm(dm){}

InterfaceManager::~InterfaceManager()
{
    clear();
}

void InterfaceManager::register_interface(Interface *i)
{
    interfaces.push_back(i);
}

void InterfaceManager::clear()
{
    // Interfaces are now in std::vector, automatically cleaned up
    interfaces.clear();
}

Interface * InterfaceManager::get_interface_by_name(const std::string &device_name, const std::string &interface_name, bool required)
{
    for (size_t i=0; i<interfaces.size(); i++)
        if (interfaces[i]->device->name == device_name && interfaces[i]->name == interface_name)
            return interfaces[i];
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
        std::string s = cd->get_parameter("clock").value;
        if (s.empty())
        {
            throw std::runtime_error("Incorrect clock value for " + name);
        } else {
            size_t pos = s.find("/");
            if (pos != std::string::npos) {
                clock_miltiplier = parse_numeric_value(s.substr(0, pos));
                clock_divider = parse_numeric_value(s.substr(pos + 1));
            } else {
                clock_miltiplier = parse_numeric_value(s);
                clock_divider = 1;
            }
        }
    } catch (std::exception &e) {
        clock_miltiplier = 1;
        clock_divider = 1;
    }

    clock_stored = 0;
}

void ComputerDevice::clock(MAYBE_UNUSED unsigned int counter)
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

emulator::Result ComputerDevice::load_config(MAYBE_UNUSED SystemData * sd)
{
    //Remembered so that devices can locate files (see find_file_location())
    this->sd = sd;

    for (size_t i = 0; i < cd->parameters.size(); i++)
    {
        const std::string &parameter_name = cd->parameters[i].name;
        if (parameter_name[0] == '~')
        {
            std::string interface_name = parameter_name.substr(1);
            if (interface_name.empty())
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect interface definition for")) + "} " + name);

            std::string connection = cd->parameters[i].value;
            Interface * interface = im->get_interface_by_name(name, interface_name);
            if (interface == nullptr)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Interface not found")) + "} " + name + ":" + interface_name);

            try {
                unsigned int pull_value = parse_numeric_value(connection);
                interface->pull(pull_value);
                continue;
            } catch (std::exception &e) {}

            LinkData ld;
            bool inverted;

            ld.s.i = interface;

            if (connection.empty())
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect connection for")) + "} " + name + ":" + connection);

            if (connection[0] == '!') {
                connection = connection.substr(1);
                inverted = true;
            } else
                inverted = false;

            size_t p = connection.find('.');
            if (p == std::string::npos)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect connection for")) + "} " + name + ":" + connection);
            std::string connected_device = connection.substr(0, p);
            std::string connected_interface = connection.substr(p + 1);

            ld.d.i = im->get_interface_by_name(connected_device, connected_interface);
            if (ld.d.i == nullptr)
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Interface not found")) + "} " + connected_device + ":" + connected_interface);

            const std::string &source_bits = cd->parameters[i].left_range;
            if (source_bits.empty())
            {
                ld.s.shift = 0;
                ld.s.mask = create_mask(ld.s.i->get_size(), 0);
            } else {
                std::string sb = source_bits.substr(1, source_bits.length()-2); //remove brackets
                unsigned int bit_1, bit_2;
                convert_range(sb, &bit_1, &bit_2);
                ld.s.shift = bit_1;
                ld.s.mask = create_mask(bit_2 - bit_1 + 1, bit_1);
            }

            const std::string &dest_bits = cd->parameters[i].right_range;
            if (dest_bits.empty())
            {
                ld.d.shift = 0;
                ld.d.mask = create_mask(ld.d.i->get_size(), 0);
            } else {
                std::string db = dest_bits.substr(1, dest_bits.length()-2); //remove brackets
                unsigned int bit_1, bit_2;
                convert_range(db, &bit_1, &bit_2);
                ld.d.shift = bit_1;
                ld.d.mask = create_mask(bit_2 - bit_1 + 1, bit_1);
            }
            ld.s.i->connect(ld.s, ld.d, inverted);

        }
    }
    if (name != "cpu") {
        cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu"));
        m_system_clock = cpu->clock;
    }

    try {
        m_cold_reset = read_confg_value(cd, "cold_reset", false, true);
        m_soft_reset = read_confg_value(cd, "soft_reset", false, true);
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);
    }

    return emulator::Result::ok();
}

bool ComputerDevice::get_reset_behavior(bool is_cold)
{
    return (is_cold)?m_cold_reset:m_soft_reset;
}

void ComputerDevice::interface_callback(MAYBE_UNUSED unsigned int callback_id, MAYBE_UNUSED unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    //Does nothing by default, but may be overridden
}

void ComputerDevice::memory_callback(MAYBE_UNUSED unsigned int callback_id, MAYBE_UNUSED unsigned int address)
{
    //Does nothing by default, but may be overridden
}

void ComputerDevice::reset(MAYBE_UNUSED bool cold)
{
    //Does nothing by default, but may be overridden
}

bool ComputerDevice::belongs_to_class(const std::string &class_to_check)
{
    return device_class == class_to_check;
}

DeviceOptions ComputerDevice::get_device_options()
{
    return {};
}

void ComputerDevice::set_device_option(unsigned option_id, unsigned value_id)
{
    // Does nothing by default
}

//------------------- Introspection and control ----------------------------//

std::vector<DeviceFieldInfo> ComputerDevice::get_device_fields()
{
    return {
        {"name",        "Device name",                          false},
        {"type",        "Device type",                          false},
        {"class",       "Device class",                         false},
        {"interfaces",  "Values of all interfaces of a device", false}
    };
}

std::vector<DeviceCommandInfo> ComputerDevice::get_device_commands()
{
    return {
        {"reset",   "[cold|soft]",  "Resets a device"},
        {"option",  "id, value",    "Sets a device option, see set_device_option()"}
    };
}

bool ComputerDevice::get_field(const std::string &field, MAYBE_UNUSED unsigned int from, MAYBE_UNUSED unsigned int to, DeviceFieldValue &out)
{
    if (field == "name") {
        out.text = name;
        return true;
    }
    if (field == "type") {
        out.text = type;
        return true;
    }
    if (field == "class") {
        out.text = device_class;
        return true;
    }
    if (field == "interfaces") {
        //Generic diagnostics for this bus-oriented architecture: dumps every
        //interface belonging to this device with its current value
        std::string s;
        for (size_t i = 0; i < im->interfaces.size(); i++)
        {
            Interface * itf = im->interfaces[i];
            if (itf->device != this) continue;
            if (!s.empty()) s += ", ";
            s += itf->name + "=" + ((itf->value == _FFFF)?"-":hex_str(itf->value, 2));
        }
        out.text = s;
        return true;
    }
    return false;
}

std::string ComputerDevice::log_device(const std::string &field, std::pair<unsigned int, unsigned int> range, const LogFormat &fmt)
{
    std::string prefix = "[" + name + "." + field + "] ";

    DeviceFieldValue v;
    if (!get_field(field, range.first, range.second, v))
        return prefix + "ERROR: unknown field";

    if (!v.numeric) return prefix + v.text;

    unsigned int width = (v.width != 0)?v.width:fmt.width;

    std::string s;
    if (v.has_start) s += format_number(v.start, fmt.base, 16) + ":";

    for (size_t i = 0; i < v.values.size(); i++)
        s += ((s.empty())?"":" ") + format_number(v.values[i], fmt.base, width);

    return prefix + s;
}

emulator::Result ComputerDevice::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "reset") {
        bool cold = p.empty() || str_tolower(p[0]) != "soft";
        reset(cold);
        return emulator::Result::ok();
    }

    if (command == "option") {
        if (p.size() < 2)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Command 'option' expects an option id and a value")) + "}");
        set_device_option(parse_numeric_value(p[0]), parse_numeric_value(p[1]));
        return emulator::Result::ok();
    }

    return emulator::Result::error(emulator::ErrorCode::UnknownCommand,
        "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Unknown command")) + "} " + name + "." + command);
}

//----------------------- class AddressableDevice -------------------------------//

unsigned AddressableDevice::get_size()
{
    return addresable_size;
}

unsigned AddressableDevice::get_direct(const unsigned address)
{
    return get_value(address);
}

unsigned int AddressableDevice::get_value_word(unsigned int address)
{
    return (get_value(address) & 0xFF) | ((get_value(address + 1) & 0xFF) << 8);
}

void AddressableDevice::set_value_word(unsigned int address, unsigned int value, bool force)
{
    set_value(address, value & 0xFF, force);
    set_value(address + 1, (value >> 8) & 0xFF, force);
}

std::vector<DeviceFieldInfo> AddressableDevice::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"value", "Stored value or a range of values", true});
    r.push_back({"size",  "Addressable size of a device",      false});
    return r;
}

std::vector<DeviceCommandInfo> AddressableDevice::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"set",  "address, value",    "Writes a value to an address"});
    r.push_back({"fill", "from, to, value",   "Fills an address range with a value"});
    return r;
}

bool AddressableDevice::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "value")
    {
        unsigned int size = get_size();
        out.numeric = true;

        if (size <= 1) {
            //Single register devices (ports, registers) ignore the range
            out.values.push_back(get_direct(0));
            return true;
        }

        if (to < from) to = from;
        if (from >= size) from = (size > 0)?size - 1:0;
        if (to >= size) to = (size > 0)?size - 1:0;

        out.has_start = true;
        out.start = from;
        for (unsigned int a = from; a <= to; a++)
            out.values.push_back(get_direct(a));
        return true;
    }

    if (field == "size") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back(get_size());
        return true;
    }

    return ComputerDevice::get_field(field, from, to, out);
}

emulator::Result AddressableDevice::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "set") {
        if (p.size() < 2)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{AddressableDevice|" + std::string(QT_TRANSLATE_NOOP("AddressableDevice", "Command 'set' expects an address and a value")) + "}");
        //force=true writes the internal state without driving the bus,
        //the same way the memory dump editor does it
        set_value(parse_numeric_value(p[0]), parse_numeric_value(p[1]), true);
        return emulator::Result::ok();
    }

    if (command == "fill") {
        if (p.size() < 3)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{AddressableDevice|" + std::string(QT_TRANSLATE_NOOP("AddressableDevice", "Command 'fill' expects a range and a value")) + "}");
        unsigned int from = parse_numeric_value(p[0]);
        unsigned int to = parse_numeric_value(p[1]);
        unsigned int value = parse_numeric_value(p[2]);
        unsigned int size = get_size();
        for (unsigned int a = from; a <= to && a < size; a++)
            set_value(a, value, true);
        return emulator::Result::ok();
    }

    return ComputerDevice::send_command(command, parameters);
}

//----------------------- class Memory -------------------------------//

Memory::Memory(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , auto_output(false)
    , buffer()
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
    // Automatic cleanup via std::vector destructor
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

unsigned int Memory::get_direct(unsigned int address)
{
    if (can_read && address < get_size()) return buffer[address];

    return 0xFF;
}


void Memory::set_value(unsigned int address, unsigned int value, bool force)
{
    if ((can_write || force) && address < get_size())
        buffer[address] = (uint8_t)value;

    if (write_callback != 0)
        memory_callback_device->memory_callback(write_callback, address);
}

void Memory::interface_callback(MAYBE_UNUSED unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    unsigned int address = new_value & create_mask(i_address.get_size(), 0);
    if (address < get_size() and auto_output) i_data.change(buffer[address]);
}

void Memory::set_size(unsigned int value)
{
    buffer.resize(value);  // std::vector handles allocation and cleanup
    addresable_size = value;

    i_address.set_size(ceil(log2(addresable_size)));

    // QRandomGenerator *rg = QRandomGenerator::global();
    // for (unsigned int i=0; i < value; i++) buffer[i]=rg->bounded(255);

    for (unsigned int i=0; i < value; i++) buffer[i] = getRandomNumber(0, 255);
}

void Memory::set_memory_callback(ComputerDevice * d, unsigned int callback_id, unsigned int mode)
{
    memory_callback_device = d;
    if ((mode & MODE_R) != 0) read_callback = callback_id;
    if ((mode & MODE_W) != 0) write_callback = callback_id;
}

uint8_t * Memory::get_buffer()
{
    return buffer.empty() ? nullptr : buffer.data();
}

std::vector<DeviceFieldInfo> Memory::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"fill", "Byte used to fill the memory on a cold reset", false});
    return r;
}

std::vector<DeviceCommandInfo> Memory::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"load", "\"file\" [, address]",     "Loads a binary file into the memory"});
    r.push_back({"save", "\"file\" [, from, to]",    "Saves a memory range to a binary file"});
    return r;
}

bool Memory::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "fill") {
        out.numeric = true;
        out.values.push_back(fill);
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

emulator::Result Memory::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "load")
    {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Memory|" + std::string(QT_TRANSLATE_NOOP("Memory", "Command 'load' expects a file name")) + "}");

        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];

        long long fsize = dsk_tools::utf8_file_size(file);
        dsk_tools::UTF8_ifstream f(file, std::ios::binary);
        if (fsize < 0 || !f.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{Memory|" + std::string(QT_TRANSLATE_NOOP("Memory", "Error reading")) + "} " + p[0]);

        unsigned int address = (p.size() > 1)?parse_numeric_value(p[1]):0;
        unsigned int size = get_size();
        if (address >= size)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Memory|" + std::string(QT_TRANSLATE_NOOP("Memory", "Address is out of the device range")) + "}");

        unsigned int len = static_cast<unsigned int>(fsize);
        if (address + len > size) len = size - address;

        std::vector<uint8_t> data(len);
        f.read(reinterpret_cast<char*>(data.data()), len);
        f.close();

        for (unsigned int i = 0; i < len; i++)
            set_value(address + i, data[i], true);

        return emulator::Result::ok();
    }

    if (command == "save")
    {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Memory|" + std::string(QT_TRANSLATE_NOOP("Memory", "Command 'save' expects a file name")) + "}");

        unsigned int size = get_size();
        unsigned int from = (p.size() > 1)?parse_numeric_value(p[1]):0;
        unsigned int to = (p.size() > 2)?parse_numeric_value(p[2]):((size > 0)?size - 1:0);
        if (to >= size) to = (size > 0)?size - 1:0;
        if (to < from)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Memory|" + std::string(QT_TRANSLATE_NOOP("Memory", "Incorrect address range")) + "}");

        const std::string file = resolve_output_path(sd, p[0]);
        dsk_tools::UTF8_ofstream f(file, std::ios::binary);
        if (!f.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{Memory|" + std::string(QT_TRANSLATE_NOOP("Memory", "Error writing")) + "} " + file);

        for (unsigned int a = from; a <= to; a++)
        {
            char b = static_cast<char>(get_direct(a));
            f.write(&b, 1);
        }
        f.close();
        return emulator::Result::ok();
    }

    return AddressableDevice::send_command(command, parameters);
}

//----------------------- class RAM -------------------------------//

RAM::RAM(InterfaceManager *im, EmulatorConfigDevice *cd):
    Memory(im, cd)
{
    this->can_read = true;
    this->can_write = true;
}

emulator::Result RAM::load_config(SystemData *sd)
{
    emulator::Result res = Memory::load_config(sd);
    if (!res) return res;

    set_size(parse_numeric_value(this->cd->get_parameter("size").value));

    try {
        std::string s = str_tolower(cd->get_parameter("fill").value);
        random_fill = (s == "random");
        if (!random_fill) fill = parse_numeric_value(s);
    } catch (std::exception &e) {
        fill = 0;
    }
    //memset(buffer, fill, get_size());

    reset(true);

    return emulator::Result::ok();
}

void RAM::reset(bool cold)
{
    if (cold && !buffer.empty()) {
        if (!random_fill) {
            memset(buffer.data(), fill, get_size());
        } else {
            // QRandomGenerator *rg = QRandomGenerator::global();
            // for (unsigned int i=0; i < get_size(); i++) buffer[i]=rg->bounded(255);
            for (unsigned int i=0; i < get_size(); i++) buffer[i]=getRandomNumber(0, 255);
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

emulator::Result ROM::load_config(SystemData *sd)
{
    emulator::Result res = Memory::load_config(sd);
    if (!res) return res;

    try {
        this->fill = parse_numeric_value(this->cd->get_parameter("fill").value);
    } catch (std::exception &e) {
        this->fill = 0xFF;
    }

    const bool repeat = read_confg_value(cd, "repeat", false, false);

    std::string image = cd->get_parameter("image", false).value;

    if (!image.empty()) {

        set_size(parse_numeric_value(cd->get_parameter("size").value));
        if (!buffer.empty()) memset(buffer.data(), fill, get_size());

        std::string file_name = find_file_location(sd, image);
        if (file_name.empty())
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "File not found")) + "} " + image);
        else
        if (dsk_tools::get_file_ext(file_name) == ".hex")
        {
            std::string content = dsk_tools::utf8_read_file(file_name);
            if (content.empty()) {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "Error reading HEX file")) + "} " + file_name);
            }

            unsigned int index = 0;
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line))
            {
                if (line.size() < 9) continue;
                unsigned int len = parse_numeric_value("$" + line.substr(1, 2));
                unsigned int type = parse_numeric_value("$" + line.substr(7, 2));
                if (type == 0)
                {
                    for (unsigned int j=0; j< len; j++)
                        buffer[index+j] = parse_numeric_value("$" + line.substr(9+j*2, 2));
                    index += len;
                }
            }
        } else {
            long long file_size = dsk_tools::utf8_file_size(file_name);
            if (file_size < 0) {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "Can't open ROM image file")) + "} " + file_name);
            }
            if (static_cast<unsigned int>(file_size) > this->get_size())
            {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "ROM image file is too big")) + "} " + this->name);
            }
            dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
            if (file.is_open()){
                file.read(reinterpret_cast<char*>(this->buffer.data()), file_size);
                file.close();

                // A chip smaller than the area it is mapped to repeats itself when
                // the high address lines are left undecoded. That is a property of
                // the machine, not of the image, so it is asked for in the config.
                size_t chunk = static_cast<size_t>(file_size);
                size_t total = static_cast<size_t>(this->get_size());
                if (repeat && chunk > 0)
                    for (size_t pos = chunk; pos < total; pos += chunk)
                    {
                        size_t left = total - pos;
                        memcpy(buffer.data() + pos, buffer.data(), (left < chunk)?left:chunk);
                    }
            } else {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "Can't open ROM image file")) + "} " + file_name);
            }
        }
    } else {
        std::vector<std::string> values = split_string(cd->get_parameter("data").right_extended, ',', true);
        set_size(values.size());
        for (size_t i=0; i<values.size(); i++)
            buffer[i] = parse_numeric_value(values[i]);
    }

    try {
        std::string s = str_tolower(cd->get_parameter("mode").value);
        if ((s == "stream")) rom_mode = ROMMode::Stream;
        else if ((s == "normal")) rom_mode = ROMMode::Normal;
        else
        {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "Incorrect mode set for")) + "} " + this->name);
        }
    } catch (std::exception &e) {
        rom_mode = ROMMode::Normal;
    }

    return emulator::Result::ok();
}

unsigned int ROM::get_value(unsigned int address)
{
    if (rom_mode == ROMMode::Normal) return Memory::get_value(address);
    auto value = Memory::get_value(stream_counter);
    stream_counter = (stream_counter + 1) % get_size();
    return value;
}

void ROM::set_value(unsigned int address, unsigned int value, bool force)
{
    if (rom_mode == ROMMode::Normal) Memory::set_value(address, value, force);
    else stream_counter = 0;
}

//----------------------- class Port -------------------------------//

Port::Port(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , default_value(0)
    , mask(_FFFF)
    , alt_bit(-1)
    , alt_value(0)
    , alt_default(0)
    , i_input(this, im, 8, "data", MODE_R, PORT_INPUT)
    , i_data(this, im, 8, "value", MODE_W)
    , i_access(this, im, 1, "access", MODE_W)
    , i_flip(this, im, 1, "flip", MODE_R, PORT_FLIP)
    , i_reset(this, im, 1, "reset", MODE_R, PORT_RESET)
    , i_alt(this, im, 8, "alt", MODE_W)

{
    try {
        size = parse_numeric_value(this->cd->get_parameter("size").value);
    } catch (std::exception &e) {
        size = 8;
    }

    i_input.set_size(size);
    i_data.set_size(size);
    i_alt.set_size(size);

    try {
        default_value = parse_numeric_value(this->cd->get_parameter("default").value);
    } catch (std::exception &e) {
        default_value = 0;
    }

    try {
        flip_mask = parse_numeric_value(cd->get_parameter("flipmask").value);
    } catch (std::exception &e) {
        flip_mask = _FFFF;
    }

    try {
        mask = parse_numeric_value(cd->get_parameter("mask").value);
    } catch (std::exception &e) {
        mask = _FFFF;
    }

    try {
        constant_value = parse_numeric_value(cd->get_parameter("constant_return").value);
        has_constant_return = true;
    } catch (std::exception &e) {
        has_constant_return = false;
    }

    // Some registers are two registers behind one address, and one bit of the
    // written value tells them apart. Reading always returns the main bank.
    try {
        alt_bit = (int)parse_numeric_value(cd->get_parameter("alt_bit").value);
    } catch (std::exception &e) {
        alt_bit = -1;
    }

    try {
        alt_default = parse_numeric_value(cd->get_parameter("alt_default").value);
    } catch (std::exception &e) {
        alt_default = 0;
    }

    value = default_value;
    alt_value = alt_default;
}

emulator::Result Port::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    const std::string acc_mode = read_confg_value(cd, "access_mode", false, std::string("rw"));
    if (acc_mode == "r") access_on_write = false;
    else
    if (acc_mode == "w") access_on_read = false;
    else
    if (acc_mode != "rw" and acc_mode != "wr")
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{Port|" + std::string(QT_TRANSLATE_NOOP("Port", "Incorrect access mode")) + "} ");

    return emulator::Result::ok();
}


void Port::interface_callback(MAYBE_UNUSED unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
        case PORT_FLIP:
            if ((old_value & 1) != 0 && (new_value & 1) == 0) write_register(value ^ flip_mask);
            break;
        case PORT_INPUT:
            // Bits driven from the outside replace their counterparts in the
            // register, so a read returns the current state of those lines.
            // Only the connected bits are touched, the rest keep what was
            // written into the port.
            this->value = (this->value & ~i_input.linked_bits) | (new_value & i_input.linked_bits);
            break;
        default: // PORT_RESET
            if ((old_value & 1) != 0 && (new_value & 1) == 0) this->value = default_value;
            break;
    }
}

unsigned int Port::read_register()
{
    if (access_on_read) {
        i_access.change(0);
        i_access.change(1);
    }
    if (!has_constant_return) return value;
    return constant_value;
}

void Port::write_register(unsigned int new_value)
{
    if (access_on_write) i_access.change(0);
    if (alt_bit >= 0 && ((new_value >> alt_bit) & 1) != 0) {
        alt_value = (new_value & mask) | (alt_value & ~mask);
        i_alt.change(alt_value);
    } else {
        this->value = (new_value & mask) | (this->value & ~mask);
        i_data.change(this->value);
    }
    if (access_on_write) i_access.change(1);
}

unsigned int Port::get_value(unsigned int address)
{
    unsigned int v = read_register();
    // Ports wider than a byte expose two byte lanes selected by A0
    if (size > 8) v = (address & 1)? ((v >> 8) & 0xFF) : (v & 0xFF);
    return v;
}

unsigned int Port::get_direct(unsigned address)
{
    return value;
}

void Port::set_value(unsigned int address, unsigned int value, bool force)
{
    if (size > 8)
        write_register((address & 1)? ((this->value & 0x00FF) | ((value & 0xFF) << 8))
                                    : ((this->value & 0xFF00) | (value & 0xFF)));
    else
        write_register(value);
}

unsigned int Port::get_value_word(MAYBE_UNUSED unsigned int address)
{
    return read_register();
}

void Port::set_value_word(MAYBE_UNUSED unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    write_register(value);
}

void Port::reset(MAYBE_UNUSED bool cold)
{
    if (alt_bit >= 0)
    {
        alt_value = alt_default;
        i_alt.change(alt_value);
    }
    write_register(default_value);
}

std::vector<DeviceFieldInfo> Port::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"default", "Value written on reset",   false});
    r.push_back({"mask",    "Write mask of a port",     false});
    return r;
}

bool Port::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "default") {
        out.numeric = true;
        out.values.push_back(default_value);
        return true;
    }
    if (field == "mask") {
        out.numeric = true;
        out.values.push_back(mask);
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

//----------------------- class PortAddress -------------------------------//

PortAddress::PortAddress(InterfaceManager *im, EmulatorConfigDevice *cd):
    Port(im, cd)
{
}

emulator::Result PortAddress::load_config(SystemData *sd)
{
    emulator::Result res = Port::load_config(sd);
    if (!res) return res;

    store_on_read = read_confg_value(cd, "store_on_read", false, false);

    return emulator::Result::ok();
}

void PortAddress::set_value(unsigned int address, MAYBE_UNUSED unsigned int value, bool force)
{
    i_access.change(0);
    this->value = (address & mask) | (this->value & ~mask);
    i_data.change(this->value);
    i_access.change(1);
}

unsigned int PortAddress::get_value(MAYBE_UNUSED unsigned int address)
{
    if (store_on_read) {
        this->value = (address & mask) | (this->value & ~mask);
        i_data.change(this->value);
    }
    return Port::get_value(address);
}

// A port-address encodes the command in the address itself, so word and byte
// accesses store exactly the same thing; only the returned width differs.
unsigned int PortAddress::get_value_word(unsigned int address)
{
    if (store_on_read) {
        this->value = (address & mask) | (this->value & ~mask);
        i_data.change(this->value);
    }
    return read_register();
}

void PortAddress::set_value_word(unsigned int address, unsigned int value, bool force)
{
    PortAddress::set_value(address, value, force);
}


void PortAddress::reset(MAYBE_UNUSED bool cold)
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
    , m_debug(DEBUG_STOPPED)
#else
    , m_debug(DEBUG_OFF)
#endif
    , break_count(0)
{
    try {
        clock = parse_numeric_value(this->cd->get_parameter("clock").value);
    } catch (std::exception &e) {
        clock = 0;
    }
}

emulator::Result CPU::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    if (clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "No CPU clock value found")) + "} " + name);

    std::string breaks = read_confg_value(cd, "breakpoints", false, std::string(""));
    if (!breaks.empty()) {
        // Addresses may be separated by commas, spaces or tabs
        for (size_t i = 0; i < breaks.size(); i++)
            if (breaks[i] == ',' || breaks[i] == '\t') breaks[i] = ' ';

        const std::vector<std::string> items = split_string(breaks, ' ', true);
        for (size_t i = 0; i < items.size(); i++)
        {
            const std::string item = str_trim(items[i]);
            if (item.empty()) continue;
            try {
                add_breakpoint(parse_numeric_value(item));
            } catch (std::exception &) {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "Invalid breakpoint address")) + "} " + name + ": " + item);
            }
        }

        const bool stopped = read_confg_value(cd, "stopped", false, false);
        const bool debug = read_confg_value(cd, "debug", false, false);
        if (debug) m_debug = DEBUG_BRAKES;
        if (stopped) m_debug = DEBUG_STOPPED;
    }

    mm = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper"));

    return emulator::Result::ok();
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

std::vector<DeviceFieldInfo> CPU::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"pc",          "Program counter",              false});
    r.push_back({"command",     "Current command code",         false});
    r.push_back({"registers",   "All registers as name=value",  false});
    r.push_back({"flags",       "All flags as name=value",      false});
    r.push_back({"clock",       "CPU frequency, Hz",            false});
    r.push_back({"debug",       "Debug mode of a CPU",          false});
    return r;
}

std::vector<DeviceCommandInfo> CPU::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"stop",        "",                 "Stops the execution"});
    r.push_back({"run",         "",                 "Resumes the execution"});
    r.push_back({"step",        "",                 "Executes one command"});
    r.push_back({"breakpoint",  "address",          "Adds a breakpoint"});
    r.push_back({"setreg",      "name, value",      "Sets a register or a flag"});
    return r;
}

//Renders the name/value pairs already provided by every CPU implementation
static std::string pairs_to_string(const std::vector<std::pair<std::string, std::string>> &pairs)
{
    std::string s;
    for (size_t i = 0; i < pairs.size(); i++) {
        //Names starting with a dash are the blank lines the register area
        //draws between register groups, not values
        if (!pairs[i].first.empty() && pairs[i].first[0] == '-') continue;
        s += ((s.empty())?"":" ") + pairs[i].first + "=" + pairs[i].second;
    }
    return s;
}

bool CPU::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "pc") {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(get_pc());
        return true;
    }
    if (field == "command") {
        out.numeric = true;
        out.values.push_back(get_command());
        return true;
    }
    if (field == "registers") {
        out.text = pairs_to_string(get_registers());
        return true;
    }
    if (field == "flags") {
        out.text = pairs_to_string(get_flags());
        return true;
    }
    if (field == "clock") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back(clock);
        return true;
    }
    if (field == "debug") {
        out.numeric = true;
        out.values.push_back(m_debug);
        return true;
    }
    return ComputerDevice::get_field(field, from, to, out);
}

emulator::Result CPU::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "stop") {
        m_debug = DEBUG_STOPPED;
        return emulator::Result::ok();
    }
    if (command == "run") {
        m_debug = DEBUG_OFF;
        return emulator::Result::ok();
    }
    if (command == "step") {
        m_debug = DEBUG_STEP;
        return emulator::Result::ok();
    }
    if (command == "breakpoint") {
        if (p.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "Command 'breakpoint' expects an address")) + "}");
        add_breakpoint(parse_numeric_value(p[0]));
        return emulator::Result::ok();
    }
    if (command == "setreg") {
        if (p.size() < 2)
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{CPU|" + std::string(QT_TRANSLATE_NOOP("CPU", "Command 'setreg' expects a name and a value")) + "}");
        set_context_value(p[0], parse_numeric_value(p[1]));
        return emulator::Result::ok();
    }

    return ComputerDevice::send_command(command, parameters);
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

emulator::Result MemoryMapper::load_config(SystemData *sd)
{
    LinkData ld;

    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    this->cache_size = sizeof(this->read_cache_items) / sizeof(MapperCacheEntry);

    std::string config_device = this->cd->get_parameter("config", false).value;

    if (!config_device.empty())
    {
        Interface * i_cfg = im->get_interface_by_name(config_device, "out", false);
        if (i_cfg == nullptr)
            i_cfg = im->get_interface_by_name(config_device, "value");
        if (i_cfg == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MemoryMapper|" + std::string(QT_TRANSLATE_NOOP("MemoryMapper", "Interface not found")) + "} " + config_device);

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

    std::string m = this->cd->get_parameter("cancelinit", false).value;
    this->cancel_init_mask = (!m.empty())?parse_numeric_value(m):0;

    //Loading ranges
    for (size_t i = 0; i < this->cd->parameters.size(); i++)
    {
        const std::string &parameter_name = this->cd->parameters[i].name;
        if (parameter_name == "@memory" || parameter_name == "@port")
        {
            MapperRange mr;
            std::string mask, c, a;
            unsigned int index;

            const std::string &range = this->cd->parameters[i].left_range;
            size_t p = range.find("][");
            if (p != std::string::npos)
            {
                c = range.substr(1, p - 1);
                a = range.substr(p + 2, range.length() - p - 3);
                size_t cp = c.find(':');
                if (cp != std::string::npos)
                {
                    mask = c.substr(cp + 1);
                    c = c.substr(0, cp);
                } else
                    mask = "";
            } else {
                c = "";
                a = range.substr(1, range.length() - 2);
                mask = "";
            }

            if (c == "*")
            {
                index = 0;
                this->first_range = 0;
                c = "";
            } else {
                index = ++this->ranges_count;
            }

            try {
                mr.config_mask = parse_numeric_value(mask);
            } catch (std::exception &) {
                mr.config_mask = create_mask(this->i_config.get_size(), 0);
            }

            try {
                mr.config_value = parse_numeric_value(c);
            } catch (std::exception &) {
                mr.config_value = 0;
                mr.config_mask = 0;
            }

            p = a.find('-');
            if (p != std::string::npos)
            {
                mr.range_begin = parse_numeric_value(a.substr(0, p));
                mr.range_end = parse_numeric_value(a.substr(p + 1));
            } else {
                if (parameter_name == "@port")
                {
                    mr.range_begin = parse_numeric_value(a);
                    mr.range_end = mr.range_begin;
                } else
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{MemoryMapper|" + std::string(QT_TRANSLATE_NOOP("MemoryMapper", "Incorrect range for")) + "} " + parameter_name);
            }

            mr.device = this->im->dm->get_device_by_name(this->cd->parameters[i].value);

            const std::string &right_range = this->cd->parameters[i].right_range;
            try {
               mr.base = parse_numeric_value(right_range.substr(1, right_range.length() - 2));
            } catch (std::exception &) {
                mr.base = 0;
            }

            m = this->cd->extended_parameter(i, "mode");
            if (m == "r") mr.mode = MODE_R;
            else if (m == "w") mr.mode = MODE_W;
            else mr.mode = MODE_RW;

            try {
                mr.address_mask = parse_numeric_value(this->cd->extended_parameter(i, "addr_mask"));
                mr.address_value = parse_numeric_value(this->cd->extended_parameter(i, "addr_value"));
            } catch (std::exception &) {
                mr.address_mask = 0;
                mr.address_value = 0;
            }

            // A strict range answers only accesses of its own mode: a register
            // that exists for reading only times out when written, which is how
            // some firmware tells machines apart
            mr.strict = (this->cd->extended_parameter(i, "strict") == "1");

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

    return emulator::Result::ok();
}

void MemoryMapper::reset(MAYBE_UNUSED bool cold)
{
    if (this->cancel_init_mask != 0) this->first_range = 0;
    this->read_cache_items = 0;
    this->write_cache_items = 0;
}

void MemoryMapper::interface_callback(MAYBE_UNUSED unsigned int callback_id, MAYBE_UNUSED unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
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

    if ((this->first_range == 0) && ((address & this->cancel_init_mask) != 0))
    {
        this->first_range = 1;
        this->read_cache_items = 0;
        this->write_cache_items = 0;
    }

    unsigned int address_on_device, range_index;
    this->no_device = false;
    AddressableDevice * d = this->map(&(this->ranges), this->first_range, this->ranges_count, this->i_config.value, address, MODE_R, &address_on_device, &range_index);
    if (d != nullptr)
    {
        //TODO: Cache
        //if (mr->cache) this->add_cache_entry(); //this->ranges[range_index]
        return d->get_value(address_on_device);

    } else {
        this->no_device = !this->responds(address, MODE_R);
        return _FFFF;
    }
}

// Whether anything is mapped at the address at all: a range of the other
// mode still counts, unless it is strict
bool MemoryMapper::responds(unsigned int address, unsigned int mode)
{
    unsigned int config = this->i_config.value;
    for (unsigned int i = this->first_range; i <= this->ranges_count; i++)
    {
        MapperRange * mr = &(this->ranges[i]);
        if (
            ( (config & mr->config_mask) == mr->config_value )
            && (address >= mr->range_begin) && (address <= mr->range_end)
            && ( (address & mr->address_mask) == mr->address_value)
            && ( ((mr->mode & mode) != 0) || !mr->strict )
            )
            return true;
    }
    return false;
}

void MemoryMapper::write(unsigned int address, unsigned int value)
{
    //TODO: Cache
    // for (unsigned int i = 0; i < this->write_cache_items; i++)

    unsigned int address_on_device, range_index;
    this->no_device = false;
    AddressableDevice * d = this->map(&(this->ranges), this->first_range, this->ranges_count, this->i_config.value, address, MODE_W, &address_on_device, &range_index);
    if (d != nullptr)
    {
        //TODO: Cache
        //if (mr->cache) this->add_cache_entry(); //this->ranges[range_index]
        d->set_value(address_on_device, value);
    } else
        this->no_device = !this->responds(address, MODE_W);
}

unsigned int MemoryMapper::read_word(unsigned int address)
{
    if ((this->first_range == 0) && ((address & this->cancel_init_mask) != 0))
    {
        this->first_range = 1;
        this->read_cache_items = 0;
        this->write_cache_items = 0;
    }

    unsigned int address_on_device, range_index;
    this->no_device = false;
    AddressableDevice * d = this->map(&(this->ranges), this->first_range, this->ranges_count, this->i_config.value, address, MODE_R, &address_on_device, &range_index);
    if (d != nullptr)
        return d->get_value_word(address_on_device);
    else {
        this->no_device = !this->responds(address, MODE_R);
        return _FFFF;
    }
}

void MemoryMapper::write_word(unsigned int address, unsigned int value)
{
    unsigned int address_on_device, range_index;
    this->no_device = false;
    AddressableDevice * d = this->map(&(this->ranges), this->first_range, this->ranges_count, this->i_config.value, address, MODE_W, &address_on_device, &range_index);
    if (d != nullptr)
        d->set_value_word(address_on_device, value);
    else
        this->no_device = !this->responds(address, MODE_W);
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

std::vector<DeviceFieldInfo> MemoryMapper::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"map",     "Current memory and port mapping",       false});
    r.push_back({"config",  "Value of the mapper configuration bus", false});
    return r;
}

bool MemoryMapper::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "config") {
        out.numeric = true;
        out.values.push_back(i_config.value);
        return true;
    }

    if (field == "map")
    {
        //Dumps the ranges the way they are matched at run time. Ranges before
        //first_range are the ones disabled by cancelinit.
        //Ranges are stored from index 1 up; index 0 exists only when the
        //config declares a [*] range, and shows as [off] once it is gone.
        std::string s;
        for (unsigned int i = (cancel_init_mask != 0)?0:1; i <= ranges_count; i++)
        {
            const MapperRange &r = ranges[i];
            s += "\n  ";
            s += (i < first_range)?"[off] ":"      ";
            s += hex_str(r.range_begin, 4) + "-" + hex_str(r.range_end, 4);
            s += " -> " + ((r.device != nullptr)?r.device->name:std::string("-"));
            s += "[" + hex_str(r.base, 4) + "]";
            s += " mode=";
            s += ((r.mode & MODE_R) != 0)?"r":"";
            s += ((r.mode & MODE_W) != 0)?"w":"";
            if (r.config_mask != 0)
                s += " cfg=" + hex_str(r.config_value, 2) + ":" + hex_str(r.config_mask, 2);
            if (r.address_mask != 0)
                s += " addr=" + hex_str(r.address_value, 4) + ":" + hex_str(r.address_mask, 4);
        }
        for (unsigned int i = 0; i < ports_count; i++)
        {
            const MapperRange &r = ports[i];
            s += "\n  port " + hex_str(r.range_begin, 2);
            s += " -> " + ((r.device != nullptr)?r.device->name:std::string("-"));
        }
        out.text = s;
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

unsigned int MemoryMapper::get_value(unsigned int address)
{
    return read(address);
}

void MemoryMapper::set_value(unsigned int address, unsigned int value, bool force)
{
    write(address, value);
}

unsigned int MemoryMapper::get_value_word(unsigned int address)
{
    return read_word(address);
}

void MemoryMapper::set_value_word(unsigned int address, unsigned int value, bool force)
{
    write_word(address, value);
}


//----------------------- class Display -------------------------------//

GenericDisplay::GenericDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , sx(0)
    , sy(0)
    , screen_valid(false)
    , was_updated(true)
    , m_renderer_valid(false)
{}

void GenericDisplay::set_renderer(VideoRenderer & vr)
{
    renderer = &vr;
    render_pixels = vr.get_buffer();
    line_bytes = vr.get_line_bytes();
    m_renderer_valid = true;
}


void GenericDisplay::validate(bool force_render)
{
    // Rendering has to be locked against the resize done by the render thread,
    // otherwise a repaint arriving from the interface thread keeps drawing into
    // a surface that has just been deleted.
    lock_surface();

    // A device may change its resolution at any moment, and the surface only
    // follows on the next frame. Until it does, the buffer is still the old,
    // possibly narrower one, so drawing would run past its end.
    if (m_renderer_valid && render_pixels != nullptr && (int)(sx * 4) <= line_bytes) {
        if (!screen_valid || force_render) render_all(force_render);
    }

    unlock_surface();
}

void GenericDisplay::reset(bool cold)
{
    screen_valid = false;
    was_updated = true;
}

std::vector<DeviceFieldInfo> GenericDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"resolution", "Current screen resolution", false});
    return r;
}

bool GenericDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "resolution") {
        out.text = std::to_string(sx) + "x" + std::to_string(sy);
        return true;
    }
    return ComputerDevice::get_field(field, from, to, out);
}

void GenericDisplay::change_resolution(unsigned new_x, unsigned new_y)
{
    if (sx != new_x || sy != new_y) {
        m_renderer_valid = false;
        sx = new_x;
        sy = new_y;
    }
}

bool GenericDisplay::has_valid_renderer()
{
    return m_renderer_valid;
}

void GenericDisplay::lock_surface()
{
    m_surface_mutex.lock();
}

void GenericDisplay::unlock_surface()
{
    m_surface_mutex.unlock();
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

//------------------------------- FDC --------------------------------------//

std::vector<DeviceFieldInfo> FDC::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"busy",  "1 while the controller is working",  false});
    r.push_back({"drive", "Index of the drive it is talking to", false});
    return r;
}

bool FDC::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "busy" || field == "drive")
    {
        out.numeric = true;
        out.values.push_back((field == "busy") ? (get_busy() ? 1u : 0u) : get_selected_drive());
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}
