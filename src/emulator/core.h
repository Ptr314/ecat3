#ifndef CORE_H
#define CORE_H

#include <QObject>
#include <QMessageBox>

#include "config.h"

#define MAX_LINKS               100
#define MAX_INTERFACES          200
#define MAX_REGISTERED_DEVICES  100

#define MODE_OFF                0
#define MODE_R                  1
#define MODE_W                  2
#define MODE_RW                 MODE_R + MODE_W

#define DEBUG_OFF               0
#define DEBUG_STOPPED           1
#define DEBUG_STEP              2
#define DEBUG_BRAKES            3


class DeviceManager;
class InterfaceManager;
class Interface;
class ComputerDevice;
class MemoryMapper;

typedef uint8_t ScreenColor[3];

struct SystemData {
    QString         system_file;
    QString         system_path;
    QString         system_type;
    QString         system_name;
    QString         system_version;
    QString         system_charmap;
    QString         software_path;
    float           screen_ratio;
    unsigned int    screen_scale;
    QString         allowed_files;
    unsigned int    mapper_cache;
};

//typedef void (ComputerDevice::*InterfaceCallbackFunc)(unsigned int, unsigned int);
using InterfaceCallbackFunc = void (ComputerDevice::*)(unsigned int, unsigned int);

typedef ComputerDevice * (*CreateDeviceFunc)(InterfaceManager *im, EmulatorConfigDevice *cd);

typedef void (*MemoryCallbackFunc)(unsigned int);

struct RegisteredDevice {
    QString type;
    CreateDeviceFunc create_func;
};

struct LinkedInterface {
    Interface * i;
    unsigned int mask;
    unsigned int shift;
};

struct LinkData {
    LinkedInterface s;
    LinkedInterface d;
};

struct DeviceDescription {
    ComputerDevice *device;
    QString device_type;
    QString device_name;
};

struct MapperRange {
    unsigned int        config_mask;		//AND-маска, которая накладывается на значение порта конфигурации
    unsigned int        config_value;		//Число, с которым сравнивается значение порта после маски
    unsigned int        range_begin;		//Адрес начала диапазона
    unsigned int        range_end;      	//Адрес конца диапазона
    unsigned int        address_mask;		//AND-маска, которая накладывается на адрес
    unsigned int        address_value;		//Число, с которым сравнивается адрес после маски
    ComputerDevice *    device;             //Устройство, соответствующее наложенным условиям
    unsigned int        base;				//Адрес во внутр. адр. пр-ве устройства, соотв. RangeBegin системы
    unsigned int        mode;				//Режим допустимости чтения-записи для устройства
    bool                cache;              //Разрешение кеширования записи
};

struct MapperCacheEntry {
    unsigned int        range_begin;
    unsigned int        range_end;
    ComputerDevice *    device;
    unsigned int        base;
    unsigned int        counter;
};

class ComputerDevice: public QObject
{
    Q_OBJECT

public:
    QString type;
    QString name;

    ComputerDevice(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void reset(bool cold);
    virtual void load_config(SystemData *sd);
    virtual void clock(unsigned int counter);
    virtual void system_clock(unsigned int counter);

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value);
    virtual void memory_callback(unsigned int callback_id, unsigned int address);

private:
    unsigned int clock_stored;
    unsigned int clock_miltiplier;
    unsigned int clock_divider;


protected:
    Interface * create_interface(unsigned int size, QString name, unsigned int mode, unsigned int callback_id = 0);
    EmulatorConfigDevice *cd;
    InterfaceManager *im;
    ComputerDevice * memory_callback_device;
};

class AddressableDevice: public ComputerDevice
{
public:
    AddressableDevice(InterfaceManager *im, EmulatorConfigDevice *cd):ComputerDevice(im, cd){};
    virtual unsigned int get_value(unsigned int address) = 0;
    virtual void set_value(unsigned int address, unsigned int value) = 0;
};


class Memory: public AddressableDevice
{
protected:
    bool can_read;
    bool can_write;
    bool auto_output;
    Interface *i_address;
    Interface *i_data;
    uint8_t * buffer;
    unsigned int size;
    unsigned short fill;
    unsigned int read_callback;
    unsigned int write_callback;

    virtual void set_value(unsigned int address, unsigned int value);
public:
    Memory(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~Memory();
    void set_size(unsigned int value);
    unsigned int get_size();
    virtual unsigned int get_value(unsigned int address);
    void set_memory_callback(ComputerDevice * d, unsigned int callback_id, unsigned int mode);
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value);
};

class RAM: public Memory
{
public:
    RAM(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd);
    virtual void reset(bool cold);
};

class ROM: public Memory
{
public:
    ROM(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd);
};

class Port:public AddressableDevice
{
private:
    unsigned int size;
    unsigned int flip_mask;

protected:
    unsigned int value;
    Interface * i_input;
    Interface * i_data;
    Interface * i_access;
    Interface * i_flip;

public:
    virtual unsigned int get_value(unsigned int address);
    virtual void set_value(unsigned int address, unsigned int value);

    Port(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value);
    virtual void reset(bool cold);
};

class PortAddress:public Port
{
public:
    PortAddress(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void set_value(unsigned int address, unsigned int value);
};

class DeviceManager: public QObject
{
    Q_OBJECT

public:
    DeviceManager(); //
    ~DeviceManager(); //

    unsigned int device_count;
    ComputerDevice *error_device;
    QString error_message;

    void add_device(InterfaceManager *im, EmulatorConfigDevice *d); //
    void clear(); //
    void load_devices_config(SystemData *sd); //
    ComputerDevice *get_device_by_name(QString name, bool required=true); //
    unsigned int get_device_index(QString name);
    void reset_devices(bool cold);
    void clock(unsigned int counter);
    void error(ComputerDevice *d, QString message);
    void error_clear();
    DeviceDescription * get_device(unsigned int i); //
    void register_device(QString device_type, CreateDeviceFunc func); //

private:
    DeviceDescription devices[100];
    unsigned int registered_devices_count;
    RegisteredDevice registered_devices[MAX_REGISTERED_DEVICES];
};

class Interface: public QObject
{
    Q_OBJECT
private:
    unsigned int size;
    unsigned int mode;
    unsigned int old_value;
    unsigned int edge_value;
    InterfaceManager *im;
    LinkData linked_interfaces[MAX_LINKS];
    unsigned int callback_id;
public:
    unsigned int value;
    unsigned int mask;
    QString name;
    unsigned int linked;
    unsigned int linked_bits;
    ComputerDevice *device;

    Interface(
                ComputerDevice * device,
                InterfaceManager * im,
                unsigned int size,
                QString name,
                unsigned int mode,
                unsigned int callback_id
    );

    void connect(LinkedInterface s, LinkedInterface d);
    unsigned int get_size();
    void set_size(unsigned int new_size);
    void set_mode(unsigned int new_mode);
    void change(unsigned int new_value); //Вызывается устройством для изменения выхода
    void changed(LinkData link, unsigned int value); //Вызывается связанными интерфейсами при изменении значения на них
    void clear();
    bool pos_edge(); //Триггеры фронтов для 0-го бита
    bool neg_edge();

};

class InterfaceManager: public QObject
{
    Q_OBJECT

public:
    unsigned int interfaces_count;
    Interface * interfaces[MAX_INTERFACES];
    DeviceManager *dm;


    InterfaceManager(DeviceManager *dm);
    ~InterfaceManager();

    void register_interface(Interface *i);
    Interface * get_interface_by_name(QString device_name, QString interface_name);
    void clear();
    void interface_changes(Interface *i);


private:
};

class CPU: public ComputerDevice
{
private:
    unsigned int breakpoints[100];

protected:
    Interface * address;
    Interface * data;
    bool reset_mode;

public:
    unsigned int clock;
    MemoryMapper * mm;
    unsigned int debug;
    unsigned int break_count;

    CPU(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd);
    virtual unsigned int execute() = 0;
    bool check_breakpoint(unsigned int address);
    void add_breakpoint(unsigned int address);
    void remove_breakpoint(unsigned int address);
    void clear_breakpoints();
    virtual void reset(bool cold);
    virtual unsigned int get_pc() = 0;
};

class MemoryMapper: public ComputerDevice
{
private:
    //TODO: Implement
protected:

public:

    MemoryMapper(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd);
    virtual void reset(bool cold);
    void sort_cache();
};

class Display: public ComputerDevice
{
    //TODO: Implement
private:
protected:
public:
    Display(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void get_screen(bool required) = 0;
};


//----------------------- Creation functions -------------------------------//

ComputerDevice * create_ram(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_rom(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_memory_mapper(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_port(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_port_address(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // CORE_H
