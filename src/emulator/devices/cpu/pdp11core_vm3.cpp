// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВМ3 part of the PDP-11 core: modes, MMU, halt mode

#include "pdp11core.h"

// К1801ВМ3 ближе всего к PDP-11/34 (так считают и авторы реконструкции
// кристалла, github.com/1801BM1/cpu11, vm3): режимы ядра и пользователя со
// своими указателями стека, приоритет 0-7 в разрядах 7-5 слова состояния,
// диспетчер памяти KT11-D (по восемь пар PAR/PDR на режим) и 22-разрядный
// адрес, как у F-11 (SR3, разряд 4). Регистры диспетчера и слово состояния
// по адресу 177776 - внутри кристалла, на магистраль обращения к ним не идут.
//
// Пультовый режим свой, не как у ВМ2. Вход - команда HALT в режиме ядра,
// запрос по линии HALT или двойная ошибка: указатель стека пульта ставится
// на 100000, туда уходят PSW и PC, и выполнение идёт с адреса 0. В пульте
// адреса 000000-077777 - теневая память платы (вывод SEL, здесь разряд 22
// физического адреса), 100000-137777 - через регистр HPAR (172512),
// 140000-177777 - страница ввода-вывода. Прерывания в пульте замаскированы,
// зависание магистрали берёт вектор 4 теневой памяти, ничего не кладя в стек.
// Выход - RTI или RTT. Так это видно по тесту haltm.mac и по исходнику ядра
// (hdl/vm3/vm3_wb.v реплики forth32: wb_adr[22] <= hmod & ~ba[15], parh_sel).

namespace {
    // Те же, что в pdp11core.cpp
    const unsigned int C_ALU   = 1;
    const unsigned int C_RESET = 30;
}

#define VM3_IO      017760000u      // страница ввода-вывода 22-разрядного адреса
#define VM3_SEL     020000000u      // теневая память пультового режима

#define SR0_NR      0100000         // страница не загружена
#define SR0_PL      0040000         // выход за длину страницы
#define SR0_RO      0020000         // запись в страницу только для чтения
#define SR0_ABORTS  (SR0_NR | SR0_PL | SR0_RO)
#define SR0_WRITE   0160401         // что программа пишет в SR0: аварии, обслуживание, включение
#define PDR_WRITE   0077416         // длина, направление, доступ
#define PDR_W       0000100         // в страницу писали

void pdp11core::get_vm3(uint16_t * out) const
{
    unsigned int n = 0;
    for (int i = 0; i < 4; i++) out[n++] = m_sp[i];
    for (int s = 0; s < 2; s++) for (int i = 0; i < 8; i++) out[n++] = m_par[s][i];
    for (int s = 0; s < 2; s++) for (int i = 0; i < 8; i++) out[n++] = m_pdr[s][i];
    out[n++] = m_sr0; out[n++] = m_sr2; out[n++] = m_sr3; out[n++] = m_hpar;
    out[n++] = m_hsp;
}

void pdp11core::set_vm3(const uint16_t * in)
{
    unsigned int n = 0;
    for (int i = 0; i < 4; i++) m_sp[i] = in[n++];
    for (int s = 0; s < 2; s++) for (int i = 0; i < 8; i++) m_par[s][i] = in[n++];
    for (int s = 0; s < 2; s++) for (int i = 0; i < 8; i++) m_pdr[s][i] = in[n++];
    m_sr0 = in[n++]; m_sr2 = in[n++]; m_sr3 = in[n++]; m_hpar = in[n++];
    m_hsp = in[n++];
}

// Виртуальный адрес в физический. probe - для отладчика: без аварий и без
// отметки записи
uint32_t pdp11core::vm3_translate(uint16_t va, bool write, bool probe)
{
    if (context.halt_mode) {
        if (va < 0100000) return VM3_SEL | va;
        if (va < 0140000) return (((uint32_t)m_hpar << 6) + (va & 017777)) & 017777777u;
        return VM3_IO + (va & 017777);
    }

    // Режим обслуживания переводит одни лишь обращения к приёмнику
    if ((m_sr0 & 1) == 0 && !((m_sr0 & 0400) && m_dest && !m_stream))
        return (va >= 0160000) ? VM3_IO + (va & 017777) : va;

    const unsigned int mode = (m_space >= 0) ? (unsigned int)m_space : vm3_mode();
    const unsigned int page = va >> 13;
    unsigned int reason = 0;
    unsigned int set = 0;
    if (mode == 0) set = 0;
    else if (mode == 3) set = 1;
    else reason = SR0_NR;                   // режимов 1 и 2 у кристалла нет

    if (reason == 0) {
        // Проверки независимы: незагруженная страница короче адреса даёт
        // в SR0 оба признака (тест 11/34 FKABD0 ждёт 140003)
        const uint16_t pdr = m_pdr[set][page];
        const unsigned int acf = (pdr >> 1) & 3;
        const unsigned int block = (va >> 6) & 0177;
        const unsigned int plf = (pdr >> 8) & 0177;
        if (acf == 0 || acf == 2) reason |= SR0_NR;
        if ((pdr & 010) ? (block < plf) : (block > plf)) reason |= SR0_PL;
        if (write && acf == 1) reason |= SR0_RO;
    }

    if (reason != 0) {
        if (!probe) {
            // Первая авария замораживает SR0 и SR2, пока программа не снимет
            // разряды аварии
            if ((m_sr0 & SR0_ABORTS) == 0)
                m_sr0 = (uint16_t)((m_sr0 & 0000401) | reason | (mode << 5) | (page << 1));
            m_mmu_abort = true;
            m_abort = true;
        }
        return 0;
    }

    // Страница и режим последнего обращения видны в SR0, пока авария его не
    // заморозила (тест 44 FKTHB0 ждёт в SR0 страницу 7 после чтения самого SR0)
    if (!probe && (m_sr0 & SR0_ABORTS) == 0)
        m_sr0 = (uint16_t)((m_sr0 & ~0156) | (mode << 5) | (page << 1));
    // Отладчик спрашивает из своего потока: запомненную страницу не трогать
    if (!probe) m_w_pdr = write ? &m_pdr[set][page] : nullptr;
    uint32_t pa = ((uint32_t)m_par[set][page] << 6) + (va & 017777);
    if (m_sr3 & 020)
        pa &= 017777777u;
    else {
        pa &= 0777777u;
        if (pa >= 0760000u) pa = VM3_IO + (pa & 017777);
    }
    return pa;
}

// Регистры внутри кристалла. true - адрес их, обмен сделан
bool pdp11core::vm3_register(uint32_t pa, bool write, bool byte, uint16_t &value)
{
    if (pa < VM3_IO || pa >= VM3_SEL) return false;
    const unsigned int a = (pa - VM3_IO) | 0160000;      // как 16-разрядный адрес
    uint16_t * reg = nullptr;
    uint16_t wmask = 0177777;
    uint16_t cur = 0;
    bool psw = false;
    bool par = false;

    if (a >= 0172300 && a <= 0172317)      { reg = &m_pdr[0][(a >> 1) & 7]; wmask = PDR_WRITE; }
    else if (a >= 0172340 && a <= 0172357) { reg = &m_par[0][(a >> 1) & 7]; par = true; }
    else if (a >= 0177600 && a <= 0177617) { reg = &m_pdr[1][(a >> 1) & 7]; wmask = PDR_WRITE; }
    else if (a >= 0177640 && a <= 0177657) { reg = &m_par[1][(a >> 1) & 7]; par = true; }
    else if ((a & ~1u) == 0177572) { reg = &m_sr0; wmask = SR0_WRITE; }
    else if ((a & ~1u) == 0177574) { if (!write) value = 0; return true; }
    else if ((a & ~1u) == 0177576) { if (!write) value = m_sr2; return true; }
    else if ((a & ~1u) == 0172512) { reg = &m_hpar; par = true; }
    else if ((a & ~1u) == 0172516) {
        if (!write) {
            // Неиспользуемые разряды кристалл читает единицами
            value = (uint16_t)(0177717 | (m_sr3 & 060));
            return true;
        }
        reg = &m_sr3;
        wmask = 060;
    }
    else if ((a & ~1u) == 0177776) { psw = true; }
    else return false;

    cur = psw ? context.PSW : *reg;
    if (!write) {
        // PAR и HPAR без 22-разрядного режима отдают только 12 разрядов;
        // пульт этот режим включает сам
        if (par && (m_sr3 & 020) == 0 && !context.halt_mode) cur &= 07777;
        value = cur;
        return true;
    }

    uint16_t v = value;
    if (byte) v = (a & 1) ? (uint16_t)((cur & 0x00FF) | ((value & 0xFF) << 8))
                          : (uint16_t)((cur & 0xFF00) | (value & 0xFF));
    if (psw) {
        // Разряд T так не ставится
        vm3_set_psw((uint16_t)((v & ~PDP11::F_T) | (context.PSW & PDP11::F_T)));
        m_psw_written = true;
        m_psw_value = context.PSW;
        return true;
    }
    *reg = (uint16_t)((cur & ~wmask) | (v & wmask));
    // Запись в PDR снимает признак записи в страницу
    if (wmask == PDR_WRITE) *reg &= ~PDR_W;
    return true;
}

uint16_t pdp11core::vm3_read(uint16_t va, bool byte)
{
    // Команда, у которой обращение уже сорвалось, магистраль больше не трогает
    if (m_abort) return 0;
    // ВМ3, в отличие от ВМ1 и ВМ2, нечётный адрес слова не прощает
    if (!byte && (va & 1)) { m_abort = true; return 0; }
    const bool was = m_abort;
    m_abort = false;
    const uint32_t pa = vm3_translate(va, false);
    if (m_abort) return 0;
    m_abort = was;
    uint16_t v = 0;
    if (vm3_register(pa, false, byte, v))
        return byte ? (uint16_t)(((va & 1) ? (v >> 8) : v) & 0xFF) : v;
    return byte ? read_byte(pa) : read_word(pa);
}

void pdp11core::vm3_write(uint16_t va, uint16_t value, bool byte)
{
    if (m_abort) return;
    if (!byte && (va & 1)) { m_abort = true; return; }
    const bool was = m_abort;
    m_abort = false;
    m_w_pdr = nullptr;
    const uint32_t pa = vm3_translate(va, true);
    if (m_abort) return;
    m_abort = was;
    uint16_t v = value;
    // Признак записи в страницу ставится до самой записи (запись в PDR его
    // снимает), кроме записи в SR0 (тест 32 FKTHB0; PSW - ставит, тест 30)
    if (m_w_pdr != nullptr && (pa & ~1u) != VM3_IO + (0177572 - 0160000)) *m_w_pdr |= PDR_W;
    if (vm3_register(pa, true, byte, v)) return;
    if (byte) write_byte(pa, (uint8_t)value);
    else write_word(pa, value);
}

// Смена режима меняет и указатель стека - и в пульте: SP в его командах -
// SP режима из слова состояния, свой стек пульта (m_hsp) отдельно.
// Указателей у кристалла два, выбирает разряд 15: у режима 01 стек ядра, у 10 -
// пользователя
void pdp11core::vm3_set_psw(uint16_t value)
{
    const unsigned int old = vm3_sp_index(vm3_mode());
    const unsigned int now = vm3_sp_index((value >> 14) & 3);
    if (old != now) {
        m_sp[old] = context.R[PDP11::REG_SP];
        context.R[PDP11::REG_SP] = m_sp[now];
    }
    context.PSW = value;
}

// Ловушка или прерывание. Вектор читается в пространстве ядра; новый режим
// берётся из вектора, прежний режим запоминается в разрядах 13-12. Авария
// при записи в стек - двойная ошибка, она уводит в пульт (слово состояния к
// этому времени уже взято из вектора)
void pdp11core::vm3_trap(uint16_t vector)
{
    m_trap_vector = vector;
    m_trap_pc = context.R[PDP11::REG_PC];
    m_trap_count++;
    context.halted = false;
    m_dest = false;

    if (context.halt_mode) {
        // В пульте вектор берётся из теневой памяти, в стек ничего не идёт
        m_abort = false;
        const uint16_t pc = vm3_read(vector, false);
        const uint16_t psw = vm3_read((uint16_t)(vector + 2), false);
        m_abort = false;
        m_mmu_abort = false;
        context.R[PDP11::REG_PC] = pc;
        vm3_set_psw(psw);
        return;
    }

    const uint16_t old_psw = context.PSW;
    const uint16_t old_pc = context.R[PDP11::REG_PC];
    const unsigned int old_mode = vm3_mode();

    m_abort = false;
    m_mmu_abort = false;
    m_space = 0;
    const uint16_t pc = vm3_read(vector, false);
    const uint16_t psw = vm3_read((uint16_t)(vector + 2), false);
    m_space = -1;
    if (m_abort) {
        m_abort = false;
        m_mmu_abort = false;
        vm3_enter_halt();
        return;
    }

    vm3_set_psw((uint16_t)((psw & ~030000) | (old_mode << 12)));
    m_in_trap = true;
    push(old_psw);
    if (!m_abort) push(old_pc);
    m_in_trap = false;
    if (m_abort) {
        // Двойная ошибка. Авария диспетчера при записи в стек - его ловушка
        // через 250 со словом состояния, уже взятым из вектора (тест 45
        // FKTHB0); зависание магистрали - вход в пульт
        const bool mmu = m_mmu_abort;
        m_abort = false;
        m_mmu_abort = false;
        if (mmu && m_trap_depth == 0) {
            m_trap_depth++;
            vm3_trap(PDP11::V_MMU);
            m_trap_depth--;
        } else
            vm3_enter_halt();
        return;
    }
    context.R[PDP11::REG_PC] = pc;

    // Вход в ловушку сам загнал стек ядра в жёлтую зону: следом - ловушка
    // через 4, обработчик первой не успевает начаться. Своя ловушка жёлтой
    // зоны повторно не проверяется
    if (!m_yellow_trap && vm3_mode() == 0 && context.R[PDP11::REG_SP] < 0400) {
        m_yellow_trap = true;
        vm3_trap(PDP11::V_BUS_ERROR);
        m_yellow_trap = false;
    }
}

void pdp11core::vm3_enter_halt()
{
    snapshot_history();
    m_step_pending = false;
    // Слово состояния пульта - 340, режим ядра: SP его команд - SP ядра
    const uint16_t psw = context.PSW;
    const uint16_t pc = context.R[PDP11::REG_PC];
    vm3_set_psw(0340);
    if (!context.halt_mode) {
        context.halt_mode = true;
        on_halt_mode(true);
    }
    // Повторный вход (HALT в пульте) тоже начинает стек пульта заново: PSW и
    // PC ложатся в 77776 и 77774 теневой памяти, там их и читает монитор
    m_abort = false;
    m_hsp = 0100000;
    m_in_trap = true;
    push(psw);
    push(pc);
    m_in_trap = false;
    m_abort = false;
    m_mmu_abort = false;
    context.R[PDP11::REG_PC] = 0;
    context.halted = false;
}

// RTI и RTT. Из пульта - назад в программу, с её указателем стека. В режиме
// пользователя слово состояния не может ни поднять режим, ни сменить приоритет
void pdp11core::vm3_return(uint16_t pc, uint16_t psw)
{
    context.R[PDP11::REG_PC] = pc;
    if (context.halt_mode) {
        context.halt_mode = false;
        on_halt_mode(false);
        vm3_set_psw(psw);
        return;
    }
    if (vm3_mode() == 3)
        psw = (uint16_t)((psw & 037) | (context.PSW & 07740) | ((psw | context.PSW) & 0170000));
    vm3_set_psw(psw);
}

bool pdp11core::vm3_interrupts(unsigned int & cycles)
{
    if (context.halt_mode) return false;

    if (halt_pin || is_halt_req) {
        is_halt_req = false;
        vm3_enter_halt();
        cycles += C_TRAP;
        return true;
    }
    if (is_aclo) {
        is_aclo = false;
        vm3_trap(PDP11::V_POWER_FAIL);
        cycles += C_TRAP;
        return true;
    }

    // Таймер - уровень 6, устройства на магистрали - уровень 4
    const unsigned int priority = (context.PSW >> 5) & 7;
    if (is_irq2 && priority < 6) {
        is_irq2 = false;
        vm3_trap(PDP11::V_IRQ2);
        cycles += C_TRAP;
        return true;
    }
    if (is_virq && priority < 4) {
        is_virq = false;
        m_last_iako = true;
        const uint16_t vector = virq_vector;
        vm3_trap(vector);
        on_virq_ack(vector);
        cycles += C_TRAP;
        return true;
    }
    return false;
}

void pdp11core::vm3_reset()
{
    for (int i = 0; i < 4; i++) m_sp[i] = 0;
    m_hsp = 0;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < 8; i++) { m_par[s][i] = 0; m_pdr[s][i] = 0; }
    m_sr0 = m_sr2 = m_sr3 = m_hpar = 0;
    m_mmu_abort = false;
    m_space = -1;
    context.PSW = 0340;
    context.R[PDP11::REG_PC] = start_address;
    // Плата держит процессор в пульте, пока переключатель стоит на «пульт»:
    // монитор в теневом ПЗУ получает управление сразу
    if (start_in_halt) vm3_enter_halt();
}

// Команды, которые у ВМ3 ведут себя иначе, чем у ВМ1 и ВМ2. true - сделано
bool pdp11core::vm3_execute(uint16_t command, unsigned int & cycles)
{
    const bool kernel = context.halt_mode || vm3_mode() == 0;

    switch (command) {
    case 0000000:                                       // HALT
        cycles += C_HALT;
        // Кристалл смотрит только на разряд 15: в режиме 01 HALT выполняется
        if (context.halt_mode || (context.PSW & 0100000) == 0) vm3_enter_halt();
        else do_trap(PDP11::V_RESERVED);
        return true;
    case 0000002:                                       // RTI
    case 0000006: {                                     // RTT
        cycles += 2 * C_DATI + C_ALU;
        const uint16_t pc = pop();
        const uint16_t psw = pop();
        if (m_abort) return true;
        // RTT: ловушка T - после следующей команды; это и так выходит, потому
        // что признак трассировки команды берётся в её начале
        vm3_return(pc, psw);
        return true;
    }
    case 0000005:                                       // RESET
        cycles += C_RESET;
        if (kernel) {
            on_bus_init();
            // INIT выключает диспетчер памяти и 22-разрядный режим
            m_sr0 = 0;
            m_sr3 = 0;
        }
        return true;
    default:
        break;
    }

    // 000007-000077 у ВМ3 резервные: пультовых START, STEP и команд ВМ2 нет
    if (command >= 0000007 && command <= 0000077) {
        cycles += C_HALT;
        do_trap(PDP11::V_RESERVED);
        return true;
    }

    // MFPI/MFPD (0065SS/1065SS) и MTPI/MTPD (0066DD/1066DD): данные - в
    // предыдущем режиме, адрес считается в текущем. Отдельного пространства
    // данных у кристалла нет, D и I - одно и то же
    const unsigned int op = command & 0077700;
    if (op == 0006500 || op == 0006600) {
        const unsigned int prev = (context.PSW >> 12) & 3;
        const unsigned int cur = vm3_mode();
        const unsigned int prev_sp = vm3_sp_index(prev);
        const bool other_sp = prev_sp != vm3_sp_index(cur);
        const unsigned int spec = command & 077;
        if (op == 0006500) {
            pdp11operand o = decode_operand(spec, false, cycles);
            cycles += access_cycles(o, OP_READ) + C_DATO + C_ALU;
            uint16_t v;
            if (o.is_reg)
                v = (o.reg == PDP11::REG_SP && other_sp) ? m_sp[prev_sp] : context.R[o.reg];
            else {
                m_space = (int)prev;
                v = read_word_checked(o.addr);
                m_space = -1;
            }
            if (m_abort) return true;
            push(v);
            set_nz(v, false);
            set_flag(PDP11::F_V, false);
        } else {
            const uint16_t v = pop();
            pdp11operand o = decode_operand(spec, false, cycles);
            cycles += C_DATI + access_cycles(o, OP_WRITE) + C_ALU;
            if (m_abort) return true;
            if (o.is_reg) {
                if (o.reg == PDP11::REG_SP && other_sp) m_sp[prev_sp] = v;
                else context.R[o.reg] = v;
            } else {
                m_space = (int)prev;
                write_word_checked(o.addr, v);
                m_space = -1;
            }
            set_nz(v, false);
            set_flag(PDP11::F_V, false);
        }
        return true;
    }

    // MTPS: в режиме пользователя меняются только признаки, режим и
    // предыдущий режим не меняются никогда
    if ((command & 0177700) == 0106400) {
        pdp11operand o = decode_operand(command & 077, true, cycles);
        cycles += single_op_cycles(o, OP_READ);
        const uint16_t v = read_operand(o, true);
        if (m_abort) return true;
        if (kernel)
            context.PSW = (uint16_t)((context.PSW & (0177400 | PDP11::F_T)) | (v & 0357));
        else
            context.PSW = (uint16_t)((context.PSW & ~017) | (v & 017));
        return true;
    }
    return false;
}
