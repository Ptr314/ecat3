#ifndef I8080main_H
#define I8080main_H

#include "emulator/core.h"
#include "emulator/devices/cpu/i8080core.h"

//#define LOG_8080 1

#ifdef LOG_8080
#include "cpulogger.h"
#endif

using namespace I8080;

class i8080;

//Library wrapper
class I8080Core: public i8080core
{
private:
    i8080 * emulator_device;

public:
    I8080Core(i8080 * emulator_device);
    virtual uint8_t read_mem(uint16_t address) override;
    virtual void write_mem(uint16_t address, uint8_t value) override;
    virtual uint8_t read_port(uint16_t address) override;
    virtual void write_port(uint16_t address, uint8_t value) override;
    virtual void inte_changed(unsigned int inte) override;
};

//Emulator class
class i8080: public CPU
{
private:
    Interface i_nmi;
    Interface i_int;
    Interface i_inte;
    Interface i_m1;

    i8080core * core;

#ifdef LOG_8080
    CPULogger * logger;
#endif

protected:
    virtual unsigned int get_pc() override;

public:
    i8080(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~i8080();
    virtual void reset(bool cold) override;
    virtual unsigned int execute() override;
    virtual unsigned int read_mem(unsigned int address) override;
    virtual void write_mem(unsigned int address, unsigned int data) override;
    virtual unsigned int read_port(unsigned int address);
    virtual void write_port(unsigned int address, unsigned int data);
    virtual void inte_changed(unsigned int inte);

    virtual QList<QPair<QString, QString>> get_registers() override;
    virtual QList<QPair<QString, QString>> get_flags() override;
    virtual unsigned int get_command() override;

    virtual void set_context_value(QString name, unsigned int value) override;

};

unsigned int read_mem(unsigned int address);
void write_mem(unsigned int address, unsigned int data);

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8080main_H
