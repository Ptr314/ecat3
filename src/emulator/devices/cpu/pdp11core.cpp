// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: PDP-11 compatible CPU core (К1801ВМ1, К1801ВМ2)

#include "pdp11core.h"

// The 1801 series spends nearly all of its time on the bus, and its published
// instruction times are exactly the bus cycles an instruction runs plus a
// period or two of internal work:
//     DATI  - a read                   7T + tn
//     DATO  - a write, MOV only       10T + tn
//     DATIO - a read-modify-write     13T + 2tn
// tn is the delay before the memory answers, two periods on the BK. That makes
// a register to register operation ten periods - the 300 000 operations per
// second the BK0010 is specified for at 3 MHz - and every trap 52 periods,
// which is the documented 42T + 5tn of EMT.
namespace {
    const unsigned int T_REPLY = 2;                     // tn
    const unsigned int C_DATI  = 7 + T_REPLY;
    const unsigned int C_DATO  = 10 + T_REPLY;
    const unsigned int C_DATIO = 13 + 2 * T_REPLY;

    // Internal work on top of the bus cycles
    const unsigned int C_ALU      = 1;      // an operation on registers alone
    const unsigned int C_SRC_MEM  = 3;      // source operand taken from memory
    const unsigned int C_DST_MEM  = 5;      // destination operand in memory
    // A microcoded PDP-11 walks the same microcode whether a conditional
    // branch is taken or not, and only the documented time of the always
    // taken BR is known, so both cost the same here
    const unsigned int C_BRANCH   = 5;      // 12T + tn
    const unsigned int C_NOBRANCH = C_BRANCH;
    const unsigned int C_SOB      = 9;      // 16T + tn
    const unsigned int C_SOB_END  = C_SOB;
    const unsigned int C_JUMP     = 5;
    const unsigned int C_TRAP     = 2 * C_DATO + 2 * C_DATI + C_ALU;
    const unsigned int C_HALT     = 54 + 7 * T_REPLY - C_DATI;
    const unsigned int C_IDLE     = 8;      // WAIT, idling until an interrupt
    const unsigned int C_RESET    = 30;     // INIT held on the bus
    const unsigned int C_EIS      = 40;     // MUL/DIV/ASH/ASHC of the 1801VM2

    // Address computation: the bus cycles a mode spends before the operand
    // itself is touched, plus the period an auto-decrement costs
    const unsigned int MODE_CYCLES[8] = {
        0, 0, 0, C_DATI, 1, C_DATI + 1, C_DATI, 2 * C_DATI
    };
}

pdp11core::pdp11core(int family_type)
    : has_eis(family_type != PDP11_FAMILY_1801VM1)
    , is_virq(false)
    , virq_vector(0)
    , is_irq2(false)
    , is_irq3(false)
    , is_halt_req(false)
    , m_abort(false)
    , m_no_trace(false)
    , start_address(0100000)
    , start_from_vector(false)
    , halt_vector(PDP11::V_BUS_ERROR)
{
    context.type = family_type;
    for (int i = 0; i < 8; i++) context.R[i] = 0;
    context.PSW = PDP11::F_MASK;
    context.halted = false;
    context.halt_mode = false;
    context.stop = false;
}

void pdp11core::reset()
{
    for (int i = 0; i < 8; i++) context.R[i] = 0;
    context.halted = false;
    context.halt_mode = false;
    context.stop = false;

    is_virq = false;
    is_irq2 = false;
    is_irq3 = false;
    is_halt_req = false;
    m_abort = false;
    m_no_trace = false;

    if (start_from_vector) {
        context.R[PDP11::REG_PC] = read_word(start_address);
        context.PSW = read_word(start_address + 2);
    } else {
        // The БК begins executing straight from the start address with the
        // interrupts masked
        context.R[PDP11::REG_PC] = start_address;
        context.PSW = 0340;
    }
}

pdp11context * pdp11core::get_context()
{
    return &context;
}

uint16_t pdp11core::get_pc()
{
    return context.R[PDP11::REG_PC];
}

uint16_t pdp11core::get_command()
{
    return read_word(context.R[PDP11::REG_PC] & 0xFFFE);
}

// IRQ2/IRQ3 are latched on activation and cleared once served: those inputs
// carry short pulses from a generator in the configurations, and a purely
// level sensitive input would drop them between two instructions.
//
// VIRQ is different - it is driven by a level. On a БК it is the keyboard
// ready flag, which the program clears by reading 0177662 itself. Latching it
// meant that a program polling the keyboard with interrupts disabled still
// entered vector 060 afterwards, with the key already taken.
void pdp11core::set_virq(bool state, uint16_t vector)
{
    is_virq = state;
    if (state) virq_vector = vector;
}

void pdp11core::set_irq2(bool state) { if (state) is_irq2 = true; }
void pdp11core::set_irq3(bool state) { if (state) is_irq3 = true; }
void pdp11core::set_halt(bool state) { if (state) is_halt_req = true; }

//----------------------- Bus access -------------------------------//

// Unlike the big PDP-11s, the 1801 series does not trap on an odd word
// address: the low address bit is simply ignored and the whole word at the
// even address is transferred. The ROMs rely on it, e.g. the БК0011М БЕЙСИК
// tests a byte variable with a word TST of its odd address, so a trap here
// makes every FOR loop end with "СТОП".
uint16_t pdp11core::read_word_checked(uint16_t address)
{
    return read_word(address & 0xFFFE);
}

void pdp11core::write_word_checked(uint16_t address, uint16_t value)
{
    write_word(address & 0xFFFE, value);
}

uint16_t pdp11core::fetch()
{
    uint16_t value = read_word_checked(context.R[PDP11::REG_PC]);
    context.R[PDP11::REG_PC] += 2;
    return value;
}

//----------------------- Flags -------------------------------//

void pdp11core::set_flag(uint16_t flag, bool value)
{
    if (value) context.PSW |= flag; else context.PSW &= ~flag;
}

bool pdp11core::get_flag(uint16_t flag) const
{
    return (context.PSW & flag) != 0;
}

void pdp11core::set_nz(uint16_t value, bool is_byte)
{
    if (is_byte) {
        set_flag(PDP11::F_N, (value & 0x80) != 0);
        set_flag(PDP11::F_Z, (value & 0xFF) == 0);
    } else {
        set_flag(PDP11::F_N, (value & 0x8000) != 0);
        set_flag(PDP11::F_Z, value == 0);
    }
}

//----------------------- Operands -------------------------------//

pdp11operand pdp11core::decode_operand(unsigned int spec, bool is_byte, unsigned int & cycles)
{
    unsigned int mode = (spec >> 3) & 7;
    unsigned int reg  = spec & 7;

    pdp11operand op;
    op.is_reg = false;
    op.reg = reg;
    op.addr = 0;

    cycles += MODE_CYCLES[mode];

    // SP and PC always move by a full word, even in byte instructions
    uint16_t step = (is_byte && reg < PDP11::REG_SP)? 1 : 2;

    switch (mode) {
    case 0:                                     // Rn
        op.is_reg = true;
        break;
    case 1:                                     // (Rn)
        op.addr = context.R[reg];
        break;
    case 2:                                     // (Rn)+ and #immediate
        op.addr = context.R[reg];
        context.R[reg] += (reg == PDP11::REG_PC)? 2 : step;
        break;
    case 3:                                     // @(Rn)+ and @#absolute
        op.addr = read_word_checked(context.R[reg]);
        context.R[reg] += 2;
        break;
    case 4:                                     // -(Rn)
        context.R[reg] -= (reg == PDP11::REG_PC)? 2 : step;
        op.addr = context.R[reg];
        break;
    case 5:                                     // @-(Rn)
        context.R[reg] -= 2;
        op.addr = read_word_checked(context.R[reg]);
        break;
    case 6: {                                   // X(Rn), PC-relative when Rn is PC
        uint16_t x = fetch();
        op.addr = (uint16_t)(context.R[reg] + x);
        break;
    }
    default: {                                  // @X(Rn)
        uint16_t x = fetch();
        op.addr = read_word_checked((uint16_t)(context.R[reg] + x));
        break;
    }
    }
    return op;
}

uint16_t pdp11core::read_operand(const pdp11operand & op, bool is_byte)
{
    if (op.is_reg)
        return is_byte? (uint16_t)(context.R[op.reg] & 0xFF) : context.R[op.reg];

    if (is_byte) return read_byte(op.addr);
    return read_word_checked(op.addr);
}

void pdp11core::write_operand(const pdp11operand & op, bool is_byte, uint16_t value)
{
    if (op.is_reg) {
        if (is_byte)
            context.R[op.reg] = (uint16_t)((context.R[op.reg] & 0xFF00) | (value & 0x00FF));
        else
            context.R[op.reg] = value;
    } else {
        if (is_byte) write_byte(op.addr, (uint8_t)value);
        else write_word_checked(op.addr, value);
    }
}

// A memory operand costs exactly one bus cycle, and which one depends on what
// the instruction does with it. A register operand costs nothing.
unsigned int pdp11core::access_cycles(const pdp11operand & op, operand_access access)
{
    if (op.is_reg) return 0;
    switch (access) {
    case OP_READ:   return C_DATI;
    case OP_WRITE:  return C_DATO;
    case OP_MODIFY: return C_DATIO;
    default:        return 0;
    }
}

// Single operand instructions add nothing to the bus cycle of their operand -
// only a register operand, which needs no bus at all, takes a period in the ALU
unsigned int pdp11core::single_op_cycles(const pdp11operand & op, operand_access access)
{
    return op.is_reg? C_ALU : access_cycles(op, access);
}

//----------------------- Stack, traps, interrupts -------------------------------//

void pdp11core::push(uint16_t value)
{
    context.R[PDP11::REG_SP] -= 2;
    write_word_checked(context.R[PDP11::REG_SP], value);
}

uint16_t pdp11core::pop()
{
    uint16_t value = read_word_checked(context.R[PDP11::REG_SP]);
    context.R[PDP11::REG_SP] += 2;
    return value;
}

void pdp11core::do_trap(uint16_t vector)
{
    uint16_t old_psw = context.PSW;
    uint16_t old_pc  = context.R[PDP11::REG_PC];

    m_trap_vector = vector;
    m_trap_pc = old_pc;
    m_trap_count++;

    m_abort = false;
    push(old_psw);
    push(old_pc);

    if (m_abort) {
        // A fault while saving the state has nowhere left to be reported
        context.stop = true;
        return;
    }

    context.R[PDP11::REG_PC] = read_word_checked(vector);
    context.PSW = read_word_checked(vector + 2);
    context.halted = false;
    m_abort = false;
}

void pdp11core::enter_halt_mode()
{
    // The console request and the HALT instruction both hand control to the
    // monitor through a vector, which on the БК is the same 004 the СТОП key
    // uses.
    context.halt_mode = true;
    do_trap(halt_vector);
}

bool pdp11core::check_interrupts(unsigned int & cycles)
{
    if (is_halt_req) {
        is_halt_req = false;
        enter_halt_mode();
        cycles += C_TRAP;
        return true;
    }

    if ((context.PSW & PDP11::F_MASK) == 0) {
        if (is_irq2)      { is_irq2 = false; do_trap(PDP11::V_IRQ2); cycles += C_TRAP; return true; }
        else if (is_irq3) { is_irq3 = false; do_trap(PDP11::V_IRQ3); cycles += C_TRAP; return true; }
        else if (is_virq) { is_virq = false; do_trap(virq_vector);   cycles += C_TRAP; return true; }
    }
    return false;
}

void pdp11core::do_branch(uint16_t command, bool condition, unsigned int & cycles)
{
    if (condition) {
        int8_t offset = (int8_t)(command & 0xFF);
        context.R[PDP11::REG_PC] = (uint16_t)(context.R[PDP11::REG_PC] + offset * 2);
        cycles += C_BRANCH;
    } else
        cycles += C_NOBRANCH;
}

//----------------------- Double operand instructions -------------------------------//

bool pdp11core::execute_double(uint16_t command, unsigned int & cycles)
{
    unsigned int op_id = (command >> 12) & 017;

    // 1..6 are the word forms, 011..016 the byte ones, except that 16SSDD is
    // SUB rather than a byte variant of ADD.
    if (!((op_id >= 1 && op_id <= 6) || (op_id >= 011 && op_id <= 016))) return false;

    bool is_byte = (op_id >= 011) && (op_id != 016);

    pdp11operand src_op = decode_operand((command >> 6) & 077, is_byte, cycles);
    uint16_t src = read_operand(src_op, is_byte);
    pdp11operand dst_op = decode_operand(command & 077, is_byte, cycles);

    unsigned int kind = op_id & 07;             // 1 MOV, 2 CMP, 3 BIT, 4 BIC, 5 BIS, 6 ADD/SUB
    uint32_t mask = is_byte? 0xFFu : 0xFFFFu;
    uint16_t sign = is_byte? 0x0080 : 0x8000;

    // MOV writes the destination without reading it and CMP and BIT never
    // write, the rest read and write it in a single read-modify-write cycle
    operand_access dst_access = (kind == 1)? OP_WRITE
                              : ((kind == 2 || kind == 3)? OP_READ : OP_MODIFY);
    cycles += C_ALU + access_cycles(src_op, OP_READ) + access_cycles(dst_op, dst_access);
    if (!src_op.is_reg) cycles += C_SRC_MEM;
    if (!dst_op.is_reg) cycles += C_DST_MEM;

    // MOV and MOVB never read the destination, which matters for registers
    // with a side effect on read.
    uint16_t dst = (kind == 1)? 0 : read_operand(dst_op, is_byte);

    if (m_abort) return true;

    switch (kind) {
    case 1:                                     // MOV / MOVB
        set_nz(src, is_byte);
        set_flag(PDP11::F_V, false);
        if (is_byte && dst_op.is_reg)
            // MOVB into a register sign-extends the byte
            context.R[dst_op.reg] = (uint16_t)(int16_t)(int8_t)(src & 0xFF);
        else
            write_operand(dst_op, is_byte, src);
        break;

    case 2: {                                   // CMP / CMPB, computes src - dst
        uint32_t r = (uint32_t)(src & mask) + (uint32_t)(~dst & mask) + 1;
        uint16_t res = (uint16_t)(r & mask);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, ((src ^ dst) & (src ^ res) & sign) != 0);
        set_flag(PDP11::F_C, (r & (mask + 1)) == 0);
        break;
    }

    case 3: {                                   // BIT / BITB
        uint16_t res = (uint16_t)(src & dst & mask);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, false);
        break;
    }

    case 4: {                                   // BIC / BICB
        uint16_t res = (uint16_t)(dst & ~src & mask);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, false);
        write_operand(dst_op, is_byte, res);
        break;
    }

    case 5: {                                   // BIS / BISB
        uint16_t res = (uint16_t)((dst | src) & mask);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, false);
        write_operand(dst_op, is_byte, res);
        break;
    }

    default:
        if (op_id == 6) {                       // ADD
            uint32_t r = (uint32_t)src + (uint32_t)dst;
            uint16_t res = (uint16_t)r;
            set_nz(res, false);
            set_flag(PDP11::F_V, (~(src ^ dst) & (src ^ res) & 0x8000) != 0);
            set_flag(PDP11::F_C, r > 0xFFFF);
            write_operand(dst_op, false, res);
        } else {                                // SUB, dst - src
            uint32_t r = (uint32_t)dst + (uint32_t)(uint16_t)(~src) + 1;
            uint16_t res = (uint16_t)r;
            set_nz(res, false);
            set_flag(PDP11::F_V, ((dst ^ src) & (dst ^ res) & 0x8000) != 0);
            set_flag(PDP11::F_C, r <= 0xFFFF);
            write_operand(dst_op, false, res);
        }
        break;
    }

    return true;
}

//----------------------- Single operand instructions -------------------------------//

bool pdp11core::execute_single(uint16_t command, unsigned int & cycles)
{
    unsigned int sop = (command >> 6) & 0777;   // the byte flag is masked out here
    bool is_byte = (command & 0100000) != 0;
    unsigned int spec = command & 077;

    uint32_t mask = is_byte? 0xFFu : 0xFFFFu;
    uint16_t sign = is_byte? 0x0080 : 0x8000;

    // JMP and SWAB exist in the word form only
    if (!is_byte && sop == 001) {                       // JMP
        pdp11operand op = decode_operand(spec, false, cycles);
        cycles += C_JUMP;
        // A jump to a register is an illegal instruction, not a reserved
        // opcode: it traps through vector 4 and not through vector 10
        if (op.is_reg) { cycles += C_TRAP; do_trap(PDP11::V_ILLEGAL); return true; }
        context.R[PDP11::REG_PC] = op.addr;
        return true;
    }

    if (!is_byte && sop == 003) {                       // SWAB
        pdp11operand op = decode_operand(spec, false, cycles);
        cycles += single_op_cycles(op, OP_MODIFY);
        uint16_t dst = read_operand(op, false);
        if (m_abort) return true;
        uint16_t res = (uint16_t)((dst >> 8) | (dst << 8));
        write_operand(op, false, res);
        set_flag(PDP11::F_N, (res & 0x80) != 0);
        set_flag(PDP11::F_Z, (res & 0xFF) == 0);
        set_flag(PDP11::F_V, false);
        set_flag(PDP11::F_C, false);
        return true;
    }

    if (sop < 050 || sop > 067) return false;

    // 064..067 mean different things in the two halves of the opcode space
    if (sop >= 064) {
        if (!is_byte) {
            switch (sop) {
            case 064: {                                 // MARK
                cycles += C_DATI + C_ALU;
                unsigned int nn = spec;
                context.R[PDP11::REG_SP] = (uint16_t)(context.R[PDP11::REG_PC] + nn * 2);
                context.R[PDP11::REG_PC] = context.R[5];
                context.R[5] = pop();
                return true;
            }
            case 065: {                                 // MFPI, one address space here
                pdp11operand op = decode_operand(spec, false, cycles);
                cycles += access_cycles(op, OP_READ) + C_DATO + C_ALU;
                uint16_t v = op.is_reg? context.R[op.reg] : read_word_checked(op.addr);
                if (m_abort) return true;
                push(v);
                set_nz(v, false);
                set_flag(PDP11::F_V, false);
                return true;
            }
            case 066: {                                 // MTPI
                uint16_t v = pop();
                pdp11operand op = decode_operand(spec, false, cycles);
                cycles += C_DATI + access_cycles(op, OP_WRITE) + C_ALU;
                if (m_abort) return true;
                write_operand(op, false, v);
                set_nz(v, false);
                set_flag(PDP11::F_V, false);
                return true;
            }
            default: {                                  // SXT
                pdp11operand op = decode_operand(spec, false, cycles);
                cycles += single_op_cycles(op, OP_MODIFY);
                uint16_t res = get_flag(PDP11::F_N)? 0xFFFF : 0x0000;
                write_operand(op, false, res);
                set_flag(PDP11::F_Z, !get_flag(PDP11::F_N));
                set_flag(PDP11::F_V, false);
                return true;
            }
            }
        } else {
            switch (sop) {
            case 064: {                                 // MTPS
                pdp11operand op = decode_operand(spec, true, cycles);
                cycles += single_op_cycles(op, OP_READ);
                uint16_t v = read_operand(op, true);
                if (m_abort) return true;
                // The trace bit cannot be set this way
                context.PSW = (uint16_t)((context.PSW & PDP11::F_T) | (v & 0xFF & ~PDP11::F_T));
                return true;
            }
            case 067: {                                 // MFPS
                pdp11operand op = decode_operand(spec, true, cycles);
                cycles += single_op_cycles(op, OP_WRITE);
                uint16_t v = (uint16_t)(context.PSW & 0xFF);
                if (op.is_reg)
                    context.R[op.reg] = (uint16_t)(int16_t)(int8_t)(v & 0xFF);
                else
                    write_operand(op, true, v);
                set_nz(v, true);
                set_flag(PDP11::F_V, false);
                return true;
            }
            default:
                // MFPD/MTPD have no meaning with a single address space
                cycles += C_TRAP;
                do_trap(PDP11::V_RESERVED);
                return true;
            }
        }
    }

    pdp11operand op = decode_operand(spec, is_byte, cycles);
    // TST only reads its operand; everything else in the group reads and
    // writes it in one bus cycle, CLR included - it is a DATIO on the 1801
    cycles += single_op_cycles(op, (sop == 057)? OP_READ : OP_MODIFY);

    // CLR does not read its destination
    uint16_t dst = (sop == 050)? 0 : read_operand(op, is_byte);
    if (m_abort) return true;

    switch (sop) {
    case 050:                                           // CLR / CLRB
        write_operand(op, is_byte, 0);
        set_flag(PDP11::F_N, false);
        set_flag(PDP11::F_Z, true);
        set_flag(PDP11::F_V, false);
        set_flag(PDP11::F_C, false);
        break;

    case 051: {                                         // COM / COMB
        uint16_t res = (uint16_t)(~dst & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, false);
        set_flag(PDP11::F_C, true);
        break;
    }

    case 052: {                                         // INC / INCB
        uint16_t res = (uint16_t)((dst + 1) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, (dst & mask) == (uint32_t)(sign - 1));
        break;
    }

    case 053: {                                         // DEC / DECB
        uint16_t res = (uint16_t)((dst - 1) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, (dst & mask) == (uint32_t)sign);
        break;
    }

    case 054: {                                         // NEG / NEGB
        uint16_t res = (uint16_t)((0 - dst) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, (res & mask) == (uint32_t)sign);
        set_flag(PDP11::F_C, (res & mask) != 0);
        break;
    }

    case 055: {                                         // ADC / ADCB
        uint16_t c = get_flag(PDP11::F_C)? 1 : 0;
        uint16_t res = (uint16_t)((dst + c) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, c != 0 && (dst & mask) == (uint32_t)(sign - 1));
        set_flag(PDP11::F_C, c != 0 && (dst & mask) == mask);
        break;
    }

    case 056: {                                         // SBC / SBCB
        uint16_t c = get_flag(PDP11::F_C)? 1 : 0;
        uint16_t res = (uint16_t)((dst - c) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_V, (dst & mask) == (uint32_t)sign);
        set_flag(PDP11::F_C, c != 0 && (dst & mask) == 0);
        break;
    }

    case 057:                                           // TST / TSTB
        set_nz((uint16_t)(dst & mask), is_byte);
        set_flag(PDP11::F_V, false);
        set_flag(PDP11::F_C, false);
        break;

    case 060: {                                         // ROR / RORB
        uint16_t c = get_flag(PDP11::F_C)? 1 : 0;
        uint16_t res = (uint16_t)(((dst & mask) >> 1) | (c * (sign & mask)));
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_C, (dst & 1) != 0);
        set_flag(PDP11::F_V, get_flag(PDP11::F_N) != get_flag(PDP11::F_C));
        break;
    }

    case 061: {                                         // ROL / ROLB
        uint16_t c = get_flag(PDP11::F_C)? 1 : 0;
        uint16_t res = (uint16_t)(((dst << 1) | c) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_C, (dst & sign) != 0);
        set_flag(PDP11::F_V, get_flag(PDP11::F_N) != get_flag(PDP11::F_C));
        break;
    }

    case 062: {                                         // ASR / ASRB
        uint16_t res = (uint16_t)(((dst & mask) >> 1) | (dst & sign));
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_C, (dst & 1) != 0);
        set_flag(PDP11::F_V, get_flag(PDP11::F_N) != get_flag(PDP11::F_C));
        break;
    }

    default: {                                          // 063: ASL / ASLB
        uint16_t res = (uint16_t)((dst << 1) & mask);
        write_operand(op, is_byte, res);
        set_nz(res, is_byte);
        set_flag(PDP11::F_C, (dst & sign) != 0);
        set_flag(PDP11::F_V, get_flag(PDP11::F_N) != get_flag(PDP11::F_C));
        break;
    }
    }

    return true;
}

//----------------------- Everything else -------------------------------//

bool pdp11core::execute_misc(uint16_t command, unsigned int & cycles)
{
    // Branches
    if ((command & 0177400) >= 0000400 && (command & 0177400) <= 0003400) {
        bool n = get_flag(PDP11::F_N), z = get_flag(PDP11::F_Z);
        bool v = get_flag(PDP11::F_V);
        bool cond = false;
        switch (command & 0177400) {
        case 0000400: cond = true; break;                       // BR
        case 0001000: cond = !z; break;                         // BNE
        case 0001400: cond = z; break;                          // BEQ
        case 0002000: cond = (n == v); break;                   // BGE
        case 0002400: cond = (n != v); break;                   // BLT
        case 0003000: cond = !z && (n == v); break;             // BGT
        default:      cond = z || (n != v); break;              // BLE
        }
        do_branch(command, cond, cycles);
        return true;
    }

    if ((command & 0177400) >= 0100000 && (command & 0177400) <= 0103400) {
        bool n = get_flag(PDP11::F_N), z = get_flag(PDP11::F_Z);
        bool v = get_flag(PDP11::F_V), c = get_flag(PDP11::F_C);
        bool cond = false;
        switch (command & 0177400) {
        case 0100000: cond = !n; break;                         // BPL
        case 0100400: cond = n; break;                          // BMI
        case 0101000: cond = !c && !z; break;                   // BHI
        case 0101400: cond = c || z; break;                     // BLOS
        case 0102000: cond = !v; break;                         // BVC
        case 0102400: cond = v; break;                          // BVS
        case 0103000: cond = !c; break;                         // BCC / BHIS
        default:      cond = c; break;                          // BCS / BLO
        }
        do_branch(command, cond, cycles);
        return true;
    }

    // EMT and TRAP
    if ((command & 0177400) == 0104000) {
        cycles += C_TRAP;
        do_trap(PDP11::V_EMT);
        return true;
    }
    if ((command & 0177400) == 0104400) {
        cycles += C_TRAP;
        do_trap(PDP11::V_TRAP);
        return true;
    }

    // JSR
    if ((command & 0177000) == 0004000) {
        unsigned int r = (command >> 6) & 7;
        pdp11operand op = decode_operand(command & 077, false, cycles);
        cycles += C_DATO + C_ALU;
        if (op.is_reg) { cycles += C_TRAP; do_trap(PDP11::V_ILLEGAL); return true; }
        uint16_t target = op.addr;
        push(context.R[r]);
        if (m_abort) return true;
        context.R[r] = context.R[PDP11::REG_PC];
        context.R[PDP11::REG_PC] = target;
        return true;
    }

    // XOR
    if ((command & 0177000) == 0074000) {
        unsigned int r = (command >> 6) & 7;
        pdp11operand op = decode_operand(command & 077, false, cycles);
        cycles += C_ALU + access_cycles(op, OP_MODIFY);
        if (!op.is_reg) cycles += C_DST_MEM;
        uint16_t dst = read_operand(op, false);
        if (m_abort) return true;
        uint16_t res = (uint16_t)(dst ^ context.R[r]);
        write_operand(op, false, res);
        set_nz(res, false);
        set_flag(PDP11::F_V, false);
        return true;
    }

    // SOB
    if ((command & 0177000) == 0077000) {
        unsigned int r = (command >> 6) & 7;
        context.R[r]--;
        if (context.R[r] != 0) {
            context.R[PDP11::REG_PC] -= (uint16_t)((command & 077) * 2);
            cycles += C_SOB;
        } else
            cycles += C_SOB_END;
        return true;
    }

    // Extended arithmetic, absent on the 1801ВМ1
    if ((command & 0174000) == 0070000) {
        unsigned int group = (command >> 9) & 3;
        if (!has_eis) { do_trap(PDP11::V_RESERVED); cycles += C_TRAP; return true; }

        unsigned int r = (command >> 6) & 7;
        pdp11operand op = decode_operand(command & 077, false, cycles);
        cycles += C_EIS + access_cycles(op, OP_READ);
        uint16_t src = read_operand(op, false);
        if (m_abort) return true;

        switch (group) {
        case 0: {                                       // MUL
            int32_t product = (int32_t)(int16_t)context.R[r] * (int32_t)(int16_t)src;
            context.R[r] = (uint16_t)(product >> 16);
            if ((r & 1) == 0) context.R[r | 1] = (uint16_t)(product & 0xFFFF);
            else context.R[r] = (uint16_t)(product & 0xFFFF);
            set_flag(PDP11::F_N, product < 0);
            set_flag(PDP11::F_Z, product == 0);
            set_flag(PDP11::F_V, false);
            set_flag(PDP11::F_C, product < -32768 || product > 32767);
            break;
        }
        case 1: {                                       // DIV
            if ((r & 1) != 0 || src == 0) {
                set_flag(PDP11::F_V, true);
                set_flag(PDP11::F_C, src == 0);
                break;
            }
            int32_t dividend = (int32_t)(((uint32_t)context.R[r] << 16) | context.R[r | 1]);
            int32_t divisor = (int32_t)(int16_t)src;
            int32_t quotient = dividend / divisor;
            if (quotient > 32767 || quotient < -32768) {
                set_flag(PDP11::F_V, true);
                set_flag(PDP11::F_C, false);
                break;
            }
            context.R[r] = (uint16_t)quotient;
            context.R[r | 1] = (uint16_t)(dividend % divisor);
            set_flag(PDP11::F_N, quotient < 0);
            set_flag(PDP11::F_Z, quotient == 0);
            set_flag(PDP11::F_V, false);
            set_flag(PDP11::F_C, false);
            break;
        }
        case 2: {                                       // ASH
            int shift = (int)(src & 077);
            if (shift > 31) shift -= 64;
            int32_t value = (int32_t)(int16_t)context.R[r];
            int32_t res;
            if (shift >= 0) { res = value << shift; set_flag(PDP11::F_C, shift > 0 && ((value << (shift - 1)) & 0x8000) != 0); }
            else            { res = value >> (-shift); set_flag(PDP11::F_C, ((value >> (-shift - 1)) & 1) != 0); }
            context.R[r] = (uint16_t)res;
            set_nz((uint16_t)res, false);
            set_flag(PDP11::F_V, ((value ^ res) & 0x8000) != 0);
            break;
        }
        default: {                                      // ASHC
            int shift = (int)(src & 077);
            if (shift > 31) shift -= 64;
            int32_t value = (int32_t)(((uint32_t)context.R[r] << 16) | context.R[r | 1]);
            int64_t res;
            if (shift >= 0) { res = (int64_t)value << shift; set_flag(PDP11::F_C, shift > 0 && (((int64_t)value << (shift - 1)) & 0x80000000LL) != 0); }
            else            { res = (int64_t)value >> (-shift); set_flag(PDP11::F_C, (((int64_t)value >> (-shift - 1)) & 1) != 0); }
            context.R[r] = (uint16_t)(((uint32_t)res >> 16) & 0xFFFF);
            context.R[r | 1] = (uint16_t)((uint32_t)res & 0xFFFF);
            set_flag(PDP11::F_N, ((uint32_t)res & 0x80000000u) != 0);
            set_flag(PDP11::F_Z, ((uint32_t)res) == 0);
            set_flag(PDP11::F_V, ((value ^ (int32_t)res) & 0x80000000u) != 0);
            break;
        }
        }
        return true;
    }

    // RTS
    if ((command & 0177770) == 0000200) {
        cycles += C_DATI + C_ALU;
        unsigned int r = command & 7;
        context.R[PDP11::REG_PC] = context.R[r];
        context.R[r] = pop();
        return true;
    }

    // Condition code operations, 000240 is NOP
    if ((command & 0177740) == 0000240) {
        cycles += C_ALU;
        uint16_t flags = (uint16_t)(command & 017);
        if ((command & 020) != 0) context.PSW |= flags;
        else context.PSW &= ~flags;
        return true;
    }

    // No operand instructions
    switch (command) {
    case 0000000:                                       // HALT
        cycles += C_HALT;
        enter_halt_mode();
        return true;
    case 0000001:                                       // WAIT
        cycles += C_IDLE;
        context.halted = true;
        return true;
    case 0000002: {                                     // RTI
        cycles += 2 * C_DATI + C_ALU;
        context.R[PDP11::REG_PC] = pop();
        context.PSW = pop();
        return true;
    }
    case 0000003:                                       // BPT
        cycles += C_TRAP;
        do_trap(PDP11::V_BPT);
        return true;
    case 0000004:                                       // IOT
        cycles += C_TRAP;
        do_trap(PDP11::V_IOT);
        return true;
    case 0000005:                                       // RESET
        // Peripheral initialisation is driven from the emulator device
        cycles += C_RESET;
        return true;
    case 0000006:                                       // RTT
        cycles += 2 * C_DATI + C_ALU;
        context.R[PDP11::REG_PC] = pop();
        context.PSW = pop();
        // RTT defers the trace trap until after the next instruction
        m_no_trace = true;
        return true;
    default:
        break;
    }

    // START (000010-000013) and STEP (000014-000017) belong to the console
    // (halt) mode. Executed in the OS mode they hand the processor over to
    // the halt mode exactly like HALT does, which on the БК is a trap
    // through vector 4 rather than the reserved instruction vector 10.
    if (command >= 0000010 && command <= 0000017) {
        cycles += C_HALT;
        enter_halt_mode();
        return true;
    }

    return false;
}

//----------------------- Main loop -------------------------------//

unsigned int pdp11core::execute()
{
    unsigned int cycles = 0;

    if (context.stop) return C_IDLE;

    if (check_interrupts(cycles)) return cycles;

    if (context.halted) return C_IDLE;          // WAIT, idling until an interrupt

    bool trace = get_flag(PDP11::F_T) && !m_no_trace;
    m_no_trace = false;

    m_abort = false;
    cycles += C_DATI;                           // reading the instruction
    uint16_t command = fetch();

    bool handled = false;
    if (!m_abort) {
        handled = execute_double(command, cycles);
        if (!handled) handled = execute_single(command, cycles);
        if (!handled) handled = execute_misc(command, cycles);
    }

    if (m_abort) {
        cycles += C_TRAP;
        do_trap(PDP11::V_BUS_ERROR);
    } else if (!handled) {
        cycles += C_TRAP;
        do_trap(PDP11::V_RESERVED);
    } else if (trace) {
        cycles += C_TRAP;
        do_trap(PDP11::V_BPT);
    }

    return cycles;
}
