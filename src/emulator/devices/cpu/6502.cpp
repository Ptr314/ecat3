// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 6502 & 65c02 emulator interface class

#include "6502.h"

#define CALLBACK_NMI    1
#define CALLBACK_INT    2


// ---------------------------  Library wrapper --------------------------------

mos6502Core::mos6502Core(mos6502 * emulator_device, int family_type):
    mos6502core(family_type)
{
    this->emulator_device = emulator_device;
}

uint8_t mos6502Core::read_mem(uint16_t address)
{
    return emulator_device->read_mem(address);
}

void mos6502Core::write_mem(uint16_t address, uint8_t value)
{
    emulator_device->write_mem(address, value);
}

// ---------------------------  Emulator device --------------------------------

mos6502::mos6502(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type):
    CPU(im, cd)
    , i_nmi(this, im, 1, "nmi", MODE_R, CALLBACK_NMI)
    , i_irq(this, im, 1, "irq", MODE_R, CALLBACK_INT)
    , i_so(this, im, 1, "so", MODE_R)
{
    core = new mos6502Core(this, family_type);

    over_commands.push_back(0x20);
}

void mos6502::reset(bool cold)
{
    CPU::reset(cold);
}

unsigned int mos6502::read_mem(unsigned int address)
{
    i_address.change(address);
    return mm->read(address);
}

void mos6502::write_mem(unsigned int address, unsigned int data)
{
    i_address.change(address);
    mm->write(address, data);
}

QList<QPair<QString, QString>> mos6502::get_registers()
{
    QList<QPair<QString, QString>> l;
    mos6502context * c = core->get_context();

    l << QPair<QString, QString>("A", QString("%1").arg(c->A, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("X", QString("%1").arg(c->X, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("Y", QString("%1").arg(c->Y, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("S", QString("%1").arg(c->S, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("P", QString("%1").arg(c->P, 2, 16, QChar('0')).toUpper())
      << QPair<QString, QString>("-1", "")
      << QPair<QString, QString>("PC", QString("%1").arg(c->r16.PC, 4, 16, QChar('0')).toUpper())
    ;
    return l;
}

QList<QPair<QString, QString>> mos6502::get_flags()
{
    QList<QPair<QString, QString>> l;
    mos6502context * c = core->get_context();

    l << QPair<QString, QString>("C",  QString("%1").arg( ((c->P & MOS6502::F_C) != 0)?1:0))
      << QPair<QString, QString>("Z",  QString("%1").arg( ((c->P & MOS6502::F_Z) != 0)?1:0))
      << QPair<QString, QString>("I",  QString("%1").arg( ((c->P & MOS6502::F_I) != 0)?1:0))
      << QPair<QString, QString>("D",  QString("%1").arg( ((c->P & MOS6502::F_D) != 0)?1:0))
      << QPair<QString, QString>("B",  QString("%1").arg( ((c->P & MOS6502::F_B) != 0)?1:0))
      << QPair<QString, QString>("5", QString("%1").arg( ((c->P & MOS6502::F_P5) != 0)?1:0))
      << QPair<QString, QString>("V",  QString("%1").arg( ((c->P & MOS6502::F_V) != 0)?1:0))
      << QPair<QString, QString>("N",  QString("%1").arg( ((c->P & MOS6502::F_N) != 0)?1:0))
    ;
    return l;
}

unsigned int mos6502::get_command()
{
    return core->get_command();
}

unsigned int mos6502::get_pc()
{
    return core->get_pc();
}

void mos6502::set_context_value(QString name, unsigned int value)
{
    mos6502context * c = core->get_context();

    if (name == "PC")
    {
        c->r16.PC = value;
    }
}

#ifdef LOG_CPU
void mos6502::log_state(uint8_t command, bool before, unsigned int cycles)
{
    if (log_available())
    {
        mos6502context * c = core->get_context();
        logs(
            QString(" %1").arg(command, 2, 16, QChar('0')) + ((before)?"+":"-")
            + QString(" A:%1").arg(c->A, 2, 16, QChar('0'))
            + QString(" X:%1").arg(c->X, 2, 16, QChar('0'))
            + QString(" Y:%1").arg(c->Y, 2, 16, QChar('0'))
            + QString(" S:%1").arg(c->S, 2, 16, QChar('0'))
            + QString(" P:%1").arg(c->P, 2, 16, QChar('0'))
            );
    }
}
#endif

void mos6502::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_NMI:
#ifdef LOG_CPU
        logs("NMI = " + QString::number(new_value & 1));
#endif
        core->set_nmi((new_value & 1) == 0); //NMI has active 0
        break;
    case CALLBACK_INT:
#ifdef LOG_CPU
        logs("INT = " + QString::number(new_value & 1));
#endif
        core->set_irq((new_value & 1) == 0); //INT has active 0
        break;
    }
}

unsigned int mos6502::execute()
{
    if (reset_mode)
    {
        core->reset();
        reset_mode = false;
    }

    if (debug == DEBUG_STOPPED)
        return 0;

#ifdef LOG_CPU
    uint8_t log_cmd = get_command();
    static bool do_log = false;
    uint16_t address = get_pc();

    if (address >0x3000 && address < 0x4000)
        do_log = true;
    if (do_log) log_state(log_cmd, true);
#endif

    unsigned int cycles = core->execute();

#ifdef LOG_CPU
    // if (do_log) log_state(log_cmd, false, cycles);
#endif


    switch (debug) {
    case DEBUG_STEP:
        debug = DEBUG_STOPPED;
        break;
    case DEBUG_BRAKES:
        if (check_breakpoint(get_pc())) debug = DEBUG_STOPPED;
        break;
    default:
        break;
    }

    return cycles;
}

ComputerDevice * create_mos6502(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new mos6502(im, cd, MOS_6502_FAMILY_BASIC);
}

ComputerDevice * create_wdc65c02(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new mos6502(im, cd, MOS_6502_FAMILY_65C02);
}
