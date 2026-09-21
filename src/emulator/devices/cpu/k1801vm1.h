// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВМ1 / К1801ВМ2 emulator interface class

#pragma once

#include "emulator/core.h"
#include "pdp11core.h"

class k1801vm1;

// Library wrapper
class K1801VM1Core: public pdp11core
{
private:
    k1801vm1 * emulator_device;

public:
    K1801VM1Core(k1801vm1 * emulator_device, int family_type);
    virtual uint16_t read_word(uint16_t address) override;
    virtual void write_word(uint16_t address, uint16_t value) override;
    virtual uint8_t read_byte(uint16_t address) override;
    virtual void write_byte(uint16_t address, uint8_t value) override;
    // Ядро сообщает о входе в пультовый режим и выходе из него, устройство
    // поднимает линию наружу
    virtual void on_halt_mode(bool state) override;
    virtual void on_virq_ack(uint16_t vector) override;
    virtual void on_bus_init() override;
};

// Emulator device
class k1801vm1 : public CPU
{
private:
    // The last bus timeout, for scripts hunting holes in a memory map
    unsigned int m_timeouts = 0;
    unsigned int m_timeout_address = 0;
    unsigned int m_timeout_pc = 0;

    Interface i_virq;               // vectored interrupt request
    Interface i_vector;             // vector supplied by the requesting device
    Interface i_irq2;               // request with the fixed vector 0100
    Interface i_irq3;               // request with the fixed vector 0270
    Interface i_halt;               // console (halt mode) request
    // Power-fail line. While it is asserted the processor is held in reset and
    // executes nothing; releasing it starts the processor from its start
    // address. On the УК-НЦ the peripheral processor drives this line (bit 5 of
    // its register 177716), which is how the machine brings the central
    // processor up - the ROM it boots from is on the peripheral side.
    Interface i_dclo;

    // Линия аварии сети. Её снятие при работающем процессоре - прерывание по
    // вектору 024, и оно старше всех остальных. У УК-НЦ линией управляет
    // периферийный процессор разрядом 15 регистра 177716: единица прижимает
    // линию, ноль отпускает. Резидент винчестера так и сообщает центральному
    // процессору об окончании переноса - подставляет свой адрес в вектор 024
    // и снимает линию на мгновение
    Interface i_aclo;

    // Процессор в пультовом режиме. У машин, где от режима зависит карта
    // памяти (УК-НЦ: в пультовом все 64 КБ - ОЗУ, в обычном верхние 8 КБ -
    // страница ввода-вывода), эта линия заводится на ~config диспетчера
    Interface i_halt_mode;

    // Подтверждение векторного прерывания (IAKO): на мгновение показывает
    // взятый вектор и возвращается в ноль. Устройство, чей это вектор, снимает
    // свой запрос. Не подключённая линия ничего не стоит
    Interface i_iako;

    // INIT на магистрали: импульс 1-0 по команде RESET. Устройства, которые
    // к нему подключены (~init), сбрасывают свои регистры
    Interface i_init;

    // True while ~dclo holds the processor down
    bool m_held_in_reset = false;

    // True while ~aclo is asserted; the interrupt comes when it is released
    bool m_aclo_active = false;

    K1801VM1Core * core;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

public:
    k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd, int family_type);
    virtual ~k1801vm1();

    virtual emulator::Result load_config(SystemData *sd) override;
    virtual void reset(bool cold) override;
    virtual unsigned int execute() override;

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    // Byte access, used by the debugger and the disassembler
    virtual unsigned int read_mem(unsigned int address) override;
    virtual void write_mem(unsigned int address, unsigned int data) override;

    // Word access, the native width of the bus
    unsigned int read_mem_word(unsigned int address);
    void write_mem_word(unsigned int address, unsigned int data);

    // True when nothing answered the last access. On the МПИ bus that is a
    // timeout, and the processor turns it into a trap through vector 4.
    bool bus_timeout();
    void note_timeout(unsigned int address);
    void set_halt_mode(bool state);
    void virq_acknowledged(unsigned int vector);
    void bus_init();

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    virtual std::vector<std::pair<std::string, std::string>> get_registers() override;
    virtual std::vector<std::pair<std::string, std::string>> get_flags() override;
    virtual unsigned int get_pc() override;
    virtual unsigned int get_command() override;

    virtual void set_context_value(const std::string &name, unsigned int value) override;
};

ComputerDevice * create_k1801vm1(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_k1801vm2(InterfaceManager *im, EmulatorConfigDevice *cd);
