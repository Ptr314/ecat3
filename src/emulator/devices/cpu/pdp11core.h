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

    // К1801ВМ3: режимы ядра и пользователя со своими указателями стека,
    // приоритет 0-7, диспетчер памяти с 22-разрядным адресом и свой
    // пультовый режим (теневая память по SEL, страница HPAR). Всё это - в
    // pdp11core_vm3.cpp; у ВМ1 и ВМ2 ни одна из этих веток не работает
    bool vm3;
    uint16_t m_sp[4] = {};          // указатели стека режимов, кроме текущего
    // Стек пульта (R10 кристалла): им в пульте пользуются неявные обращения к
    // стеку - JSR, RTS, RTI, вход в пульт. Явный SP в команде пульта - SP ядра
    uint16_t m_hsp = 0;
    uint16_t & stack_reg() { return (vm3 && context.halt_mode) ? m_hsp : context.R[PDP11::REG_SP]; }
    uint16_t m_par[2][8] = {};      // [0] - ядро, [1] - пользователь
    uint16_t m_pdr[2][8] = {};
    uint16_t m_sr0 = 0, m_sr2 = 0, m_sr3 = 0, m_hpar = 0;
    bool m_mmu_abort = false;       // авария диспетчера: ловушка 250, а не 4
    int m_space = -1;               // режим, в котором идёт обмен данными (MFPx/MTPx), -1 - текущий
    // Команда записала PSW по адресу 177776: признаки, которые она сама потом
    // выставила, не в счёт - остаётся записанное
    bool m_psw_written = false;
    uint16_t m_psw_value = 0;
    // Стек ядра опустился ниже 400 («жёлтая зона»): ловушка через 4 после
    // команды. При входе в ловушку проверки нет
    bool m_yellow = false;
    bool m_in_trap = false;
    bool m_yellow_trap = false;     // идёт ловушка по самой жёлтой зоне
    unsigned int m_trap_depth = 0;  // ловушка, начатая при входе в другую
    // Режим обслуживания (SR0, разряд 8): без включённого диспетчера
    // переводятся только обращения к операнду-приёмнику
    bool m_dest = false;
    uint16_t * m_w_pdr = nullptr;   // PDR страницы последней записи: W ставится, если это не регистр процессора
    // Автоинкременты команды: при аварии откатываются все, и удачного
    // источника тоже - у 11/34 нет SR1, и сорванную команду можно повторить,
    // только если регистры остались прежними. Автодекременты остаются
    unsigned int m_inc_count = 0;
    uint8_t m_inc_reg[4] = {};
    uint8_t m_inc_step[4] = {};
    void vm3_stack_check()
    {
        if (!m_in_trap && !context.halt_mode && vm3_mode() == 0 && context.R[PDP11::REG_SP] < 0400)
            m_yellow = true;
    }
    unsigned int vm3_mode() const { return (context.PSW >> 14) & 3; }
    static unsigned int vm3_sp_index(unsigned int mode) { return (mode & 2) ? 3 : 0; }
    uint32_t vm3_translate(uint16_t va, bool write, bool probe = false);
    bool vm3_register(uint32_t pa, bool write, bool byte, uint16_t &value);
    uint16_t vm3_read(uint16_t va, bool byte);
    void vm3_write(uint16_t va, uint16_t value, bool byte);
    void vm3_set_psw(uint16_t value);
    void vm3_trap(uint16_t vector);
    void vm3_enter_halt();
    void vm3_return(uint16_t pc, uint16_t psw);
    bool vm3_interrupts(unsigned int & cycles);
    void vm3_reset();
    bool vm3_execute(uint16_t command, unsigned int & cycles);

    // Пультовый режим ВМ1. Своего режима в кристалле нет: вход в пультовое
    // исключение - это цикл чтение-модификация-запись 0177716 с установкой
    // разряда 3 (его разбирает внешняя схема), сохранение PSW в 0177676 и PC
    // в 0177674 и вектор 0160002; START и STEP читают их обратно и сбрасывают
    // разряд 3. Тайм-аут на любом шаге - обычное исключение по зависанию: на
    // БК, где по 0177676 никто не отвечает, так и получается ловушка через
    // 004 по клавише СТОП и команде HALT. Разряды 10 и 11 PSW ставит только
    // вектор пультового исключения. Описание кристалла - 1801BM1/cpu11,
    // vm1/doc/1801vm1.docx, «Процедура входа в пультовые исключения»
    bool vm1_console;
    void vm1_console_entry(uint16_t vector, int depth);
    void vm1_console_return(bool step);
    void vm1_leave_console_psw();
    void vm1_timeout(int depth);
    void vm1_trap(uint16_t vector, int depth);

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
    // ВМ1: RTT и STEP кончаются выборкой следующей команды без опроса блока
    // прерываний, и между ними и этой командой запрос не берётся
    bool m_no_poll = false;


    uint16_t fetch();
    uint16_t read_stream(uint16_t address);
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
    unsigned int C_REPLY;       // tn itself, the reply_delay they were built from

    void push(uint16_t value);
    uint16_t pop();
    void do_trap(uint16_t vector);
    void enter_halt_mode(uint16_t vector);
    // ВМ2: слово состояния с разрядом 8 - флагом пультового режима. Он у нас
    // отдельный (context.halt_mode), а на кристалле это разряд PSW: в
    // пультовом режиме его пишет загрузка PSW словом (вектор, RTI, RTT,
    // START, STEP), и в стек PSW уходит вместе с ним. RTI и RTT - только при
    // возврате в 160000-177777 (load_psw_return)
    bool vm2_sel() const { return has_console && halt_sel != 0; }
    uint16_t psw_word() const;
    void load_psw_word(uint16_t value);
    void load_psw_return(uint16_t value);
    uint16_t sel_base() const { return (uint16_t)(halt_sel & 0177400); }
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
    // ВМ1: номер процессора (входы nPA0, nPA1). Регистр начального пуска -
    // 177716 + 020 * номер
    unsigned int cpu_number = 0;
    // ВМ3: после пуска сразу войти в пульт (переключатель платы на «пульт»)
    bool start_in_halt = false;

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

    // Что отдаёт процессору безадресное чтение - регистр начального пуска
    // платы. Пультовая команда 000020 кладёт его в R0: у ДВК-3 это 140000 и
    // режим пуска в трёх младших разрядах, по нему монитор решает, выйти ли
    // в пульт, загрузиться или пустить программу
    unsigned int una_value = 0;

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
    // Physical addresses: 16 bits for the ВМ1 and ВМ2, 22 for the ВМ3 behind
    // its MMU, plus bit 22 for its halt mode shadow space (SEL)
    virtual uint16_t read_word(uint32_t address) = 0;
    virtual void write_word(uint32_t address, uint16_t value) = 0;
    virtual uint8_t read_byte(uint32_t address) = 0;
    virtual void write_byte(uint32_t address, uint8_t value) = 0;

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
        bool     no_poll;
    };
    void get_latches(saved_latches &s) const;
    void set_latches(const saved_latches &s);

    // Состояние ВМ3 вне контекста для снимка: указатели стека режимов, PAR и
    // PDR ядра и пользователя, SR0, SR2, SR3, HPAR - VM3_STATE_WORDS слов
    enum { VM3_STATE_WORDS = 4 + 32 + 4 + 1 };
    bool is_vm3() const { return vm3; }
    // Физический адрес виртуального - для отладчика, без аварий и отметок
    uint32_t vm3_peek_address(uint16_t va) { return vm3_translate(va, false, true); }
    void get_vm3(uint16_t * out) const;
    void set_vm3(const uint16_t * in);

    // The last trap taken, for scripts: vector, PC of the trapped instruction, count
    uint16_t m_trap_vector = 0;
    uint16_t m_trap_pc = 0;
    unsigned int m_trap_count = 0;

    // Адреса последних выполненных команд, кольцом: откуда программа пришла
    // туда, где сломалась. Одна запись в массив на команду
    enum { HISTORY_SIZE = 256 };
    uint16_t m_history[HISTORY_SIZE] = {};
    unsigned int m_history_pos = 0;
    // То же кольцо, снятое при последнем входе в пультовый режим: монитор
    // потом крутится в своём цикле опроса и затирает историю за миг
    uint16_t m_halt_history[HISTORY_SIZE] = {};
    unsigned int m_halt_history_pos = 0;
    void snapshot_history()
    {
        for (unsigned int i = 0; i < HISTORY_SIZE; i++) m_halt_history[i] = m_history[i];
        m_halt_history_pos = m_history_pos;
    }

    virtual void set_virq(bool state, uint16_t vector);
    virtual void set_irq2(bool state);
    virtual void set_irq3(bool state);
    virtual void set_aclo(bool state);
    virtual void set_halt(bool state);

    unsigned int execute();

    // For a timing model outside the core (timing = vm1). m_stream is set
    // while an access reads the instruction stream: an opcode, an index, an
    // immediate or an absolute address. The rest tells what the last
    // execute() did
    bool m_stream = false;
    enum { LAST_NONE, LAST_INSN, LAST_INTERRUPT, LAST_IDLE };
    unsigned int m_last_kind = LAST_NONE;
    uint16_t m_last_command = 0;
    bool m_last_trapped = false;        // it ended in a trap: reserved, bus error, trace
    bool m_last_console = false;        // ВМ1 entered or left the console: no template
    bool m_last_taken = false;          // a branch or SOB whose condition held
    bool m_last_iako = false;           // LAST_INTERRUPT: a vectored one, with IAKO
    // ВМ2: ASH/ASHC - сколько шагов сдвига сделал кристалл (влево n - n,
    // вправо n - n-1); DIV - кончился переполнением или делением на ноль
    unsigned int m_last_shift = 0;
    int m_last_shift_n = 0;             // ВМ3: число сдвига со знаком (< 0 - вправо)
    bool m_last_div_v = false;
};
