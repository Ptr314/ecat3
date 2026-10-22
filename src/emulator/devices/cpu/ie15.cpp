// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Microprogrammed processor (МПУ) of the 15ИЭ-00-013 terminal

#include <cstring>

#include "ie15.h"
#include "emulator/disasm.h"
#include "emulator/utils.h"

#define IE15_FLAG_PORT  020

//--------------------------- Дизассемблер ----------------------------------//

// Мнемоники таблицы 7 ТО, числа - восьмеричные, как в ТО. Табличный
// дизассемблер не годится: JMP переходит на адрес плюс 1
class DisAsmIE15: public DisAsm
{
public:
    DisAsmIE15() { max_command_length = 2; }
    emulator::Result load_file(MAYBE_UNUSED const std::string &file_name) override { return emulator::Result::ok(); }
    unsigned int disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string * output) override;
};

unsigned int DisAsmIE15::disassemle(CommandBytes bytes, MAYBE_UNUSED unsigned int PC, unsigned int max_len, std::string * output)
{
    if (max_len < 1) { *output = "?"; return 0; }
    const uint8_t op = (*bytes)[0];
    const unsigned lo = op & 0x0F;
    const std::string r = "r" + std::to_string(lo);
    const bool two = (op & 0xF0) == 0x10 || (op & 0xF0) == 0x20 || op == 0x30;
    if (two && max_len < 2) { *output = "?"; return 0; }
    const uint8_t a = two ? (*bytes)[1] : 0;
    switch (op & 0xF0) {
        case 0x00: *output = "add  " + r; break;
        case 0x10: *output = "jmp  " + oct_str((((lo << 8) | a) + 1) & 0xFFF, 4); break;
        case 0x20: *output = "ldc  " + r + ", " + oct_str(a, 3); break;
        case 0x30:
            if (op == 0x30) *output = "lca  " + oct_str(a, 3);
            else if (op == 0x33) *output = "ral";
            else if (op == 0x35) *output = "rar";
            else *output = "?";
            break;
        case 0x40: *output = "dsr  " + r; break;
        case 0x50:
            switch (op) {
                case 0x50: *output = "isn"; break;
                case 0x51: *output = "inc"; break;
                case 0x52: *output = "dsn"; break;
                case 0x58: *output = "ise"; break;
                case 0x5A: *output = "dse"; break;
                case 0x5B: *output = "dec"; break;
                case 0x5D: *output = "com"; break;
                case 0x5F: *output = "clr"; break;
                default:   *output = "?"; break;
            }
            break;
        case 0x60: *output = "lla  " + oct_str(lo, 2); break;
        case 0x70: *output = "jmi  " + r; break;
        case 0x80:
            if (op == 0x87) *output = "skp";
            else if (op == 0x8F) *output = "nop";
            else *output = std::string((op & 8) ? "sfs  " : "sfc  ") + std::to_string(op & 7);
            break;
        case 0x90: *output = "and  " + r; break;
        case 0xA0: *output = "xor  " + r; break;
        case 0xB0: *output = "cs   " + r; break;
        case 0xC0: *output = std::string((op & 8) ? "sfl  " : "cfl  ") + std::to_string(op & 7); break;
        case 0xD0: *output = "lda  " + r; break;
        case 0xE0: *output = "sta  " + r; break;
        default:   *output = "ota  " + oct_str(lo, 2); break;
    }
    return two ? 2 : 1;
}

//--------------------------- Процессор -------------------------------------//

// LLA (кроме 07), SFC/SFS 0-4, CFL/SFL (кроме 5 - это перенос), OTA (кроме
// 16 и 17 - страница регистров)
bool IE15CPU::port_op(unsigned op)
{
    switch (op & 0xF0) {
        case 0x60: return op != 0x67;
        case 0x80: return (op & 7) < 5;
        case 0xC0: return (op & 7) != 5;
        case 0xF0: return op < 0xFE;
        default:   return false;
    }
}

IE15CPU::IE15CPU(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    memset(m_regs, 0, sizeof(m_regs));
}

void IE15CPU::core_reset()
{
    m_pc = 0;
    m_a = 0;
    m_cf = m_zf = m_rf = false;
    memset(m_regs, 0, sizeof(m_regs));
}

// Только включение питания: тёплый сброс ЭВМ до терминала не доходит
void IE15CPU::reset(bool cold)
{
    if (cold) CPU::reset(cold);
}

uint8_t IE15CPU::fetch()
{
    const uint8_t v = (uint8_t)mm->read(m_pc) & 0xFF;
    m_pc = (m_pc + 1) & 0xFFF;
    return v;
}

// Пропуск следующей команды: двухбайтовая (JMP, LDC, LCA) пропускается целиком
void IE15CPU::skip()
{
    const uint8_t op = fetch() & 0xF0;
    if (op == 0x10 || op == 0x20 || op == 0x30) m_pc = (m_pc + 1) & 0xFFF;
}

void IE15CPU::execute_one(uint8_t op)
{
    unsigned t;
    switch (op & 0xF0) {
        case 0x00:      // ADD
            t = m_a + reg_lo(op & 15);
            set_a((uint8_t)t);
            m_cf = (t & 0x100) != 0;
            break;
        case 0x10:      // JMP: страница из кода, адрес плюс 1
            m_pc = ((((op & 15) << 8) | arg()) + 1) & 0xFFF;
            break;
        case 0x20:      // LDC: в старшие разряды - страница
            set_reg(op & 15, arg() | (m_pc & 0xF00));
            m_pc = (m_pc + 1) & 0xFFF;
            break;
        case 0x30:
            if (op == 0x30) {           // LCA
                set_a(arg());
                m_pc = (m_pc + 1) & 0xFFF;
            } else if (op == 0x33) {    // RAL
                set_a((uint8_t)((m_a << 1) | (m_a >> 7)));
            } else if (op == 0x35) {    // RAR
                set_a((uint8_t)((m_a >> 1) | (m_a << 7)));
            } else note_unsupported("opcode " + oct_str(op, 3));
            break;
        case 0x40: {    // DSR: пропуск, если стало 377
            const uint8_t v = (uint8_t)(reg_lo(op & 15) - 1);
            set_reg(op & 15, v);
            m_zf = (v == 0xFF);
            if (m_zf) skip();
            break;
        }
        case 0x50:
            switch (op) {
                case 0x50: case 0x51: case 0x58:    // ISN, INC, ISE
                    t = m_a + 1;
                    set_a((uint8_t)t);
                    m_cf = (t & 0x100) != 0;
                    if ((op == 0x50 && m_zf) || (op == 0x58 && !m_zf)) skip();
                    break;
                case 0x52: case 0x5A: case 0x5B:    // DSN, DSE, DEC
                    t = m_a - 1;
                    set_a((uint8_t)t);
                    m_cf = (t & 0x100) != 0;
                    if ((op == 0x52 && m_zf) || (op == 0x5A && !m_zf)) skip();
                    break;
                case 0x5D: set_a(m_a ^ 0xFF); break;   // COM
                case 0x5F: set_a(0); break;            // CLR
                default: note_unsupported("opcode " + oct_str(op, 3)); break;
            }
            break;
        case 0x60:      // LLA: порт 07 не подключён - 377
            set_a((op == 0x67) ? 0xFF : (uint8_t)mm->read_port(op & 15));
            break;
        case 0x70:      // JMI
            m_pc = reg(op & 15) & 0xFFF;
            break;
        case 0x80: {    // SFC/SKP: пропуск при сброшенном, SFS/NOP - при установленном
            bool f;
            switch (op & 7) {
                case 5:  f = (m_a & 0x80) != 0; break;
                case 6:  f = m_cf; break;
                case 7:  f = false; break;
                default: f = (mm->read_port(IE15_FLAG_PORT | (op & 7)) & 0xFF) != 0; break;
            }
            if (((op & 8) == 0) != f) skip();
            break;
        }
        case 0x90: set_a(m_a & reg_lo(op & 15)); break;     // AND
        case 0xA0: set_a(m_a ^ reg_lo(op & 15)); break;     // XOR
        case 0xB0:      // CS: пропуск при равенстве
            if (m_a == reg_lo(op & 15)) { m_zf = true; skip(); }
            break;
        case 0xC0:      // CFL/SFL; флаг 5 - это перенос
            if ((op & 7) == 5) m_cf = false;
            else mm->write_port(IE15_FLAG_PORT | (op & 7), (op >> 3) & 1);
            break;
        case 0xD0: set_a(reg_lo(op & 15)); break;           // LDA
        case 0xE0: set_reg(op & 15, m_a | (m_pc & 0xF00)); break;   // STA
        default:        // OTA; 16 и 17 - страница регистров
            if (op == 0xFE) m_rf = true;
            else if (op == 0xFF) m_rf = false;
            else mm->write_port(op & 15, m_a);
            break;
    }
}

unsigned int IE15CPU::execute()
{
    if (reset_mode) {
        core_reset();
        reset_mode = false;
    }
    if (m_debug == DEBUG_STOPPED) return 0;

    if (m_debug == DEBUG_OFF) {
        // Первая команда пакета может быть портовой: всё до неё уже отсчитано
        // Код, прочитанный для проверки, и исполняется: чтение через маппер
        // одно на команду
        unsigned n = 0;
        uint8_t op = (uint8_t)mm->read(m_pc);
        do {
            m_pc = (m_pc + 1) & 0xFFF;
            execute_one(op);
            n++;
            op = (uint8_t)mm->read(m_pc);
        } while (n < BATCH && !port_op(op));
        return n;
    }

    execute_one(fetch());

    switch (m_debug) {
        case DEBUG_STEP:
            m_debug = DEBUG_STOPPED;
            break;
        case DEBUG_BRAKES:
            if (check_breakpoint(m_pc)) m_debug = DEBUG_STOPPED;
            break;
        default:
            break;
    }
    return 1;
}

unsigned int IE15CPU::read_mem(unsigned int address)
{
    return mm->read(address);
}

// Программная память - только ПЗУ, процессор в неё не пишет
void IE15CPU::write_mem(unsigned int address, unsigned int data)
{
    mm->write(address, data);
}

unsigned int IE15CPU::get_command()
{
    return peek_mem(m_pc);
}

std::vector<std::pair<std::string, std::string>> IE15CPU::get_registers()
{
    std::vector<std::pair<std::string, std::string>> r;
    r.push_back({"PC", oct_str(m_pc, 4)});
    r.push_back({"A", oct_str(m_a, 3)});
    r.push_back({"-1", ""});
    for (unsigned i = 0; i < 32; i++) {
        if (i == 16) r.push_back({"-2", ""});
        r.push_back({"R" + std::to_string(i), oct_str(m_regs[i], 4)});
    }
    return r;
}

std::vector<std::pair<std::string, std::string>> IE15CPU::get_flags()
{
    return {
        {"C", m_cf ? "1" : "0"},
        {"Z", m_zf ? "1" : "0"},
        {"R", m_rf ? "1" : "0"}
    };
}

void IE15CPU::set_context_value(const std::string &name, unsigned int value)
{
    if (name == "PC") m_pc = value & 0xFFF;
    else if (name == "A") m_a = (uint8_t)value;
    else if (name.size() > 1 && name[0] == 'R') {
        const unsigned i = (unsigned)atoi(name.c_str() + 1);
        if (i < 32) m_regs[i] = value & 0xFFF;
    }
}

DisAsm * IE15CPU::create_disasm(MAYBE_UNUSED const std::string &data_path, emulator::Result &res)
{
    res = emulator::Result::ok();
    return new DisAsmIE15();
}

void IE15CPU::save_state(StateWriter &w)
{
    CPU::save_state(w);
    w.u("PC", m_pc);
    w.u("A", m_a, 8);
    w.b("C", m_cf);
    w.b("Z", m_zf);
    w.b("R", m_rf);
    w.array("regs", m_regs, 32);
}

emulator::Result IE15CPU::load_state(const StateReader &r)
{
    emulator::Result res = CPU::load_state(r);
    if (!res) return res;
    r.u("PC", m_pc);
    r.u("A", m_a);
    r.b("C", m_cf);
    r.b("Z", m_zf);
    r.b("R", m_rf);
    r.array("regs", m_regs, 32);
    m_pc &= 0xFFF;
    return emulator::Result::ok();
}

ComputerDevice * create_ie15_cpu(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new IE15CPU(im, cd);
}
