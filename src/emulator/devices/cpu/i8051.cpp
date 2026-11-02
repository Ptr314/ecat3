// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8051 (КР1816ВЕ51) with its 4 KB of program memory on a mapper

#include <cstring>

#include "i8051.h"
#include "emulator/disasm_i8051.h"
#include "emulator/utils.h"

#define CALLBACK_P3     1
#define CALLBACK_RESET  2

#define SFR_P0      0x80
#define SFR_SP      0x81
#define SFR_DPL     0x82
#define SFR_DPH     0x83
#define SFR_TCON    0x88
#define SFR_TMOD    0x89
#define SFR_TL0     0x8A
#define SFR_TL1     0x8B
#define SFR_TH0     0x8C
#define SFR_TH1     0x8D
#define SFR_P1      0x90
#define SFR_SCON    0x98
#define SFR_SBUF    0x99
#define SFR_P2      0xA0
#define SFR_IE      0xA8
#define SFR_P3      0xB0
#define SFR_IP      0xB8
#define SFR_PSW     0xD0
#define SFR_ACC     0xE0
#define SFR_B       0xF0

#define PSW_P       0x01
#define PSW_OV      0x04
#define PSW_RS      0x18
#define PSW_AC      0x40
#define PSW_CY      0x80

#define TCON_IT0    0x01
#define TCON_IE0    0x02
#define TCON_IT1    0x04
#define TCON_IE1    0x08
#define TCON_TR0    0x10
#define TCON_TF0    0x20
#define TCON_TR1    0x40
#define TCON_TF1    0x80

#define IE_EX0      0x01
#define IE_ET0      0x02
#define IE_EX1      0x04
#define IE_ET1      0x08
#define IE_ES       0x10
#define IE_EA       0x80

#define MACHINE_CLOCKS  12

i8051::i8051(InterfaceManager *im, EmulatorConfigDevice *cd):
      CPU(im, cd)
    , i_p0(this, im, 8, "p0", MODE_W)
    , i_p1(this, im, 8, "p1", MODE_W)
    , i_p2(this, im, 8, "p2", MODE_W)
    , i_p3(this, im, 8, "p3", MODE_W)
    , i_p0_in(this, im, 8, "p0_in", MODE_R)
    , i_p1_in(this, im, 8, "p1_in", MODE_R)
    , i_p2_in(this, im, 8, "p2_in", MODE_R)
    , i_p3_in(this, im, 8, "p3_in", MODE_R, CALLBACK_P3)
    , i_reset(this, im, 1, "reset", MODE_R, CALLBACK_RESET)
{
    m_out[0] = &i_p0; m_out[1] = &i_p1; m_out[2] = &i_p2; m_out[3] = &i_p3;
    m_in[0] = &i_p0_in; m_in[1] = &i_p1_in; m_in[2] = &i_p2_in; m_in[3] = &i_p3_in;
    memset(m_iram, 0, sizeof(m_iram));
    memset(m_sfr, 0, sizeof(m_sfr));
}

emulator::Result i8051::load_config(SystemData *sd)
{
    emulator::Result res = CPU::load_config(sd);
    if (!res) return res;
    m_t0_clock = read_confg_value(cd, "t0_clock", false, (unsigned int)0);
    m_t1_clock = read_confg_value(cd, "t1_clock", false, (unsigned int)0);
    return emulator::Result::ok();
}

void i8051::reset_chip()
{
    memset(m_sfr, 0, sizeof(m_sfr));
    m_sfr[SFR_P0 & 0x7F] = 0xFF;
    m_sfr[SFR_P1 & 0x7F] = 0xFF;
    m_sfr[SFR_P2 & 0x7F] = 0xFF;
    m_sfr[SFR_P3 & 0x7F] = 0xFF;
    m_sfr[SFR_SP & 0x7F] = 7;
    m_pc = 0;
    m_in_irq[0] = m_in_irq[1] = false;
    m_skip_irq = false;
    m_t0_fraction = m_t1_fraction = 0;
    for (unsigned int i = 0; i < 4; i++) drive_port(i);
}

void i8051::reset(bool cold)
{
    CPU::reset(cold);
    if (cold) memset(m_iram, 0, sizeof(m_iram));
    reset_chip();
}

void i8051::drive_port(unsigned int n)
{
    m_out[n]->change(m_sfr[(SFR_P0 + n * 0x10) & 0x7F]);
}

uint8_t i8051::port_pins(unsigned int n) const
{
    const uint8_t latch = m_sfr[(SFR_P0 + n * 0x10) & 0x7F];
    return m_in[n]->linked != 0 ? (uint8_t)(latch & m_in[n]->value) : latch;
}

void i8051::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == CALLBACK_RESET) {
        const bool held = (new_value & 1) != 0;
        if (held && !m_held) reset_chip();
        m_held = held;
        return;
    }
    if (callback_id == CALLBACK_P3) {
        // Спад на INT0/INT1 запоминается и при запрете: разряды IE0, IE1 TCON
        const unsigned int fall = ~new_value & old_value & m_sfr[SFR_P3 & 0x7F];
        uint8_t &tcon = m_sfr[SFR_TCON & 0x7F];
        if ((fall & 4) && (tcon & TCON_IT0)) tcon |= TCON_IE0;
        if ((fall & 8) && (tcon & TCON_IT1)) tcon |= TCON_IE1;
    }
}

unsigned int i8051::get_pc() { return m_pc; }

unsigned int i8051::read_mem(unsigned int address)
{
    return mm->read(address & 0xFFFF);
}

void i8051::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address & 0xFFFF, data);
}

uint8_t i8051::fetch()
{
    return (uint8_t)mm->read(m_pc++);
}

uint16_t i8051::fetch16()
{
    const unsigned int hi = fetch();
    return (uint16_t)((hi << 8) | fetch());
}

// Обращение в SP выше 7F у кристалла с 128 байтами ОЗУ ни к чему не ведёт: чтение
// даёт случайное, здесь - зеркало
uint8_t i8051::read_iram(unsigned int address) const { return m_iram[address & 0x7F]; }
void i8051::write_iram(unsigned int address, uint8_t value) { m_iram[address & 0x7F] = value; }

uint8_t i8051::read_direct(unsigned int address)
{
    if (address < 0x80) return m_iram[address];
    switch (address) {
    case SFR_P0: return port_pins(0);
    case SFR_P1: return port_pins(1);
    case SFR_P2: return port_pins(2);
    case SFR_P3: return port_pins(3);
    default: return m_sfr[address & 0x7F];
    }
}

// Команды чтение-модификация-запись берут защёлку порта, а не выводы
uint8_t i8051::read_direct_latch(unsigned int address)
{
    return (address < 0x80) ? m_iram[address] : m_sfr[address & 0x7F];
}

void i8051::write_direct(unsigned int address, uint8_t value)
{
    if (address < 0x80) {
        m_iram[address] = value;
        return;
    }
    m_sfr[address & 0x7F] = value;
    switch (address) {
    case SFR_P0: drive_port(0); break;
    case SFR_P1: drive_port(1); break;
    case SFR_P2: drive_port(2); break;
    case SFR_P3: drive_port(3); break;
    case SFR_ACC:
    case SFR_PSW: update_parity(); break;
    case SFR_IE:
    case SFR_IP: m_skip_irq = true; break;
    case SFR_SBUF: note_unsupported("i8051: serial port"); break;
    default: break;
    }
}

// Адрес разряда: до 7F - байты 20-2F, выше - SFR с адресом, кратным 8
bool i8051::read_bit(unsigned int bit, bool latch)
{
    const unsigned int address = (bit < 0x80) ? (0x20 + (bit >> 3)) : (bit & 0xF8);
    const uint8_t v = latch ? read_direct_latch(address) : read_direct(address);
    return (v >> (bit & 7)) & 1;
}

void i8051::write_bit(unsigned int bit, bool value)
{
    const unsigned int address = (bit < 0x80) ? (0x20 + (bit >> 3)) : (bit & 0xF8);
    uint8_t v = read_direct_latch(address);
    if (value) v |= (uint8_t)(1u << (bit & 7));
    else v &= (uint8_t)~(1u << (bit & 7));
    write_direct(address, v);
}

void i8051::push(uint8_t value)
{
    uint8_t &sp = m_sfr[SFR_SP & 0x7F];
    sp++;
    write_iram(sp, value);
}

uint8_t i8051::pop()
{
    uint8_t &sp = m_sfr[SFR_SP & 0x7F];
    const uint8_t v = read_iram(sp);
    sp--;
    return v;
}

uint8_t i8051::acc() const { return m_sfr[SFR_ACC & 0x7F]; }

void i8051::update_parity()
{
    uint8_t v = acc();
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    uint8_t &psw = m_sfr[SFR_PSW & 0x7F];
    psw = (uint8_t)((psw & ~PSW_P) | (v & 1));
}

void i8051::set_acc(uint8_t value)
{
    m_sfr[SFR_ACC & 0x7F] = value;
    update_parity();
}

bool i8051::carry() const { return (m_sfr[SFR_PSW & 0x7F] & PSW_CY) != 0; }

void i8051::set_carry(bool value)
{
    uint8_t &psw = m_sfr[SFR_PSW & 0x7F];
    psw = (uint8_t)(value ? (psw | PSW_CY) : (psw & ~PSW_CY));
}

void i8051::set_flags(bool c, bool ac, bool ov)
{
    uint8_t &psw = m_sfr[SFR_PSW & 0x7F];
    psw &= (uint8_t)~(PSW_CY | PSW_AC | PSW_OV);
    if (c) psw |= PSW_CY;
    if (ac) psw |= PSW_AC;
    if (ov) psw |= PSW_OV;
}

uint8_t i8051::reg(unsigned int n) const
{
    return m_iram[(m_sfr[SFR_PSW & 0x7F] & PSW_RS) | n];
}

void i8051::set_reg(unsigned int n, uint8_t value)
{
    m_iram[(m_sfr[SFR_PSW & 0x7F] & PSW_RS) | n] = value;
}

uint8_t i8051::indirect_address(unsigned int opcode) const
{
    return reg(opcode & 1);
}

uint16_t i8051::movx_address_ri(unsigned int opcode) const
{
    return (uint16_t)((m_sfr[SFR_P2 & 0x7F] << 8) | reg(opcode & 1));
}

void i8051::add(uint8_t value, bool with_carry)
{
    const unsigned int a = acc();
    const unsigned int c = with_carry && carry() ? 1 : 0;
    const unsigned int r = a + value + c;
    const bool ac = ((a & 15) + (value & 15) + c) > 15;
    const bool ov = (~(a ^ value) & (a ^ r) & 0x80) != 0;
    set_flags(r > 0xFF, ac, ov);
    set_acc((uint8_t)r);
}

void i8051::subb(uint8_t value)
{
    const unsigned int a = acc();
    const unsigned int c = carry() ? 1 : 0;
    const unsigned int r = a - value - c;
    const bool ac = (a & 15) < (value & 15) + c;
    const bool ov = ((a ^ value) & (a ^ r) & 0x80) != 0;
    set_flags(r > 0xFF, ac, ov);
    set_acc((uint8_t)r);
}

void i8051::jump_relative(uint8_t offset)
{
    m_pc = (uint16_t)(m_pc + (int8_t)offset);
}

// Режимы таймера: 0 - 13 разрядов, 1 - 16, 2 - 8 с перезагрузкой, 3 - у T0 два
// 8-разрядных счётчика (второй на такте и управлении T1), у T1 - стоп. Пока T0 в
// режиме 3, TR1 и TF1 заняты: T1 идёт без них, а останавливает его только свой режим 3
void i8051::step_timer(unsigned int n, unsigned int machine_cycles)
{
    uint8_t &tcon = m_sfr[SFR_TCON & 0x7F];
    const unsigned int tmod = (m_sfr[SFR_TMOD & 0x7F] >> (n * 4)) & 15;
    const unsigned int mode = tmod & 3;
    const bool free_run = n == 1 && (m_sfr[SFR_TMOD & 0x7F] & 3) == 3;
    const uint8_t tr = n ? TCON_TR1 : TCON_TR0;
    const uint8_t tf = free_run ? 0 : (n ? TCON_TF1 : TCON_TF0);
    uint8_t &tl = m_sfr[(n ? SFR_TL1 : SFR_TL0) & 0x7F];
    uint8_t &th = m_sfr[(n ? SFR_TH1 : SFR_TH0) & 0x7F];

    // T0 в режиме 3 забрал TR1 и TF1 себе: TH0 считает такты
    if (free_run && (tcon & TCON_TR1)) {
        const unsigned int v = m_sfr[SFR_TH0 & 0x7F] + machine_cycles;
        m_sfr[SFR_TH0 & 0x7F] = (uint8_t)v;
        if (v > 0xFF) tcon |= TCON_TF1;
    }
    if (n == 1 && mode == 3) return;

    if (!free_run) {
        if ((tcon & tr) == 0) return;
        // GATE: счёт идёт, только пока на INT высокий уровень
        if ((tmod & 8) != 0 && (port_pins(3) & (n ? 8 : 4)) == 0) return;
    }

    unsigned int ticks = machine_cycles;
    if (tmod & 4) {
        // Счётчик внешних импульсов
        const unsigned int rate = n ? m_t1_clock : m_t0_clock;
        uint64_t &fraction = n ? m_t1_fraction : m_t0_fraction;
        fraction += (uint64_t)machine_cycles * rate * MACHINE_CLOCKS;
        const uint64_t c = clock != 0 ? clock : 1;
        ticks = (unsigned int)(fraction / c);
        fraction -= (uint64_t)ticks * c;
        if (ticks == 0) return;
    }

    switch (mode) {
    case 0: {
        const unsigned int v = (((unsigned int)th << 5) | (tl & 31)) + ticks;
        th = (uint8_t)(v >> 5);
        tl = (uint8_t)((tl & 0xE0) | (v & 31));
        if (v > 0x1FFF) tcon |= tf;
        break;
    }
    case 1: {
        const unsigned int v = (((unsigned int)th << 8) | tl) + ticks;
        th = (uint8_t)(v >> 8);
        tl = (uint8_t)v;
        if (v > 0xFFFF) tcon |= tf;
        break;
    }
    case 2: {
        const unsigned int v = tl + ticks;
        if (v > 0xFF) {
            // При коротком периоде за команду бывает и не одна перезагрузка
            tl = (uint8_t)(th + (v - 0x100) % (0x100u - th));
            tcon |= tf;
        } else
            tl = (uint8_t)v;
        break;
    }
    default: {
        const unsigned int v = tl + ticks;
        tl = (uint8_t)v;
        if (v > 0xFF) tcon |= tf;
        break;
    }
    }
}

void i8051::step_timers(unsigned int machine_cycles)
{
    if ((m_sfr[SFR_TCON & 0x7F] & (TCON_TR0 | TCON_TR1)) != 0 || (m_sfr[SFR_TMOD & 0x7F] & 3) == 3) {
        step_timer(0, machine_cycles);
        step_timer(1, machine_cycles);
    }
}

// Опрос после команды: запрос старшего уровня прерывает младший, порядок внутри
// уровня INT0, T0, INT1, T1, порт
bool i8051::check_interrupts()
{
    const uint8_t ie = m_sfr[SFR_IE & 0x7F];
    if ((ie & IE_EA) == 0) return false;

    uint8_t &tcon = m_sfr[SFR_TCON & 0x7F];
    // Уровневый вход: запрос, пока на выводе ноль
    const uint8_t pins = port_pins(3);
    const bool ie0 = (tcon & TCON_IT0) ? (tcon & TCON_IE0) != 0 : (pins & 4) == 0;
    const bool ie1 = (tcon & TCON_IT1) ? (tcon & TCON_IE1) != 0 : (pins & 8) == 0;

    const bool pending[5] = {
        ie0 && (ie & IE_EX0), (tcon & TCON_TF0) && (ie & IE_ET0),
        ie1 && (ie & IE_EX1), (tcon & TCON_TF1) && (ie & IE_ET1),
        (m_sfr[SFR_SCON & 0x7F] & 3) && (ie & IE_ES)
    };
    const uint8_t ip = m_sfr[SFR_IP & 0x7F];
    for (int level = 1; level >= 0; level--) {
        if (m_in_irq[1] || (level == 0 && m_in_irq[0])) return false;
        for (unsigned int i = 0; i < 5; i++) {
            if (!pending[i] || (((ip >> i) & 1) != 0) != (level == 1)) continue;
            m_in_irq[level] = true;
            if (i == 0 && (tcon & TCON_IT0)) tcon &= (uint8_t)~TCON_IE0;
            if (i == 1) tcon &= (uint8_t)~TCON_TF0;
            if (i == 2 && (tcon & TCON_IT1)) tcon &= (uint8_t)~TCON_IE1;
            if (i == 3) tcon &= (uint8_t)~TCON_TF1;
            push((uint8_t)m_pc);
            push((uint8_t)(m_pc >> 8));
            m_pc = (uint16_t)(3 + i * 8);
            return true;
        }
    }
    return false;
}

void i8051::execute_opcode(uint8_t op)
{
    m_cycles = 1;
    const unsigned int rn = op & 7;
    const unsigned int low = op & 15;

    // AJMP и ACALL: страница 2 КБ текущего PC
    if ((op & 0x1F) == 0x01) {
        const unsigned int a = ((op & 0xE0u) << 3) | fetch();
        m_pc = (uint16_t)((m_pc & 0xF800) | a);
        m_cycles = 2;
        return;
    }
    if ((op & 0x1F) == 0x11) {
        const unsigned int a = ((op & 0xE0u) << 3) | fetch();
        push((uint8_t)m_pc);
        push((uint8_t)(m_pc >> 8));
        m_pc = (uint16_t)((m_pc & 0xF800) | a);
        m_cycles = 2;
        return;
    }

    // Операнд обычных групп: 04 - A или #, 05 - direct, 06-07 - @Ri, 08-0F - Rn,
    // номер которого - младшие три разряда
    const bool ri = low == 6 || low == 7;

    switch (op >> 4) {
    case 0x0:
        switch (low) {
        case 0x0: return;                                           // NOP
        case 0x2: m_pc = fetch16(); m_cycles = 2; return;           // LJMP
        case 0x3: {                                                 // RR A
            const uint8_t a = acc();
            set_acc((uint8_t)((a >> 1) | (a << 7)));
            return;
        }
        case 0x4: set_acc((uint8_t)(acc() + 1)); return;            // INC A
        case 0x5: {                                                 // INC direct
            const unsigned int d = fetch();
            write_direct(d, (uint8_t)(read_direct_latch(d) + 1));
            return;
        }
        default:
            if (ri) write_iram(indirect_address(op), (uint8_t)(read_iram(indirect_address(op)) + 1));
            else set_reg(rn, (uint8_t)(reg(rn) + 1));
            return;
        }
    case 0x1:
        switch (low) {
        case 0x0: {                                                 // JBC bit,rel
            const unsigned int b = fetch();
            const uint8_t rel = fetch();
            m_cycles = 2;
            if (read_bit(b, true)) {
                write_bit(b, false);
                jump_relative(rel);
            }
            return;
        }
        case 0x2: {                                                 // LCALL
            const uint16_t a = fetch16();
            push((uint8_t)m_pc);
            push((uint8_t)(m_pc >> 8));
            m_pc = a;
            m_cycles = 2;
            return;
        }
        case 0x3: {                                                 // RRC A
            const uint8_t a = acc();
            const bool c = carry();
            set_carry((a & 1) != 0);
            set_acc((uint8_t)((a >> 1) | (c ? 0x80 : 0)));
            return;
        }
        case 0x4: set_acc((uint8_t)(acc() - 1)); return;            // DEC A
        case 0x5: {                                                 // DEC direct
            const unsigned int d = fetch();
            write_direct(d, (uint8_t)(read_direct_latch(d) - 1));
            return;
        }
        default:
            if (ri) write_iram(indirect_address(op), (uint8_t)(read_iram(indirect_address(op)) - 1));
            else set_reg(rn, (uint8_t)(reg(rn) - 1));
            return;
        }
    case 0x2:
    case 0x3:
        switch (low) {
        case 0x0: {                                                 // JB, JNB
            const unsigned int b = fetch();
            const uint8_t rel = fetch();
            m_cycles = 2;
            if (read_bit(b, false) == ((op >> 4) == 2)) jump_relative(rel);
            return;
        }
        case 0x2: {                                                 // RET, RETI
            const unsigned int hi = pop();
            m_pc = (uint16_t)((hi << 8) | pop());
            m_cycles = 2;
            if ((op >> 4) == 3) {
                if (m_in_irq[1]) m_in_irq[1] = false;
                else m_in_irq[0] = false;
                m_skip_irq = true;
            }
            return;
        }
        case 0x3: {                                                 // RL A, RLC A
            const uint8_t a = acc();
            if ((op >> 4) == 2) set_acc((uint8_t)((a << 1) | (a >> 7)));
            else {
                const bool c = carry();
                set_carry((a & 0x80) != 0);
                set_acc((uint8_t)((a << 1) | (c ? 1 : 0)));
            }
            return;
        }
        default: {                                                  // ADD, ADDC
            uint8_t v;
            if (low == 4) v = fetch();
            else if (low == 5) v = read_direct(fetch());
            else if (ri) v = read_iram(indirect_address(op));
            else v = reg(rn);
            add(v, (op >> 4) == 3);
            return;
        }
        }
    case 0x4:
    case 0x5:
    case 0x6: {
        const unsigned int group = op >> 4;
        const uint8_t a = acc();
        if (low == 0) {                                             // JC, JNC, JZ
            const uint8_t rel = fetch();
            m_cycles = 2;
            const bool taken = group == 4 ? carry() : (group == 5 ? !carry() : a == 0);
            if (taken) jump_relative(rel);
            return;
        }
        if (low == 2 || low == 3) {                                 // direct,A и direct,#
            const unsigned int d = fetch();
            const uint8_t v = (low == 2) ? a : fetch();
            if (low == 3) m_cycles = 2;
            const uint8_t x = read_direct_latch(d);
            write_direct(d, (uint8_t)(group == 4 ? (x | v) : (group == 5 ? (x & v) : (x ^ v))));
            return;
        }
        uint8_t v;
        if (low == 4) v = fetch();
        else if (low == 5) v = read_direct(fetch());
        else if (ri) v = read_iram(indirect_address(op));
        else v = reg(rn);
        set_acc((uint8_t)(group == 4 ? (a | v) : (group == 5 ? (a & v) : (a ^ v))));
        return;
    }
    case 0x7:
        switch (low) {
        case 0x0: {                                                 // JNZ
            const uint8_t rel = fetch();
            m_cycles = 2;
            if (acc() != 0) jump_relative(rel);
            return;
        }
        case 0x2: {                                                 // ORL C,bit
            const unsigned int b = fetch();
            m_cycles = 2;
            if (read_bit(b, false)) set_carry(true);
            return;
        }
        case 0x3:                                                   // JMP @A+DPTR
            m_pc = (uint16_t)(acc() + ((m_sfr[SFR_DPH & 0x7F] << 8) | m_sfr[SFR_DPL & 0x7F]));
            m_cycles = 2;
            return;
        case 0x4: set_acc(fetch()); return;                         // MOV A,#
        case 0x5: {                                                 // MOV direct,#
            const unsigned int d = fetch();
            write_direct(d, fetch());
            m_cycles = 2;
            return;
        }
        default:
            if (ri) write_iram(indirect_address(op), fetch());
            else set_reg(rn, fetch());
            return;
        }
    case 0x8:
        switch (low) {
        case 0x0: {                                                 // SJMP
            const uint8_t rel = fetch();
            jump_relative(rel);
            m_cycles = 2;
            return;
        }
        case 0x2: {                                                 // ANL C,bit
            const unsigned int b = fetch();
            m_cycles = 2;
            if (!read_bit(b, false)) set_carry(false);
            return;
        }
        case 0x3:                                                   // MOVC A,@A+PC
            set_acc((uint8_t)mm->read((uint16_t)(m_pc + acc())));
            m_cycles = 2;
            return;
        case 0x4: {                                                 // DIV AB
            const uint8_t a = acc();
            uint8_t &b = m_sfr[SFR_B & 0x7F];
            m_cycles = 4;
            set_flags(false, (m_sfr[SFR_PSW & 0x7F] & PSW_AC) != 0, b == 0);
            if (b != 0) {
                const uint8_t q = (uint8_t)(a / b);
                b = (uint8_t)(a % b);
                set_acc(q);
            }
            return;
        }
        case 0x5: {                                                 // MOV direct,direct
            const unsigned int src = fetch();
            const unsigned int dst = fetch();
            write_direct(dst, read_direct(src));
            m_cycles = 2;
            return;
        }
        default: {                                                  // MOV direct,@Ri/Rn
            const unsigned int d = fetch();
            write_direct(d, ri ? read_iram(indirect_address(op)) : reg(rn));
            m_cycles = 2;
            return;
        }
        }
    case 0x9:
        switch (low) {
        case 0x0: {                                                 // MOV DPTR,#
            m_sfr[SFR_DPH & 0x7F] = fetch();
            m_sfr[SFR_DPL & 0x7F] = fetch();
            m_cycles = 2;
            return;
        }
        case 0x2: {                                                 // MOV bit,C
            write_bit(fetch(), carry());
            m_cycles = 2;
            return;
        }
        case 0x3:                                                   // MOVC A,@A+DPTR
            set_acc((uint8_t)mm->read((uint16_t)(acc() + ((m_sfr[SFR_DPH & 0x7F] << 8) | m_sfr[SFR_DPL & 0x7F]))));
            m_cycles = 2;
            return;
        default: {                                                  // SUBB
            uint8_t v;
            if (low == 4) v = fetch();
            else if (low == 5) v = read_direct(fetch());
            else if (ri) v = read_iram(indirect_address(op));
            else v = reg(rn);
            subb(v);
            return;
        }
        }
    case 0xA:
        switch (low) {
        case 0x0: {                                                 // ORL C,/bit
            const unsigned int b = fetch();
            m_cycles = 2;
            if (!read_bit(b, false)) set_carry(true);
            return;
        }
        case 0x2: {                                                 // MOV C,bit
            set_carry(read_bit(fetch(), false));
            return;
        }
        case 0x3: {                                                 // INC DPTR
            unsigned int v = ((m_sfr[SFR_DPH & 0x7F] << 8) | m_sfr[SFR_DPL & 0x7F]) + 1;
            m_sfr[SFR_DPH & 0x7F] = (uint8_t)(v >> 8);
            m_sfr[SFR_DPL & 0x7F] = (uint8_t)v;
            m_cycles = 2;
            return;
        }
        case 0x4: {                                                 // MUL AB
            const unsigned int r = (unsigned int)acc() * m_sfr[SFR_B & 0x7F];
            m_cycles = 4;
            m_sfr[SFR_B & 0x7F] = (uint8_t)(r >> 8);
            set_flags(false, (m_sfr[SFR_PSW & 0x7F] & PSW_AC) != 0, r > 0xFF);
            set_acc((uint8_t)r);
            return;
        }
        case 0x5:
            note_unsupported("i8051: reserved opcode A5");
            return;
        default: {                                                  // MOV @Ri/Rn,direct
            const uint8_t v = read_direct(fetch());
            if (ri) write_iram(indirect_address(op), v);
            else set_reg(rn, v);
            m_cycles = 2;
            return;
        }
        }
    case 0xB:
        switch (low) {
        case 0x0: {                                                 // ANL C,/bit
            const unsigned int b = fetch();
            m_cycles = 2;
            if (read_bit(b, false)) set_carry(false);
            return;
        }
        case 0x2: {                                                 // CPL bit
            const unsigned int b = fetch();
            write_bit(b, !read_bit(b, true));
            return;
        }
        case 0x3: set_carry(!carry()); return;                      // CPL C
        default: {                                                  // CJNE
            uint8_t left, right;
            if (low == 4) { left = acc(); right = fetch(); }
            else if (low == 5) { left = acc(); right = read_direct(fetch()); }
            else if (ri) { left = read_iram(indirect_address(op)); right = fetch(); }
            else { left = reg(rn); right = fetch(); }
            const uint8_t rel = fetch();
            m_cycles = 2;
            set_carry(left < right);
            if (left != right) jump_relative(rel);
            return;
        }
        }
    case 0xC:
        switch (low) {
        case 0x0: push(read_direct(fetch())); m_cycles = 2; return;     // PUSH
        case 0x2: write_bit(fetch(), false); return;                    // CLR bit
        case 0x3: set_carry(false); return;                             // CLR C
        case 0x4: {                                                     // SWAP A
            const uint8_t a = acc();
            set_acc((uint8_t)((a << 4) | (a >> 4)));
            return;
        }
        case 0x5: {                                                     // XCH A,direct
            const unsigned int d = fetch();
            const uint8_t v = read_direct(d);
            write_direct(d, acc());
            set_acc(v);
            return;
        }
        default: {                                                      // XCH A,@Ri/Rn
            const uint8_t a = acc();
            if (ri) {
                const uint8_t addr = indirect_address(op);
                set_acc(read_iram(addr));
                write_iram(addr, a);
            } else {
                set_acc(reg(rn));
                set_reg(rn, a);
            }
            return;
        }
        }
    case 0xD:
        switch (low) {
        case 0x0: {                                                     // POP
            const unsigned int d = fetch();
            write_direct(d, pop());
            m_cycles = 2;
            return;
        }
        case 0x2: write_bit(fetch(), true); return;                     // SETB bit
        case 0x3: set_carry(true); return;                              // SETB C
        case 0x4: {                                                     // DA A
            unsigned int a = acc();
            bool c = carry();
            if ((a & 15) > 9 || (m_sfr[SFR_PSW & 0x7F] & PSW_AC) != 0) {
                a += 6;
                if (a > 0xFF) c = true;
            }
            if ((a & 0x1F0) > 0x90 || c) {
                a += 0x60;
                if (a > 0xFF) c = true;
            }
            set_carry(c);
            set_acc((uint8_t)a);
            return;
        }
        case 0x5: {                                                     // DJNZ direct,rel
            const unsigned int d = fetch();
            const uint8_t rel = fetch();
            const uint8_t v = (uint8_t)(read_direct_latch(d) - 1);
            write_direct(d, v);
            m_cycles = 2;
            if (v != 0) jump_relative(rel);
            return;
        }
        case 0x6:
        case 0x7: {                                                     // XCHD A,@Ri
            const uint8_t addr = indirect_address(op);
            const uint8_t m = read_iram(addr);
            const uint8_t a = acc();
            write_iram(addr, (uint8_t)((m & 0xF0) | (a & 15)));
            set_acc((uint8_t)((a & 0xF0) | (m & 15)));
            return;
        }
        default: {                                                      // DJNZ Rn,rel
            const uint8_t rel = fetch();
            const uint8_t v = (uint8_t)(reg(rn) - 1);
            set_reg(rn, v);
            m_cycles = 2;
            if (v != 0) jump_relative(rel);
            return;
        }
        }
    case 0xE:
        switch (low) {
        case 0x0:                                                       // MOVX A,@DPTR
            set_acc((uint8_t)mm->read_port((m_sfr[SFR_DPH & 0x7F] << 8) | m_sfr[SFR_DPL & 0x7F]));
            m_cycles = 2;
            return;
        case 0x2:
        case 0x3:                                                       // MOVX A,@Ri
            set_acc((uint8_t)mm->read_port(movx_address_ri(op)));
            m_cycles = 2;
            return;
        case 0x4: set_acc(0); return;                                   // CLR A
        case 0x5: set_acc(read_direct(fetch())); return;                // MOV A,direct
        default:
            set_acc(ri ? read_iram(indirect_address(op)) : reg(rn));    // MOV A,@Ri/Rn
            return;
        }
    default:
        switch (low) {
        case 0x0:                                                       // MOVX @DPTR,A
            mm->write_port((m_sfr[SFR_DPH & 0x7F] << 8) | m_sfr[SFR_DPL & 0x7F], acc());
            m_cycles = 2;
            return;
        case 0x2:
        case 0x3:                                                       // MOVX @Ri,A
            mm->write_port(movx_address_ri(op), acc());
            m_cycles = 2;
            return;
        case 0x4: set_acc((uint8_t)~acc()); return;                     // CPL A
        case 0x5: write_direct(fetch(), acc()); return;                 // MOV direct,A
        default:
            if (ri) write_iram(indirect_address(op), acc());            // MOV @Ri/Rn,A
            else set_reg(rn, acc());
            return;
        }
    }
}

unsigned int i8051::execute()
{
    if (reset_mode) {
        reset_chip();
        reset_mode = false;
    }
    if (m_debug == DEBUG_STOPPED) return 0;
    if (m_held) return MACHINE_CLOCKS;

    const unsigned int held = take_hold();
    if (held > 0) return held;

    if (m_debug != DEBUG_OFF) debug_before();

    if (m_skip_irq) m_skip_irq = false;
    else if (check_interrupts()) {
        step_timers(2);
        if (m_debug != DEBUG_OFF) debug_after();
        return 2 * MACHINE_CLOCKS;
    }

    execute_opcode(fetch());
    step_timers(m_cycles);

    if (m_debug != DEBUG_OFF) debug_after();
    return m_cycles * MACHINE_CLOCKS;
}

DisAsm * i8051::create_disasm(MAYBE_UNUSED const std::string &data_path, emulator::Result &res)
{
    res = emulator::Result::ok();
    return new DisAsmI8051();
}

std::vector<std::pair<std::string, std::string>> i8051::get_registers()
{
    return {
        {"A", hex_str(acc(), 2)},
        {"B", hex_str(m_sfr[SFR_B & 0x7F], 2)},
        {"R0", hex_str(reg(0), 2)},
        {"R1", hex_str(reg(1), 2)},
        {"R2", hex_str(reg(2), 2)},
        {"R3", hex_str(reg(3), 2)},
        {"R4", hex_str(reg(4), 2)},
        {"R5", hex_str(reg(5), 2)},
        {"R6", hex_str(reg(6), 2)},
        {"R7", hex_str(reg(7), 2)},
        {"-1", ""},
        {"SP", hex_str(m_sfr[SFR_SP & 0x7F], 2)},
        {"DPTR", hex_str((m_sfr[SFR_DPH & 0x7F] << 8) | m_sfr[SFR_DPL & 0x7F], 4)},
        {"PC", hex_str(m_pc, 4)},
        {"PSW", hex_str(m_sfr[SFR_PSW & 0x7F], 2)}
    };
}

std::vector<std::pair<std::string, std::string>> i8051::get_flags()
{
    const uint8_t psw = m_sfr[SFR_PSW & 0x7F];
    return {
        {"C", std::to_string((psw & PSW_CY) ? 1 : 0)},
        {"AC", std::to_string((psw & PSW_AC) ? 1 : 0)},
        {"OV", std::to_string((psw & PSW_OV) ? 1 : 0)},
        {"P", std::to_string(psw & PSW_P)}
    };
}

unsigned int i8051::get_command()
{
    return peek_mem(m_pc) & 0xFF;
}

void i8051::set_context_value(const std::string &name, unsigned int value)
{
    if (name == "PC") m_pc = (uint16_t)value;
    else if (name == "A") set_acc((uint8_t)value);
    else if (name == "B") m_sfr[SFR_B & 0x7F] = (uint8_t)value;
    else if (name == "SP") m_sfr[SFR_SP & 0x7F] = (uint8_t)value;
    else if (name == "PSW") m_sfr[SFR_PSW & 0x7F] = (uint8_t)value;
    else if (name == "DPTR") {
        m_sfr[SFR_DPH & 0x7F] = (uint8_t)(value >> 8);
        m_sfr[SFR_DPL & 0x7F] = (uint8_t)value;
    } else if (name.size() == 2 && name[0] == 'R' && name[1] >= '0' && name[1] <= '7')
        set_reg(name[1] - '0', (uint8_t)value);
}

bool i8051::set_flag(const std::string &name, bool value)
{
    static const char * names[] = {"C", "AC", "OV"};
    static const uint8_t bits[] = {PSW_CY, PSW_AC, PSW_OV};
    uint8_t &psw = m_sfr[SFR_PSW & 0x7F];
    for (unsigned int i = 0; i < 3; i++)
        if (name == names[i]) {
            psw = (uint8_t)(value ? (psw | bits[i]) : (psw & ~bits[i]));
            return true;
        }
    return false;
}

bool i8051::get_sp(unsigned int &sp)
{
    sp = m_sfr[SFR_SP & 0x7F];
    return true;
}

// Адрес записи стека во внутреннем ОЗУ: читает его stack_peek(). Стек растёт вверх,
// поэтому запись i лежит на i байт ниже вершины
bool i8051::stack_address(int i, unsigned int &address)
{
    address = (unsigned int)((m_sfr[SFR_SP & 0x7F] - i) & 0x7F);
    return true;
}

std::vector<DeviceFieldInfo> i8051::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = CPU::get_device_fields();
    r.push_back({"iram", "Internal RAM, a range or one address", true});
    r.push_back({"sfr",  "Special function register by address", true});
    return r;
}

bool i8051::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "iram") {
        out.numeric = true;
        out.width = 8;
        out.start = from;
        const unsigned int last = (to != 0) ? to : ((from != 0) ? from : 0x7F);
        for (unsigned int i = from; i <= last && i < 0x80; i++) out.values.push_back(m_iram[i]);
        return true;
    }
    if (field == "sfr") {
        out.numeric = true;
        out.width = 8;
        out.values.push_back(m_sfr[from & 0x7F]);
        return true;
    }
    return CPU::get_field(field, from, to, out);
}

void i8051::save_state(StateWriter &w)
{
    CPU::save_state(w);
    w.array("iram", m_iram, sizeof(m_iram));
    w.array("sfr", m_sfr, sizeof(m_sfr));
    w.u("pc", m_pc);
    w.b("in_irq0", m_in_irq[0]);
    w.b("in_irq1", m_in_irq[1]);
    w.b("held", m_held);
    w.b("skip_irq", m_skip_irq);
    w.n64("t0_fraction", m_t0_fraction);
    w.n64("t1_fraction", m_t1_fraction);
}

emulator::Result i8051::load_state(const StateReader &r)
{
    emulator::Result res = CPU::load_state(r);
    if (!res) return res;
    r.array("iram", m_iram, sizeof(m_iram));
    r.array("sfr", m_sfr, sizeof(m_sfr));
    unsigned int pc = 0;
    if (r.u("pc", pc)) m_pc = (uint16_t)pc;
    r.b("in_irq0", m_in_irq[0]);
    r.b("in_irq1", m_in_irq[1]);
    r.b("held", m_held);
    r.b("skip_irq", m_skip_irq);
    r.n64("t0_fraction", m_t0_fraction);
    r.n64("t1_fraction", m_t1_fraction);
    return emulator::Result::ok();
}

ComputerDevice * create_i8051(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new i8051(im, cd);
}
