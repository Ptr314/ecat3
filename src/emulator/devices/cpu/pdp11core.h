// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: PDP-11 compatible CPU core (К1801ВМ1, К1801ВМ2)

#pragma once

#include <cstdint>
#include "pdp11_context.h"

class pdp11core
{
protected:
    pdp11context context;

    // Что умеет именно этот кристалл. Проверки положительные: раньше EIS
    // включался условием "семейство не ВМ1", и любое третье семейство
    // получило бы его молча
    bool has_eis;               // MUL/DIV/ASH/ASHC, есть у ВМ2
    bool has_console;           // пультовый режим с командами RUN/STEP/MFPC..., есть у ВМ2

    // Шаг по команде STEP: после одной выполненной команды процессор
    // возвращается в пультовый режим
    bool m_step_pending = false;

    // Interrupt inputs. VIRQ follows the line - the requesting device drops it
    // once served (a БК clears the keyboard ready flag when the program reads
    // 0177662). IRQ2, IRQ3 and HALT are latched instead: they are driven by
    // pulses from a generator, which a level sensitive input would miss
    // between two instructions. See pdp11core::set_virq().
    bool is_virq;               // vectored request over the bus
    uint16_t virq_vector;
    bool is_irq2;               // fixed vector 0100
    bool is_irq3;               // fixed vector 0270
    bool is_halt_req;           // external console (halt mode) request, latched on the edge
    bool halt_pin;              // the line itself, for a processor with a console
    // Авария сети, вектор 024. Запрос даёт СНЯТИЕ линии ACLO при работающем
    // процессоре, и он тоже защёлкивается - это событие, а не уровень. У
    // УК-НЦ линией управляет периферийный процессор разрядом 15 регистра
    // 177716, и резидент винчестера так сообщает центральному, что перенос
    // окончен: подставляет свой адрес в вектор 024 и дёргает линию
    bool is_aclo;

    // Set by a failed bus access; aborts the current instruction and
    // turns into a trap through vector 4 once it unwinds.
    bool m_abort;

    // RTT defers the trace trap until after the following instruction
    bool m_no_trace;


    uint16_t fetch();
    uint16_t read_word_checked(uint16_t address);
    void write_word_checked(uint16_t address, uint16_t value);

    void set_flag(uint16_t flag, bool value);
    bool get_flag(uint16_t flag) const;
    void set_nz(uint16_t value, bool is_byte);

    pdp11operand decode_operand(unsigned int spec, bool is_byte, unsigned int & cycles);
    uint16_t read_operand(const pdp11operand & op, bool is_byte);
    void write_operand(const pdp11operand & op, bool is_byte, uint16_t value);

    // What an instruction does with its operand, which decides the bus cycle
    // it spends on it and therefore how long it takes
    enum operand_access { OP_NONE, OP_READ, OP_WRITE, OP_MODIFY };
    unsigned int access_cycles(const pdp11operand & op, operand_access access);
    unsigned int single_op_cycles(const pdp11operand & op, operand_access access);

    // Bus cycles, which depend on how long the memory takes to answer, and the
    // times built from them. Set by set_reply_delay().
    unsigned int C_DATI;        // a read                   7T + tn
    unsigned int C_DATO;        // a write, MOV only       10T + tn
    unsigned int C_DATIO;       // a read-modify-write     13T + 2tn
    unsigned int C_TRAP;        // trap or interrupt entry
    unsigned int C_HALT;        // entry into the halt mode
    unsigned int MODE_CYCLES[8];    // address computation of each mode

    void push(uint16_t value);
    uint16_t pop();
    void do_trap(uint16_t vector);
    void enter_halt_mode(uint16_t vector);
    bool check_interrupts(unsigned int & cycles);
    void do_branch(uint16_t command, bool condition, unsigned int & cycles);

    bool execute_single(uint16_t command, unsigned int & cycles);
    bool execute_double(uint16_t command, unsigned int & cycles);
    bool execute_misc(uint16_t command, unsigned int & cycles);

public:
    // Where execution begins after INIT. The address is wired outside the chip
    // and differs between systems: the БК starts executing straight from it,
    // while other 1801 machines keep a PC/PSW pair there and are configured
    // with start_from_vector.
    uint16_t start_address;
    bool start_from_vector;

    // Vector used by the HALT instruction and the external console request
    uint16_t halt_vector;

    pdp11core(int family_type);
    virtual ~pdp11core(){};

    // Clock periods the memory takes to answer a bus cycle (tn), 2 by default
    void set_reply_delay(unsigned int periods);

    // База векторов пультового режима (вывод SEL процессора). У УК-НЦ 0160000,
    // и вектор берётся как halt_sel | vector. Ноль оставляет прежнее
    // поведение: вход в режим идёт обычной ловушкой через halt_vector
    unsigned int halt_sel = 0;

    // Сообщает устройству о смене режима, чтобы оно подняло линию наружу:
    // адресное пространство машины может от неё зависеть
    virtual void on_halt_mode(bool state) { (void)state; }

    // Процессор взял векторное прерывание - то, что на магистрали 1801 делает
    // сигнал IAKO. Устройство, чей это вектор, по нему снимает свой запрос:
    // иначе запрос, выставленный однократно (готовность канала УК-НЦ), после
    // любого чужого прерывания предлагался бы процессору снова
    virtual void on_virq_ack(uint16_t vector) { (void)vector; }

    // Команда RESET выставила на магистраль INIT: устройства сбрасывают свои
    // регистры. Сам процессор при этом не сбрасывается
    virtual void on_bus_init() {}

    // Bus access, provided by the emulator device
    virtual uint16_t read_word(uint16_t address) = 0;
    virtual void write_word(uint16_t address, uint16_t value) = 0;
    virtual uint8_t read_byte(uint16_t address) = 0;
    virtual void write_byte(uint16_t address, uint8_t value) = 0;

    virtual void reset();
    virtual pdp11context * get_context();
    virtual uint16_t get_pc();

    // Everything outside the context that a cold start does not reproduce:
    // the latched interrupt requests and the two flags that unwind an
    // instruction. A snapshot taken between two instructions with a request
    // pending has to bring it back, or the machine loses the interrupt
    struct saved_latches {
        bool     step_pending;
        bool     is_virq;
        uint16_t virq_vector;
        bool     is_irq2;
        bool     is_irq3;
        bool     is_halt_req;
        bool     halt_pin;
        bool     is_aclo;
        bool     abort;
        bool     no_trace;
    };
    void get_latches(saved_latches &s) const;
    void set_latches(const saved_latches &s);

    // The last trap taken, for scripts: vector, PC of the trapped instruction, count
    uint16_t m_trap_vector = 0;
    uint16_t m_trap_pc = 0;
    unsigned int m_trap_count = 0;

    // Адреса последних выполненных команд, кольцом: откуда программа пришла
    // туда, где сломалась. Одна запись в массив на команду
    enum { HISTORY_SIZE = 256 };
    uint16_t m_history[HISTORY_SIZE] = {};
    unsigned int m_history_pos = 0;
    uint16_t get_command();

    virtual void set_virq(bool state, uint16_t vector);
    virtual void set_irq2(bool state);
    virtual void set_irq3(bool state);
    virtual void set_aclo(bool state);
    virtual void set_halt(bool state);

    unsigned int execute();
};
