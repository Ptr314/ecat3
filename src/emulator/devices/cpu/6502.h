#ifndef MOS6502_H
#define MOS6502_H

#include <QObject>

#include "emulator/core.h"
#include "6502core.h"

class mos6502;

// Library wrapper
class mos6502Core: public mos6502core
{
private:
    mos6502 * emulator_device;

public:
    mos6502Core(mos6502 * emulator_device, int family_type);
    virtual uint8_t read_mem(uint16_t address) override;
    virtual void write_mem(uint16_t address, uint8_t value) override;
};

// Emulator device
class mos6502 : public CPU
{
    Q_OBJECT
private:
    Interface * i_nmi;
    Interface * i_irq;
    Interface * i_so;

    mos6502Core * core;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

protected:
    virtual unsigned int get_pc() override;

#ifdef LOG_CPU
    virtual void log_state(uint8_t command, bool before, unsigned int cycles=0) override;
#endif

public:
    mos6502(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type);

    virtual void reset(bool cold) override;
    virtual unsigned int execute() override;
    virtual unsigned int read_mem(unsigned int address) override;
    virtual void write_mem(unsigned int address, unsigned int data) override;

    virtual QList<QPair<QString, QString>> get_registers() override;
    virtual QList<QPair<QString, QString>> get_flags() override;
    virtual unsigned int get_command() override;

    virtual void set_context_value(QString name, unsigned int value) override;

};

ComputerDevice * create_mos6502(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_wdc65c02(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // MOS6502_H
