// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8051 (КР1816ВЕ51) with its 4 KB of program memory on a mapper

#pragma once

#include "emulator/core.h"

// Программная память - чтения mapper'а по адресу, внешняя память данных (MOVX) -
// его порты: адрес @Ri - младший байт, старший - защёлка P2, как на выводах.
// Порты: p0-p3 - выводы (защёлки наружу), p0_in-p3_in - что на них подано снаружи;
// читается «И» того и другого, как у квазидвунаправленного порта. Неподключённый
// вход стоит единицей. reset - вывод RST: 1 держит кристалл, спад пускает.
//
// Таймеры: режимы 0-3, счёт тактов и внешних импульсов. Внешний вход здесь
// не линия, а частота: t0_clock и t1_clock, Гц. Импульсов 250 кГц на линии
// хватило бы на вызов устройства каждые три команды, а прошивке нужен только
// их счёт. Вход INT0/INT1 - p3_in, разряды 2 и 3.
//
// Последовательный порт не моделируется: регистры хранятся, передача считается
// неподдержанной.
class i8051: public CPU
{
private:
    Interface i_p0, i_p1, i_p2, i_p3;
    Interface i_p0_in, i_p1_in, i_p2_in, i_p3_in;
    Interface i_reset;
    Interface * m_out[4];
    Interface * m_in[4];

    uint8_t m_iram[128];
    uint8_t m_sfr[128];         // SFR 80-FF: индекс - адрес без старшего разряда
    uint16_t m_pc = 0;

    bool m_in_irq[2] = {};      // обслуживается уровень приоритета 0, 1
    bool m_held = false;        // RST
    bool m_skip_irq = false;    // команда, за которой запрос не берётся (RETI, запись IE, IP)

    unsigned int m_t0_clock = 0;
    unsigned int m_t1_clock = 0;
    uint64_t m_t0_fraction = 0;
    uint64_t m_t1_fraction = 0;

    unsigned int m_cycles = 0;  // машинных циклов текущей команды

    uint8_t fetch();
    uint16_t fetch16();
    uint8_t read_iram(unsigned int address) const;
    void write_iram(unsigned int address, uint8_t value);
    uint8_t read_direct(unsigned int address);
    uint8_t read_direct_latch(unsigned int address);
    void write_direct(unsigned int address, uint8_t value);
    bool read_bit(unsigned int bit, bool latch);
    void write_bit(unsigned int bit, bool value);
    void push(uint8_t value);
    uint8_t pop();
    uint8_t reg(unsigned int n) const;
    void set_reg(unsigned int n, uint8_t value);
    uint8_t indirect_address(unsigned int opcode) const;
    uint8_t acc() const;
    void set_acc(uint8_t value);
    bool carry() const;
    void set_carry(bool value);
    void set_flags(bool c, bool ac, bool ov);
    void update_parity();
    void add(uint8_t value, bool with_carry);
    void subb(uint8_t value);
    void jump_relative(uint8_t offset);
    void step_timers(unsigned int machine_cycles);
    void step_timer(unsigned int n, unsigned int machine_cycles);
    bool check_interrupts();
    void reset_chip();
    void execute_opcode(uint8_t opcode);
    uint8_t port_pins(unsigned int n) const;
    void drive_port(unsigned int n);
    uint16_t movx_address_ri(unsigned int opcode) const;

protected:
    unsigned int get_pc() override;

public:
    i8051(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    unsigned int execute() override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int read_mem(unsigned int address) override;
    void write_mem(unsigned int address, unsigned int data) override;

    DisAsm * create_disasm(const std::string &data_path, emulator::Result &res) override;
    std::vector<std::pair<std::string, std::string>> get_registers() override;
    std::vector<std::pair<std::string, std::string>> get_flags() override;
    unsigned int get_command() override;
    void set_context_value(const std::string &name, unsigned int value) override;
    bool set_flag(const std::string &name, bool value) override;
    bool get_sp(unsigned int &sp) override;
    bool stack_address(int i, unsigned int &address) override;
    unsigned int stack_entry_bytes() const override { return 1; }
    unsigned int sp_bits() const override { return 8; }
    bool stack_grows_up() const override { return true; }
    unsigned int stack_peek(unsigned int address) override { return read_iram(address); }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_i8051(InterfaceManager *im, EmulatorConfigDevice *cd);
