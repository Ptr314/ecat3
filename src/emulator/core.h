#ifndef CORE_H
#define CORE_H

#include <QObject>
#include <QMessageBox>

#include "config.h"

#define MAX_LINKS               100
#define MAX_INTERFACES          200
#define MAX_REGISTERED_DEVICES  100

class DeviceManager;
class InterfaceManager;
class Interface;
class ComputerDevice;

typedef short Storage[10*1024*1024];
typedef short ScreenColor[3];

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

typedef void (*InterfaceCallbackFunc)(unsigned int, unsigned int);

typedef ComputerDevice * (*CreateDeviceFunc)(InterfaceManager *im, EmulatorConfigDevice *cd);

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

class ComputerDevice: public QObject
{
    Q_OBJECT

public:
    QString device_type;
    QString device_name;

    ComputerDevice(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void reset(bool cold) = 0;
    virtual void load_config(SystemData *sd) = 0;
    virtual void clock(unsigned int counter) = 0;
    virtual void system_clock(unsigned int counter) = 0;

private:
    unsigned int clock_stored;
    unsigned int clock_miltiplier;
    unsigned int clock_divider;

    InterfaceManager *im;
    EmulatorConfigDevice *cd;

    Interface * create_interface(unsigned int size, QString name, unsigned int mode, InterfaceCallbackFunc *callback);

};

struct DeviceDescription {
    ComputerDevice *device;
    QString device_type;
    QString device_name;
};


class AddressableDevice: public ComputerDevice
{
public:
    virtual unsigned int get_value(unsigned int address) = 0;
    virtual unsigned int set_value(unsigned int address) = 0;
};

class DeviceManager: public QObject
{
    Q_OBJECT

public:
    DeviceManager();
    ~DeviceManager();

    int device_count;
    ComputerDevice *error_device;
    QString error_message;

    void add_device(InterfaceManager *im, EmulatorConfigDevice *d);
    void clear();
    void load_devices_config(SystemData *sd);
    ComputerDevice *get_device_by_name(QString name, bool required=true);
    unsigned int get_device_index(QString name);
    void reset_devices(bool is_cold);
    void clock(unsigned int counter);
    void error(ComputerDevice *d, QString message);
    void error_clear();
    DeviceDescription * get_device(unsigned int i);
    void register_device(QString device_type, CreateDeviceFunc func);

private:
    DeviceDescription *devices[100];
    unsigned int registered_devices_count;
    RegisteredDevice registered_devices[MAX_REGISTERED_DEVICES];
};

class Interface: public QObject
{
    Q_OBJECT
private:
    unsigned int old_value;
    unsigned int edge_value;
    InterfaceManager *im;
    LinkData linked_interfaces[MAX_LINKS];
    InterfaceCallbackFunc callback;
public:
    unsigned int value;
    unsigned int mask;
    QString name;
    ComputerDevice *d;
    unsigned int size;
    unsigned int mode;
    int linked;
    unsigned int linked_bits;

    void connect(LinkedInterface s, LinkedInterface d);
    void change(unsigned int value); //Вызывается устройством для изменения выхода
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
    Interface interfaces[MAX_INTERFACES];
    DeviceManager *dm;


    InterfaceManager(DeviceManager *dm);
    ~InterfaceManager();

    void register_interface(Interface *i);
    Interface * get_interface_by_name(QString device_name, QString interface_name);
    void clear();
    void interface_changes(Interface *i);


private:
};

ComputerDevice * create_port(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // CORE_H
