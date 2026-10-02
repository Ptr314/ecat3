// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 (К1810ВМ86/ВМ88) CPU core

#include "i8086core.h"

using namespace I8086;

static inline uint32_t linear(uint16_t seg, uint16_t off)
{
    return (((uint32_t)seg << 4) + off) & 0xFFFFF;
}

static inline bool even_parity(uint8_t v)
{
    v ^= v >> 4;
    return ((0x6996 >> (v & 0x0F)) & 1) == 0;
}

static inline unsigned bit_count(uint32_t v)
{
    unsigned n = 0;
    while (v != 0) { n += v & 1; v >>= 1; }
    return n;
}

i8086core::i8086core(int family)
    : m_family(family)
    , m_qsize((family == I8086_FAMILY_8086) ? 6 : 4)
    , m_fetch_room((family == I8086_FAMILY_8086) ? 2 : 1)
    , m_wide(family == I8086_FAMILY_8086)
    , m_start(0)
    , m_first_take(0)
    , m_seg(-1)
    , m_rep(0)
    , m_instr_ip(0)
    , m_last_prefix_ip(0)
    , m_opcode_ip(0)
    , m_mod(0), m_reg(0), m_rm(0)
    , m_ea(0)
    , m_ea_seg(DS)
{
    for (unsigned i = 0; i < 8; i++) ctx.r[i] = 0;
    for (unsigned i = 0; i < 4; i++) ctx.s[i] = 0;
    ctx.ip = 0;
    ctx.flags = 0;
    ctx.halted = false;

    bus.t = 0;
    bus.bus_free = 0;
    bus.pf_ready = 0;
    bus.pf_ip = 0;
    for (unsigned i = 0; i < QUEUE_MAX; i++) { bus.q[i] = 0; bus.q_avail[i] = 0; }
    bus.q_head = 0;
    bus.q_len = 0;
    bus.nmi_pending = false;
    bus.nmi_level = false;
    bus.intr = false;
    bus.inhibit = false;
    bus.trap = false;
    bus.rep_active = false;
    bus.rep_opcode = 0;
    bus.rep_prefix = 0;
    bus.rep_seg = -1;
    bus.rep_ip = 0;
    bus.rep_last_prefix_ip = 0;
}

void i8086core::reset()
{
    //The general registers keep whatever they held; the chip resets only these
    ctx.s[ES] = ctx.s[SS] = ctx.s[DS] = 0;
    ctx.s[CS] = 0xFFFF;
    ctx.flags = 0;
    ctx.halted = false;
    bus.nmi_pending = false;
    bus.inhibit = false;
    bus.trap = false;
    bus.rep_active = false;
    //Time goes on: the devices count it too. A cycle under way is cut
    //short, nothing more is fetched from where the processor was
    bus.bus_free = bus.t;
    bus.q_head = 0;
    bus.q_len = 0;
    ctx.ip = 0;
    bus.pf_ip = 0;
    //The chip spends a few clocks before the first fetch
    bus.pf_ready = bus.t + 7;
}

void i8086core::set_nmi(bool level)
{
    if (level && !bus.nmi_level) bus.nmi_pending = true;
    bus.nmi_level = level;
}

uint32_t i8086core::get_pc() const
{
    return linear(ctx.s[CS], bus.rep_active ? bus.rep_ip : ctx.ip);
}

void i8086core::set_ip(uint16_t ip)
{
    bus.rep_active = false;
    flush(ip);
}

void i8086core::set_cs(uint16_t cs)
{
    bus.rep_active = false;
    far_jump(cs, ctx.ip);
}

void i8086core::preload_queue(const uint8_t * bytes, unsigned count)
{
    flush(ctx.ip);
    if (count > m_qsize) count = m_qsize;
    for (unsigned i = 0; i < count; i++) {
        bus.q[i] = bytes[i];
        bus.q_avail[i] = bus.t;
    }
    bus.q_head = 0;
    bus.q_len = count;
    bus.pf_ip = (uint16_t)(ctx.ip + count);
}

void i8086core::idle(unsigned clocks)
{
    //Nothing runs: the bus belongs to someone else
    prefetch_until(bus.t);
    bus.t += clocks;
    if (bus.bus_free < bus.t) bus.bus_free = bus.t;
}

//----------------------------- Bus interface ---------------------------------

//Runs, in order, every prefetch cycle that begins before t. A cycle begins as
//soon as the bus is free and the queue has room; one that begins at t or later
//is left for later, so a request of the execution unit made at t wins the bus
void i8086core::prefetch_until(uint64_t t)
{
    for (;;) {
        if (m_qsize - bus.q_len < m_fetch_room) return;
        uint64_t start = (bus.bus_free > bus.pf_ready) ? bus.bus_free : bus.pf_ready;
        if (start >= t) return;
        fetch_cycle(start);
    }
}

void i8086core::fetch_cycle(uint64_t start)
{
    const uint32_t a = linear(ctx.s[CS], bus.pf_ip);
    const unsigned off = (start > m_start) ? offset_of(start) : 0;
    uint64_t end;
    unsigned tail = (bus.q_head + bus.q_len) % QUEUE_MAX;
    if (m_wide && (a & 1) == 0 && bus.pf_ip != 0xFFFF) {
        //A word at an even address: two bytes in one cycle
        const uint16_t v = mem_read16(a);
        end = start + 4 + mem_wait(a, false, off);
        bus.q[tail] = (uint8_t)v;
        bus.q_avail[tail] = end;
        tail = (tail + 1) % QUEUE_MAX;
        bus.q[tail] = (uint8_t)(v >> 8);
        bus.q_avail[tail] = end;
        bus.q_len += 2;
        bus.pf_ip = (uint16_t)(bus.pf_ip + 2);
    } else {
        //The 8088 always, the 8086 after a jump to an odd address
        const uint8_t v = mem_read8(a);
        end = start + 4 + mem_wait(a, false, off);
        bus.q[tail] = v;
        bus.q_avail[tail] = end;
        bus.q_len++;
        bus.pf_ip = (uint16_t)(bus.pf_ip + 1);
    }
    bus.bus_free = end;
}

//The execution unit takes the next byte of the instruction stream. With the
//queue empty it waits for the fetch that brings it
uint8_t i8086core::q_take()
{
    prefetch_until(bus.t + 1);
    if (bus.q_len == 0) {
        uint64_t start = (bus.bus_free > bus.pf_ready) ? bus.bus_free : bus.pf_ready;
        if (start < bus.t) start = bus.t;
        fetch_cycle(start);
    }
    const uint8_t v = bus.q[bus.q_head];
    if (bus.q_avail[bus.q_head] > bus.t) bus.t = bus.q_avail[bus.q_head];
    const bool blocked = (m_qsize - bus.q_len) < m_fetch_room;
    bus.q_head = (bus.q_head + 1) % QUEUE_MAX;
    bus.q_len--;
    if (blocked && (m_qsize - bus.q_len) >= m_fetch_room) bus.pf_ready = bus.t;
    ctx.ip++;
    return v;
}

uint16_t i8086core::q_take16()
{
    const uint8_t lo = q_take();
    return (uint16_t)(lo | (q_take() << 8));
}

//A jump: what is in the queue is thrown away, a fetch already on the bus runs
//to its end, and fetching starts again at the new address
void i8086core::flush(uint16_t new_ip)
{
    prefetch_until(bus.t);
    bus.q_head = 0;
    bus.q_len = 0;
    ctx.ip = new_ip;
    bus.pf_ip = new_ip;
    bus.pf_ready = bus.t;
}

void i8086core::far_jump(uint16_t cs, uint16_t ip)
{
    prefetch_until(bus.t);
    ctx.s[CS] = cs;
    flush(ip);
}

uint64_t i8086core::eu_cycle_start()
{
    prefetch_until(bus.t);
    return (bus.bus_free > bus.t) ? bus.bus_free : bus.t;
}

void i8086core::eu_cycle_end(uint64_t start, unsigned waits)
{
    bus.bus_free = start + 4 + waits;
    bus.t = bus.bus_free;
}

uint8_t i8086core::rd8(uint16_t seg, uint16_t off)
{
    const uint32_t a = linear(seg, off);
    const uint64_t s = eu_cycle_start();
    const uint8_t v = mem_read8(a);
    eu_cycle_end(s, mem_wait(a, false, offset_of(s)));
    return v;
}

uint16_t i8086core::rd16(uint16_t seg, uint16_t off)
{
    const uint32_t a = linear(seg, off);
    if (m_wide && (a & 1) == 0 && off != 0xFFFF) {
        const uint64_t s = eu_cycle_start();
        const uint16_t v = mem_read16(a);
        eu_cycle_end(s, mem_wait(a, false, offset_of(s)));
        return v;
    }
    //Two cycles, the second wraps inside the segment
    const uint8_t lo = rd8(seg, off);
    return (uint16_t)(lo | (rd8(seg, (uint16_t)(off + 1)) << 8));
}

void i8086core::wr8(uint16_t seg, uint16_t off, uint8_t v)
{
    const uint32_t a = linear(seg, off);
    const uint64_t s = eu_cycle_start();
    mem_write8(a, v);
    eu_cycle_end(s, mem_wait(a, true, offset_of(s)));
}

void i8086core::wr16(uint16_t seg, uint16_t off, uint16_t v)
{
    const uint32_t a = linear(seg, off);
    if (m_wide && (a & 1) == 0 && off != 0xFFFF) {
        const uint64_t s = eu_cycle_start();
        mem_write16(a, v);
        eu_cycle_end(s, mem_wait(a, true, offset_of(s)));
        return;
    }
    wr8(seg, off, (uint8_t)v);
    wr8(seg, (uint16_t)(off + 1), (uint8_t)(v >> 8));
}

uint8_t i8086core::in8(uint16_t port)
{
    const uint64_t s = eu_cycle_start();
    const uint8_t v = io_read8(port);
    eu_cycle_end(s, io_wait);
    return v;
}

uint16_t i8086core::in16(uint16_t port)
{
    if (m_wide && (port & 1) == 0) {
        const uint64_t s = eu_cycle_start();
        const uint16_t v = io_read16(port);
        eu_cycle_end(s, io_wait);
        return v;
    }
    const uint8_t lo = in8(port);
    return (uint16_t)(lo | (in8((uint16_t)(port + 1)) << 8));
}

void i8086core::out8(uint16_t port, uint8_t v)
{
    const uint64_t s = eu_cycle_start();
    io_write8(port, v);
    eu_cycle_end(s, io_wait);
}

void i8086core::out16(uint16_t port, uint16_t v)
{
    if (m_wide && (port & 1) == 0) {
        const uint64_t s = eu_cycle_start();
        io_write16(port, v);
        eu_cycle_end(s, io_wait);
        return;
    }
    out8(port, (uint8_t)v);
    out8((uint16_t)(port + 1), (uint8_t)(v >> 8));
}

//Two acknowledge cycles; the controller puts the vector on the bus in the second
uint8_t i8086core::inta_cycles()
{
    uint64_t s = eu_cycle_start();
    eu_cycle_end(s, 0);
    s = eu_cycle_start();
    const uint8_t v = int_ack();
    eu_cycle_end(s, 0);
    return v;
}

void i8086core::push(uint16_t v)
{
    ctx.r[SP] = (uint16_t)(ctx.r[SP] - 2);
    wr16(ctx.s[SS], ctx.r[SP], v);
}

uint16_t i8086core::pop()
{
    const uint16_t v = rd16(ctx.s[SS], ctx.r[SP]);
    ctx.r[SP] = (uint16_t)(ctx.r[SP] + 2);
    return v;
}

//------------------------------- Operands ------------------------------------

void i8086core::set_r8(unsigned i, uint8_t v)
{
    if (i < 4) ctx.r[i] = (uint16_t)((ctx.r[i] & 0xFF00) | v);
    else ctx.r[i - 4] = (uint16_t)((ctx.r[i - 4] & 0x00FF) | (v << 8));
}

//Reads the ModR/M byte and the displacement, and spends the clocks the
//execution unit needs to form the address
void i8086core::decode_modrm()
{
    const uint8_t m = q_take();
    m_mod = m >> 6;
    m_reg = (m >> 3) & 7;
    m_rm = m & 7;
    if (m_mod == 3) return;

    int def = DS;
    unsigned clocks;
    uint16_t base;
    if (m_mod == 0 && m_rm == 6) {
        m_ea = q_take16();
        m_ea_seg = seg_or(DS);
        clk(6);
        return;
    }
    switch (m_rm) {
    case 0: base = (uint16_t)(ctx.r[BX] + ctx.r[SI]); clocks = 7; break;
    case 1: base = (uint16_t)(ctx.r[BX] + ctx.r[DI]); clocks = 8; break;
    case 2: base = (uint16_t)(ctx.r[BP] + ctx.r[SI]); clocks = 8; def = SS; break;
    case 3: base = (uint16_t)(ctx.r[BP] + ctx.r[DI]); clocks = 7; def = SS; break;
    case 4: base = ctx.r[SI]; clocks = 5; break;
    case 5: base = ctx.r[DI]; clocks = 5; break;
    case 6: base = ctx.r[BP]; clocks = 5; def = SS; break;
    default: base = ctx.r[BX]; clocks = 5; break;
    }
    if (m_mod == 1) {
        base = (uint16_t)(base + (int8_t)q_take());
        clocks += 4;
    } else if (m_mod == 2) {
        base = (uint16_t)(base + q_take16());
        clocks += 4;
    }
    m_ea = base;
    m_ea_seg = seg_or(def);
    clk(clocks);
}

uint8_t i8086core::get_rm8()
{
    if (m_mod == 3) return get_r8(m_rm);
    return rd8(ctx.s[m_ea_seg], m_ea);
}

uint16_t i8086core::get_rm16()
{
    if (m_mod == 3) return ctx.r[m_rm];
    return rd16(ctx.s[m_ea_seg], m_ea);
}

void i8086core::set_rm8(uint8_t v)
{
    if (m_mod == 3) set_r8(m_rm, v);
    else wr8(ctx.s[m_ea_seg], m_ea, v);
}

void i8086core::set_rm16(uint16_t v)
{
    if (m_mod == 3) ctx.r[m_rm] = v;
    else wr16(ctx.s[m_ea_seg], m_ea, v);
}

//------------------------------- Arithmetic ----------------------------------

void i8086core::set_szp8(uint8_t v)
{
    set_flag(F_SF, (v & 0x80) != 0);
    set_flag(F_ZF, v == 0);
    set_flag(F_PF, even_parity(v));
}

void i8086core::set_szp16(uint16_t v)
{
    set_flag(F_SF, (v & 0x8000) != 0);
    set_flag(F_ZF, v == 0);
    set_flag(F_PF, even_parity((uint8_t)v));
}

//op is the operation field of the ALU instructions: ADD OR ADC SBB AND SUB XOR CMP
static inline uint32_t alu(i8086context &c, unsigned op, uint32_t a, uint32_t b, unsigned bits)
{
    const uint32_t mask = (bits == 8) ? 0xFF : 0xFFFF;
    const uint32_t sign = (bits == 8) ? 0x80 : 0x8000;
    const uint32_t carry = (c.flags & F_CF) ? 1 : 0;
    uint32_t r;
    uint16_t f = c.flags & ~(F_CF | F_PF | F_AF | F_ZF | F_SF | F_OF);
    switch (op) {
    case 0: case 2: {
        const uint32_t ci = (op == 2) ? carry : 0;
        r = a + b + ci;
        if (r > mask) f |= F_CF;
        if ((a ^ r) & (b ^ r) & sign) f |= F_OF;
        if ((a ^ b ^ r) & 0x10) f |= F_AF;
        break;
    }
    case 3: case 5: case 7: {
        const uint32_t ci = (op == 3) ? carry : 0;
        r = a - b - ci;
        if (a < b + ci) f |= F_CF;
        if ((a ^ b) & (a ^ r) & sign) f |= F_OF;
        if ((a ^ b ^ r) & 0x10) f |= F_AF;
        break;
    }
    case 1: r = a | b; break;
    case 4: r = a & b; break;
    default: r = a ^ b; break;
    }
    r &= mask;
    if (r & sign) f |= F_SF;
    if (r == 0) f |= F_ZF;
    if (even_parity((uint8_t)r)) f |= F_PF;
    c.flags = f;
    return r;
}

uint8_t i8086core::alu8(unsigned op, uint8_t a, uint8_t b)
{
    return (uint8_t)alu(ctx, op, a, b, 8);
}

uint16_t i8086core::alu16(unsigned op, uint16_t a, uint16_t b)
{
    return (uint16_t)alu(ctx, op, a, b, 16);
}

uint8_t i8086core::inc8(uint8_t v)
{
    const uint8_t r = (uint8_t)(v + 1);
    set_flag(F_OF, r == 0x80);
    set_flag(F_AF, (r & 0x0F) == 0);
    set_szp8(r);
    return r;
}

uint8_t i8086core::dec8(uint8_t v)
{
    const uint8_t r = (uint8_t)(v - 1);
    set_flag(F_OF, v == 0x80);
    set_flag(F_AF, (v & 0x0F) == 0);
    set_szp8(r);
    return r;
}

uint16_t i8086core::inc16(uint16_t v)
{
    const uint16_t r = (uint16_t)(v + 1);
    set_flag(F_OF, r == 0x8000);
    set_flag(F_AF, (r & 0x0F) == 0);
    set_szp16(r);
    return r;
}

uint16_t i8086core::dec16(uint16_t v)
{
    const uint16_t r = (uint16_t)(v - 1);
    set_flag(F_OF, v == 0x8000);
    set_flag(F_AF, (v & 0x0F) == 0);
    set_szp16(r);
    return r;
}

//The chip shifts one bit at a time, count times, the count is not masked: the
//flags left are those of the last step. op is the reg field of D0-D3; 6 is
//SETMO of the NMOS chip, which fills the operand with ones
uint16_t i8086core::shift(unsigned op, uint16_t v, unsigned count, bool word)
{
    if (count == 0) return v;
    const uint32_t mask = word ? 0xFFFF : 0xFF;
    const uint32_t sign = word ? 0x8000 : 0x80;
    uint32_t x = v & mask;
    bool cf = flag(F_CF);
    bool of = flag(F_OF);

    if (op == 6) {
        x = mask;
        cf = false;
        of = false;
        set_flag(F_AF, false);
    } else {
        for (unsigned i = 0; i < count; i++) {
            switch (op) {
            case 0: //ROL
                cf = (x & sign) != 0;
                x = ((x << 1) | (cf ? 1 : 0)) & mask;
                of = ((x & sign) != 0) != cf;
                break;
            case 1: //ROR
                cf = (x & 1) != 0;
                x = (x >> 1) | (cf ? sign : 0);
                of = ((x & sign) != 0) != ((x & (sign >> 1)) != 0);
                break;
            case 2: { //RCL
                const bool out = (x & sign) != 0;
                x = ((x << 1) | (cf ? 1 : 0)) & mask;
                cf = out;
                of = ((x & sign) != 0) != cf;
                break;
            }
            case 3: { //RCR
                const bool out = (x & 1) != 0;
                x = (x >> 1) | (cf ? sign : 0);
                cf = out;
                of = ((x & sign) != 0) != ((x & (sign >> 1)) != 0);
                break;
            }
            case 4: //SHL
                cf = (x & sign) != 0;
                x = (x << 1) & mask;
                of = ((x & sign) != 0) != cf;
                break;
            case 5: //SHR
                cf = (x & 1) != 0;
                of = (x & sign) != 0;
                x >>= 1;
                break;
            default: //SAR
                cf = (x & 1) != 0;
                x = (x >> 1) | (x & sign);
                of = false;
                break;
            }
        }
    }
    set_flag(F_CF, cf);
    set_flag(F_OF, of);
    if (op >= 4) {
        if (word) set_szp16((uint16_t)x); else set_szp8((uint8_t)x);
        //The NMOS chip leaves AF set by its own adder; nothing documents it
        if (op != 6) set_flag(F_AF, (x & 0x10) != 0);
    }
    return (uint16_t)x;
}

//Divide error: the 8086 pushes the address of the next instruction
bool i8086core::div8(bool sign, uint8_t d)
{
    const uint16_t ax = ctx.r[AX];
    if (!sign) {
        if (d == 0) return false;
        const unsigned q = ax / d;
        if (q > 0xFF) return false;
        clk(76 + bit_count(q));
        ctx.r[AX] = (uint16_t)(((ax % d) << 8) | q);
    } else {
        const int n = (int16_t)ax;
        const int dv = (int8_t)d;
        if (dv == 0) return false;
        int q = n / dv;
        const int r = n % dv;
        if (q > 127 || q < -127) return false;
        //A REP prefix flips the sign of the quotient on the NMOS chip
        if (m_rep != 0) q = -q;
        clk(97 + bit_count((unsigned)(q < 0 ? -q : q)));
        ctx.r[AX] = (uint16_t)((((unsigned)r & 0xFF) << 8) | ((unsigned)q & 0xFF));
    }
    return true;
}

bool i8086core::div16(bool sign, uint16_t d)
{
    const uint32_t n = ((uint32_t)ctx.r[DX] << 16) | ctx.r[AX];
    if (!sign) {
        if (d == 0) return false;
        const uint32_t q = n / d;
        if (q > 0xFFFF) return false;
        clk(140 + bit_count(q));
        ctx.r[AX] = (uint16_t)q;
        ctx.r[DX] = (uint16_t)(n % d);
    } else {
        const int64_t sn = (int32_t)n;
        const int64_t dv = (int16_t)d;
        if (dv == 0) return false;
        int64_t q = sn / dv;
        const int64_t r = sn % dv;
        if (q > 32767 || q < -32767) return false;
        if (m_rep != 0) q = -q;
        clk(161 + bit_count((uint32_t)(q < 0 ? -q : q)));
        ctx.r[AX] = (uint16_t)q;
        ctx.r[DX] = (uint16_t)r;
    }
    return true;
}

void i8086core::group3_8()
{
    decode_modrm();
    const bool mem = m_mod != 3;
    switch (m_reg) {
    case 0: case 1: { //TEST
        const uint8_t v = get_rm8();
        const uint8_t imm = q_take();
        alu8(4, v, imm);
        clk(mem ? 7 : 5);
        break;
    }
    case 2: { //NOT
        const uint8_t v = get_rm8();
        clk(mem ? 8 : 3);
        set_rm8((uint8_t)~v);
        break;
    }
    case 3: { //NEG
        const uint8_t v = get_rm8();
        clk(mem ? 8 : 3);
        set_rm8(alu8(5, 0, v));
        break;
    }
    case 4: { //MUL
        const uint8_t v = get_rm8();
        const uint16_t r = (uint16_t)(get_r8(0) * v);
        clk((mem ? 72 : 70) + bit_count(v));
        ctx.r[AX] = r;
        set_flag(F_CF, (r >> 8) != 0);
        set_flag(F_OF, (r >> 8) != 0);
        set_szp8((uint8_t)(r >> 8));
        break;
    }
    case 5: { //IMUL
        const uint8_t v = get_rm8();
        int r = (int8_t)get_r8(0) * (int8_t)v;
        if (m_rep != 0) r = -r;
        clk((mem ? 82 : 80) + 2 * bit_count(v));
        ctx.r[AX] = (uint16_t)r;
        const bool ext = (int16_t)ctx.r[AX] != (int8_t)ctx.r[AX];
        set_flag(F_CF, ext);
        set_flag(F_OF, ext);
        set_szp8((uint8_t)(ctx.r[AX] >> 8));
        break;
    }
    default: { //DIV, IDIV
        const uint8_t v = get_rm8();
        if (mem) clk(2);
        if (!div8(m_reg == 7, v)) interrupt(0, false);
        break;
    }
    }
}

void i8086core::group3_16()
{
    decode_modrm();
    const bool mem = m_mod != 3;
    switch (m_reg) {
    case 0: case 1: {
        const uint16_t v = get_rm16();
        const uint16_t imm = q_take16();
        alu16(4, v, imm);
        clk(mem ? 7 : 5);
        break;
    }
    case 2: {
        const uint16_t v = get_rm16();
        clk(mem ? 8 : 3);
        set_rm16((uint16_t)~v);
        break;
    }
    case 3: {
        const uint16_t v = get_rm16();
        clk(mem ? 8 : 3);
        set_rm16(alu16(5, 0, v));
        break;
    }
    case 4: {
        const uint16_t v = get_rm16();
        const uint32_t r = (uint32_t)ctx.r[AX] * v;
        clk((mem ? 120 : 118) + bit_count(v));
        ctx.r[AX] = (uint16_t)r;
        ctx.r[DX] = (uint16_t)(r >> 16);
        set_flag(F_CF, ctx.r[DX] != 0);
        set_flag(F_OF, ctx.r[DX] != 0);
        set_szp16(ctx.r[DX]);
        break;
    }
    case 5: {
        const uint16_t v = get_rm16();
        int32_t r = (int32_t)(int16_t)ctx.r[AX] * (int16_t)v;
        if (m_rep != 0) r = -r;
        clk((mem ? 130 : 128) + bit_count(v) * 3 / 2);
        ctx.r[AX] = (uint16_t)r;
        ctx.r[DX] = (uint16_t)((uint32_t)r >> 16);
        const bool ext = r != (int16_t)ctx.r[AX];
        set_flag(F_CF, ext);
        set_flag(F_OF, ext);
        set_szp16(ctx.r[DX]);
        break;
    }
    default: {
        const uint16_t v = get_rm16();
        if (mem) clk(2);
        if (!div16(m_reg == 7, v)) interrupt(0, false);
        break;
    }
    }
}

bool i8086core::condition(unsigned cc) const
{
    bool r;
    switch (cc >> 1) {
    case 0: r = flag(F_OF); break;
    case 1: r = flag(F_CF); break;
    case 2: r = flag(F_ZF); break;
    case 3: r = flag(F_CF) || flag(F_ZF); break;
    case 4: r = flag(F_SF); break;
    case 5: r = flag(F_PF); break;
    case 6: r = flag(F_SF) != flag(F_OF); break;
    default: r = flag(F_ZF) || (flag(F_SF) != flag(F_OF)); break;
    }
    return (cc & 1) ? !r : r;
}

//------------------------------- Control -------------------------------------

//Reads the vector first, then pushes, as the microcode does. IP pushed is
//whatever IP holds now: the next instruction, or the prefix an interrupted
//repetition goes back to
void i8086core::interrupt(uint8_t vector, bool hardware)
{
    IntRecord &rec = int_log[int_log_pos++ & (INT_LOG_SIZE - 1)];
    rec.vector = vector;
    rec.ax = ctx.r[AX];
    rec.from = linear(ctx.s[CS], ctx.ip);
    clk(hardware ? 29 : 27);
    const uint16_t new_ip = rd16(0, (uint16_t)(vector * 4));
    const uint16_t new_cs = rd16(0, (uint16_t)(vector * 4 + 2));
    push(get_flags());
    set_flag(F_IF, false);
    set_flag(F_TF, false);
    push(ctx.s[CS]);
    push(ctx.ip);
    far_jump(new_cs, new_ip);
    ctx.halted = false;
}

bool i8086core::interrupt_pending() const
{
    return bus.trap || bus.nmi_pending || (bus.intr && flag(F_IF));
}

void i8086core::take_interrupt_at_boundary()
{
    //An external interrupt is entered first; a pending single step then goes
    //in on top of it, before the first instruction of that handler (the
    //flowchart of the iAPX 86/88 manual), and returns into it
    const bool trap = bus.trap;
    bus.trap = false;
    if (bus.nmi_pending) {
        bus.nmi_pending = false;
        interrupt(2, false);
    } else if (bus.intr && flag(F_IF)) {
        clk(4);
        const uint8_t v = inta_cycles();
        interrupt(v, true);
    }
    if (trap) interrupt(1, false);
}

//One pass of a string instruction. Returns false when it is the last one: no
//repetition, CX run out, or the compare said stop
bool i8086core::string_op(uint8_t op)
{
    const bool word = (op & 1) != 0;
    const int16_t step = (int16_t)((flag(F_DF) ? -1 : 1) * (word ? 2 : 1));
    const uint16_t src_seg = ctx.s[seg_or(DS)];
    const bool rep = m_rep != 0;

    if (rep) {
        if (ctx.r[CX] == 0) return false;
    }

    switch (op) {
    case 0xA4: case 0xA5: //MOVS
        if (word) wr16(ctx.s[ES], ctx.r[DI], rd16(src_seg, ctx.r[SI]));
        else wr8(ctx.s[ES], ctx.r[DI], rd8(src_seg, ctx.r[SI]));
        ctx.r[SI] = (uint16_t)(ctx.r[SI] + step);
        ctx.r[DI] = (uint16_t)(ctx.r[DI] + step);
        clk(rep ? 9 : 10);
        break;
    case 0xA6: case 0xA7: { //CMPS
        if (word) {
            const uint16_t a = rd16(src_seg, ctx.r[SI]);
            alu16(7, a, rd16(ctx.s[ES], ctx.r[DI]));
        } else {
            const uint8_t a = rd8(src_seg, ctx.r[SI]);
            alu8(7, a, rd8(ctx.s[ES], ctx.r[DI]));
        }
        ctx.r[SI] = (uint16_t)(ctx.r[SI] + step);
        ctx.r[DI] = (uint16_t)(ctx.r[DI] + step);
        clk(14);
        break;
    }
    case 0xAA: case 0xAB: //STOS
        if (word) wr16(ctx.s[ES], ctx.r[DI], ctx.r[AX]);
        else wr8(ctx.s[ES], ctx.r[DI], (uint8_t)ctx.r[AX]);
        ctx.r[DI] = (uint16_t)(ctx.r[DI] + step);
        clk(rep ? 6 : 7);
        break;
    case 0xAC: case 0xAD: //LODS
        if (word) ctx.r[AX] = rd16(src_seg, ctx.r[SI]);
        else set_r8(0, rd8(src_seg, ctx.r[SI]));
        ctx.r[SI] = (uint16_t)(ctx.r[SI] + step);
        clk(rep ? 9 : 8);
        break;
    default: //SCAS
        if (word) alu16(7, ctx.r[AX], rd16(ctx.s[ES], ctx.r[DI]));
        else alu8(7, (uint8_t)ctx.r[AX], rd8(ctx.s[ES], ctx.r[DI]));
        ctx.r[DI] = (uint16_t)(ctx.r[DI] + step);
        clk(11);
        break;
    }

    if (!rep) return false;
    ctx.r[CX] = (uint16_t)(ctx.r[CX] - 1);
    if (ctx.r[CX] == 0) return false;
    if (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF) {
        if (m_rep == 0xF3 && !flag(F_ZF)) return false;
        if (m_rep == 0xF2 && flag(F_ZF)) return false;
    }
    return true;
}

unsigned i8086core::execute()
{
    m_start = bus.t;

    if (bus.rep_active) {
        if (interrupt_pending()) {
            //Back to the prefix right before the opcode: a second prefix is
            //lost, as on the chip
            bus.rep_active = false;
            flush(bus.rep_last_prefix_ip);
            take_interrupt_at_boundary();
        } else {
            m_seg = bus.rep_seg;
            m_rep = bus.rep_prefix;
            m_instr_ip = bus.rep_ip;
            m_last_prefix_ip = bus.rep_last_prefix_ip;
            const bool trap = flag(F_TF);
            bus.rep_active = string_op(bus.rep_opcode);
            if (trap && flag(F_TF)) bus.trap = true;
        }
    } else if (ctx.halted) {
        //A HLT executed with TF set is followed by its trap like any other
        //instruction, and the trap returns past the HLT
        if (bus.nmi_pending || bus.trap || (bus.intr && flag(F_IF))) {
            ctx.halted = false;
            take_interrupt_at_boundary();
        } else {
            clk(4);
        }
    } else if (!bus.inhibit && interrupt_pending()) {
        take_interrupt_at_boundary();
    } else {
        bus.inhibit = false;
        const bool trap = flag(F_TF);
        m_seg = -1;
        m_rep = 0;
        m_instr_ip = ctx.ip;
        m_last_prefix_ip = ctx.ip;
        uint8_t op = q_take();
        m_first_take = bus.t;
        for (;;) {
            if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) {
                m_seg = (op >> 3) & 3;
            } else if (op == 0xF2 || op == 0xF3) {
                m_rep = op;
            } else if (op != 0xF0 && op != 0xF1) {
                break;
            }
            clk(2);
            m_last_prefix_ip = (uint16_t)(ctx.ip - 1);
            op = q_take();
        }
        m_opcode_ip = (uint16_t)(ctx.ip - 1);
        execute_opcode(op);
        //The trap is taken after an instruction that began with TF set, and
        //not after one that cleared it (INT) or set it (IRET, POPF). After
        //every pass of a repeated string instruction too, the first included:
        //the repetition then resumes from its prefix
        if (trap && flag(F_TF)) bus.trap = true;
    }

    //Everything the bus begins before the end of this slice belongs to it
    prefetch_until(bus.t);
    const uint64_t spent = bus.t - m_start;
    if (spent == 0) { bus.t++; return 1; }
    return (unsigned)spent;
}

void i8086core::execute_opcode(uint8_t op)
{
    switch (op) {
    //ALU r/m, reg and reg, r/m and acc, imm: ADD OR ADC SBB AND SUB XOR CMP
    case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: {
        const unsigned f = (op >> 3) & 7;
        decode_modrm();
        const uint8_t a = get_rm8();
        const uint8_t r = alu8(f, a, get_r8(m_reg));
        if (m_mod == 3) { clk(3); if (f != 7) set_r8(m_rm, r); }
        else if (f == 7) clk(5);
        else { clk(8); set_rm8(r); }
        break;
    }
    case 0x01: case 0x09: case 0x11: case 0x19: case 0x21: case 0x29: case 0x31: case 0x39: {
        const unsigned f = (op >> 3) & 7;
        decode_modrm();
        const uint16_t a = get_rm16();
        const uint16_t r = alu16(f, a, ctx.r[m_reg]);
        if (m_mod == 3) { clk(3); if (f != 7) ctx.r[m_rm] = r; }
        else if (f == 7) clk(5);
        else { clk(8); set_rm16(r); }
        break;
    }
    case 0x02: case 0x0A: case 0x12: case 0x1A: case 0x22: case 0x2A: case 0x32: case 0x3A: {
        const unsigned f = (op >> 3) & 7;
        decode_modrm();
        const uint8_t b = get_rm8();
        const uint8_t r = alu8(f, get_r8(m_reg), b);
        clk(m_mod == 3 ? 3 : 5);
        if (f != 7) set_r8(m_reg, r);
        break;
    }
    case 0x03: case 0x0B: case 0x13: case 0x1B: case 0x23: case 0x2B: case 0x33: case 0x3B: {
        const unsigned f = (op >> 3) & 7;
        decode_modrm();
        const uint16_t b = get_rm16();
        const uint16_t r = alu16(f, ctx.r[m_reg], b);
        clk(m_mod == 3 ? 3 : 5);
        if (f != 7) ctx.r[m_reg] = r;
        break;
    }
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: {
        const unsigned f = (op >> 3) & 7;
        const uint8_t r = alu8(f, (uint8_t)ctx.r[AX], q_take());
        clk(4);
        if (f != 7) set_r8(0, r);
        break;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
        const unsigned f = (op >> 3) & 7;
        const uint16_t r = alu16(f, ctx.r[AX], q_take16());
        clk(4);
        if (f != 7) ctx.r[AX] = r;
        break;
    }

    //PUSH/POP segment registers; 0F is POP CS on the NMOS chip
    case 0x06: case 0x0E: case 0x16: case 0x1E:
        clk(6);
        push(ctx.s[(op >> 3) & 3]);
        break;
    case 0x07: case 0x0F: case 0x17: case 0x1F:
        clk(4);
        ctx.s[(op >> 3) & 3] = pop();
        bus.inhibit = true;
        break;

    case 0x27: { //DAA
        const uint8_t al = (uint8_t)ctx.r[AX];
        const bool cf = flag(F_CF);
        const bool af = flag(F_AF);
        uint8_t r = al;
        if ((al & 0x0F) > 9 || flag(F_AF)) { r = (uint8_t)(r + 6); set_flag(F_AF, true); }
        else set_flag(F_AF, false);
        if (al > (af ? 0x9F : 0x99) || cf) { r = (uint8_t)(r + 0x60); set_flag(F_CF, true); }
        else set_flag(F_CF, false);
        set_flag(F_OF, ((al ^ r) & r & 0x80) != 0);
        set_r8(0, r);
        set_szp8(r);
        clk(4);
        break;
    }
    case 0x2F: { //DAS
        const uint8_t al = (uint8_t)ctx.r[AX];
        const bool cf = flag(F_CF);
        const bool af = flag(F_AF);
        uint8_t r = al;
        if ((al & 0x0F) > 9 || flag(F_AF)) { r = (uint8_t)(r - 6); set_flag(F_AF, true); }
        else set_flag(F_AF, false);
        if (al > (af ? 0x9F : 0x99) || cf) { r = (uint8_t)(r - 0x60); set_flag(F_CF, true); }
        else set_flag(F_CF, false);
        set_flag(F_OF, ((al ^ r) & al & 0x80) != 0);
        set_r8(0, r);
        set_szp8(r);
        clk(4);
        break;
    }
    case 0x37: case 0x3F: { //AAA, AAS
        const uint8_t al = (uint8_t)ctx.r[AX];
        if ((al & 0x0F) > 9 || flag(F_AF)) {
            if (op == 0x37) {
                set_r8(0, (uint8_t)(al + 6));
                set_r8(4, (uint8_t)(get_r8(4) + 1));
            } else {
                set_r8(0, (uint8_t)(al - 6));
                set_r8(4, (uint8_t)(get_r8(4) - 1));
            }
            set_flag(F_AF, true);
            set_flag(F_CF, true);
        } else {
            set_flag(F_AF, false);
            set_flag(F_CF, false);
        }
        set_szp8(get_r8(0));
        set_r8(0, get_r8(0) & 0x0F);
        clk(4);
        break;
    }

    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        ctx.r[op & 7] = inc16(ctx.r[op & 7]);
        clk(2);
        break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        ctx.r[op & 7] = dec16(ctx.r[op & 7]);
        clk(2);
        break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        clk(7);
        if ((op & 7) == SP) {
            //The 8086 pushes the value SP has after the decrement
            ctx.r[SP] = (uint16_t)(ctx.r[SP] - 2);
            wr16(ctx.s[SS], ctx.r[SP], ctx.r[SP]);
        } else
            push(ctx.r[op & 7]);
        break;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        clk(4);
        const uint16_t v = pop();
        ctx.r[op & 7] = v;
        break;
    }

    //60-6F repeat 70-7F on the NMOS chip
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        const int8_t d = (int8_t)q_take();
        if (condition(op & 0x0F)) {
            clk(12);
            flush((uint16_t)(ctx.ip + d));
        } else
            clk(4);
        break;
    }

    case 0x80: case 0x82: { //82 is 80 on the NMOS chip
        decode_modrm();
        const uint8_t a = get_rm8();
        const uint8_t imm = q_take();
        const uint8_t r = alu8(m_reg, a, imm);
        if (m_mod == 3) { clk(4); if (m_reg != 7) set_r8(m_rm, r); }
        else if (m_reg == 7) clk(6);
        else { clk(9); set_rm8(r); }
        break;
    }
    case 0x81: case 0x83: {
        decode_modrm();
        const uint16_t a = get_rm16();
        const uint16_t imm = (op == 0x81) ? q_take16() : (uint16_t)(int8_t)q_take();
        const uint16_t r = alu16(m_reg, a, imm);
        if (m_mod == 3) { clk(4); if (m_reg != 7) ctx.r[m_rm] = r; }
        else if (m_reg == 7) clk(6);
        else { clk(9); set_rm16(r); }
        break;
    }
    case 0x84: {
        decode_modrm();
        alu8(4, get_rm8(), get_r8(m_reg));
        clk(m_mod == 3 ? 3 : 5);
        break;
    }
    case 0x85: {
        decode_modrm();
        alu16(4, get_rm16(), ctx.r[m_reg]);
        clk(m_mod == 3 ? 3 : 5);
        break;
    }
    case 0x86: {
        decode_modrm();
        const uint8_t a = get_rm8();
        clk(m_mod == 3 ? 4 : 9);
        set_rm8(get_r8(m_reg));
        set_r8(m_reg, a);
        break;
    }
    case 0x87: {
        decode_modrm();
        const uint16_t a = get_rm16();
        clk(m_mod == 3 ? 4 : 9);
        set_rm16(ctx.r[m_reg]);
        ctx.r[m_reg] = a;
        break;
    }
    case 0x88:
        decode_modrm();
        clk(m_mod == 3 ? 2 : 5);
        set_rm8(get_r8(m_reg));
        break;
    case 0x89:
        decode_modrm();
        clk(m_mod == 3 ? 2 : 5);
        set_rm16(ctx.r[m_reg]);
        break;
    case 0x8A: {
        decode_modrm();
        const uint8_t v = get_rm8();
        clk(m_mod == 3 ? 2 : 4);
        set_r8(m_reg, v);
        break;
    }
    case 0x8B: {
        decode_modrm();
        const uint16_t v = get_rm16();
        clk(m_mod == 3 ? 2 : 4);
        ctx.r[m_reg] = v;
        break;
    }
    case 0x8C: //only two bits of reg are decoded
        decode_modrm();
        clk(m_mod == 3 ? 2 : 5);
        set_rm16(ctx.s[m_reg & 3]);
        break;
    case 0x8D: //LEA; with a register operand the chip gives the last address it formed
        decode_modrm();
        clk(2);
        ctx.r[m_reg] = m_ea;
        break;
    case 0x8E: { //MOV sreg: CS too, without emptying the queue, as on the chip
        decode_modrm();
        const uint16_t v = get_rm16();
        clk(m_mod == 3 ? 2 : 4);
        ctx.s[m_reg & 3] = v;
        bus.inhibit = true;
        break;
    }
    case 0x8F: { //POP r/m
        decode_modrm();
        clk(m_mod == 3 ? 4 : 9);
        const uint16_t v = pop();
        set_rm16(v);
        break;
    }

    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        const uint16_t v = ctx.r[op & 7];
        ctx.r[op & 7] = ctx.r[AX];
        ctx.r[AX] = v;
        clk(3);
        break;
    }
    case 0x98: //CBW
        ctx.r[AX] = (uint16_t)(int8_t)ctx.r[AX];
        clk(2);
        break;
    case 0x99: //CWD
        ctx.r[DX] = (ctx.r[AX] & 0x8000) ? 0xFFFF : 0;
        clk(5);
        break;
    case 0x9A: { //CALL far
        const uint16_t ip = q_take16();
        const uint16_t cs = q_take16();
        clk(16);
        push(ctx.s[CS]);
        push(ctx.ip);
        far_jump(cs, ip);
        break;
    }
    case 0x9B: //WAIT: there is no coprocessor to hold TEST
        clk(3);
        break;
    case 0x9C:
        clk(6);
        push(get_flags());
        break;
    case 0x9D:
        clk(4);
        set_flags(pop());
        break;
    case 0x9E: //SAHF
        ctx.flags = (uint16_t)((ctx.flags & 0xFF00) | (get_r8(4) & (F_SF | F_ZF | F_AF | F_PF | F_CF)));
        clk(4);
        break;
    case 0x9F: //LAHF
        set_r8(4, (uint8_t)(get_flags() & 0xFF));
        clk(4);
        break;

    case 0xA0: {
        const uint16_t a = q_take16();
        set_r8(0, rd8(ctx.s[seg_or(DS)], a));
        clk(6);
        break;
    }
    case 0xA1: {
        const uint16_t a = q_take16();
        ctx.r[AX] = rd16(ctx.s[seg_or(DS)], a);
        clk(6);
        break;
    }
    case 0xA2: {
        const uint16_t a = q_take16();
        clk(6);
        wr8(ctx.s[seg_or(DS)], a, (uint8_t)ctx.r[AX]);
        break;
    }
    case 0xA3: {
        const uint16_t a = q_take16();
        clk(6);
        wr16(ctx.s[seg_or(DS)], a, ctx.r[AX]);
        break;
    }

    case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        if (m_rep != 0) {
            clk(7);
            if (string_op(op)) {
                bus.rep_active = true;
                bus.rep_opcode = op;
                bus.rep_prefix = m_rep;
                bus.rep_seg = m_seg;
                bus.rep_ip = m_instr_ip;
                bus.rep_last_prefix_ip = m_last_prefix_ip;
            }
        } else
            string_op(op);
        break;

    case 0xA8:
        alu8(4, (uint8_t)ctx.r[AX], q_take());
        clk(4);
        break;
    case 0xA9:
        alu16(4, ctx.r[AX], q_take16());
        clk(4);
        break;

    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        set_r8(op & 7, q_take());
        clk(4);
        break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        ctx.r[op & 7] = q_take16();
        clk(4);
        break;

    //C0, C1, C8, C9 repeat C2, C3, CA, CB on the NMOS chip
    case 0xC0: case 0xC2: {
        const uint16_t n = q_take16();
        clk(4);
        const uint16_t ip = pop();
        ctx.r[SP] = (uint16_t)(ctx.r[SP] + n);
        flush(ip);
        break;
    }
    case 0xC1: case 0xC3:
        flush(pop());
        break;
    case 0xC8: case 0xCA: {
        const uint16_t n = q_take16();
        clk(5);
        const uint16_t ip = pop();
        ctx.s[CS] = pop();
        ctx.r[SP] = (uint16_t)(ctx.r[SP] + n);
        flush(ip);
        break;
    }
    case 0xC9: case 0xCB: {
        clk(6);
        const uint16_t ip = pop();
        ctx.s[CS] = pop();
        flush(ip);
        break;
    }
    case 0xC4: case 0xC5: { //LES, LDS
        decode_modrm();
        const uint16_t seg = ctx.s[m_ea_seg];
        const uint16_t v = rd16(seg, m_ea);
        const uint16_t s = rd16(seg, (uint16_t)(m_ea + 2));
        clk(8);
        ctx.r[m_reg] = v;
        ctx.s[(op == 0xC4) ? ES : DS] = s;
        break;
    }
    case 0xC6: { //reg is not decoded
        decode_modrm();
        const uint8_t v = q_take();
        clk(m_mod == 3 ? 4 : 6);
        set_rm8(v);
        break;
    }
    case 0xC7: {
        decode_modrm();
        const uint16_t v = q_take16();
        clk(m_mod == 3 ? 4 : 6);
        set_rm16(v);
        break;
    }
    case 0xCC:
        clk(1);
        interrupt(3, false);
        break;
    case 0xCD: {
        const uint8_t v = q_take();
        interrupt(v, false);
        break;
    }
    case 0xCE:
        if (flag(F_OF)) {
            clk(2);
            interrupt(4, false);
        } else
            clk(4);
        break;
    case 0xCF: { //IRET
        clk(8);
        const uint16_t ip = pop();
        ctx.s[CS] = pop();
        set_flags(pop());
        flush(ip);
        break;
    }

    case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        decode_modrm();
        const bool word = (op & 1) != 0;
        const bool by_cl = (op & 2) != 0;
        const unsigned n = by_cl ? (ctx.r[CX] & 0xFF) : 1;
        const uint16_t v = word ? get_rm16() : get_rm8();
        if (by_cl) clk((m_mod == 3 ? 8 : 12) + 4 * n);
        else clk(m_mod == 3 ? 2 : 7);
        const uint16_t r = shift(m_reg, v, n, word);
        if (n != 0) {
            if (word) set_rm16(r); else set_rm8((uint8_t)r);
        }
        break;
    }
    case 0xD4: { //AAM
        const uint8_t base = q_take();
        if (base == 0) {
            //The microcode has already compared AL with zero: the flags pushed
            //are those of a zero result
            ctx.flags = (uint16_t)((ctx.flags & ~(F_SF | F_ZF | F_AF | F_PF | F_CF | F_OF)) | F_ZF | F_PF);
            clk(2);
            interrupt(0, false);
            break;
        }
        const uint8_t al = (uint8_t)ctx.r[AX];
        set_r8(4, (uint8_t)(al / base));
        set_r8(0, (uint8_t)(al % base));
        set_szp8(get_r8(0));
        clk(83);
        break;
    }
    case 0xD5: { //AAD: an addition whose flags it leaves
        const uint8_t base = q_take();
        const uint8_t r = alu8(0, (uint8_t)ctx.r[AX], (uint8_t)(get_r8(4) * base));
        ctx.r[AX] = r;
        clk(60);
        break;
    }
    case 0xD6: //SALC
        set_r8(0, flag(F_CF) ? 0xFF : 0x00);
        clk(4);
        break;
    case 0xD7: //XLAT
        set_r8(0, rd8(ctx.s[seg_or(DS)], (uint16_t)(ctx.r[BX] + (ctx.r[AX] & 0xFF))));
        clk(7);
        break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        //ESC: the operand is read for a coprocessor that is not there
        decode_modrm();
        if (m_mod != 3) { clk(4); get_rm8(); }
        else clk(2);
        break;

    case 0xE0: case 0xE1: case 0xE2: { //LOOPNE, LOOPE, LOOP
        const int8_t d = (int8_t)q_take();
        ctx.r[CX] = (uint16_t)(ctx.r[CX] - 1);
        bool go = ctx.r[CX] != 0;
        if (op == 0xE0) go = go && !flag(F_ZF);
        if (op == 0xE1) go = go && flag(F_ZF);
        if (go) {
            clk(op == 0xE0 ? 15 : op == 0xE1 ? 14 : 13);
            flush((uint16_t)(ctx.ip + d));
        } else
            clk(op == 0xE1 ? 6 : 5);
        break;
    }
    case 0xE3: { //JCXZ
        const int8_t d = (int8_t)q_take();
        if (ctx.r[CX] == 0) {
            clk(14);
            flush((uint16_t)(ctx.ip + d));
        } else
            clk(6);
        break;
    }
    case 0xE4: {
        const uint8_t p = q_take();
        clk(6);
        set_r8(0, in8(p));
        break;
    }
    case 0xE5: {
        const uint8_t p = q_take();
        clk(6);
        ctx.r[AX] = in16(p);
        break;
    }
    case 0xE6: {
        const uint8_t p = q_take();
        clk(6);
        out8(p, (uint8_t)ctx.r[AX]);
        break;
    }
    case 0xE7: {
        const uint8_t p = q_take();
        clk(6);
        out16(p, ctx.r[AX]);
        break;
    }
    case 0xE8: { //CALL near
        const uint16_t d = q_take16();
        clk(11);
        push(ctx.ip);
        flush((uint16_t)(ctx.ip + d));
        break;
    }
    case 0xE9: {
        const uint16_t d = q_take16();
        clk(11);
        flush((uint16_t)(ctx.ip + d));
        break;
    }
    case 0xEA: {
        const uint16_t ip = q_take16();
        const uint16_t cs = q_take16();
        clk(11);
        far_jump(cs, ip);
        break;
    }
    case 0xEB: {
        const int8_t d = (int8_t)q_take();
        clk(11);
        flush((uint16_t)(ctx.ip + d));
        break;
    }
    case 0xEC:
        clk(4);
        set_r8(0, in8(ctx.r[DX]));
        break;
    case 0xED:
        clk(4);
        ctx.r[AX] = in16(ctx.r[DX]);
        break;
    case 0xEE:
        clk(4);
        out8(ctx.r[DX], (uint8_t)ctx.r[AX]);
        break;
    case 0xEF:
        clk(4);
        out16(ctx.r[DX], ctx.r[AX]);
        break;

    case 0xF4: //HLT
        clk(2);
        ctx.halted = true;
        break;
    case 0xF5:
        set_flag(F_CF, !flag(F_CF));
        clk(2);
        break;
    case 0xF6:
        group3_8();
        break;
    case 0xF7:
        group3_16();
        break;
    case 0xF8: set_flag(F_CF, false); clk(2); break;
    case 0xF9: set_flag(F_CF, true); clk(2); break;
    case 0xFA: set_flag(F_IF, false); clk(2); break;
    case 0xFB: set_flag(F_IF, true); clk(2); bus.inhibit = true; break;
    case 0xFC: set_flag(F_DF, false); clk(2); break;
    case 0xFD: set_flag(F_DF, true); clk(2); break;

    case 0xFE: case 0xFF: {
        decode_modrm();
        const bool word = op == 0xFF;
        const bool mem = m_mod != 3;
        //FE with reg 2-7 does what FF does, on a byte operand with the high
        //byte read as ones
        uint16_t v = word ? get_rm16() : (uint16_t)(0xFF00 | get_rm8());
        switch (m_reg) {
        case 0:
            clk(mem ? 7 : 3);
            if (word) set_rm16(inc16(v)); else set_rm8(inc8((uint8_t)v));
            break;
        case 1:
            clk(mem ? 7 : 3);
            if (word) set_rm16(dec16(v)); else set_rm8(dec8((uint8_t)v));
            break;
        case 2: //CALL r/m
            clk(mem ? 9 : 8);
            push(ctx.ip);
            flush(v);
            break;
        case 3: { //CALL far m
            const uint16_t cs = rd16(ctx.s[m_ea_seg], (uint16_t)(m_ea + 2));
            clk(17);
            push(ctx.s[CS]);
            push(ctx.ip);
            far_jump(cs, v);
            break;
        }
        case 4: //JMP r/m
            clk(mem ? 10 : 7);
            flush(v);
            break;
        case 5: { //JMP far m
            const uint16_t cs = rd16(ctx.s[m_ea_seg], (uint16_t)(m_ea + 2));
            clk(12);
            far_jump(cs, v);
            break;
        }
        default: //PUSH r/m; 7 repeats 6
            clk(mem ? 8 : 7);
            if (mem || m_rm != SP) push(v);
            else {
                ctx.r[SP] = (uint16_t)(ctx.r[SP] - 2);
                wr16(ctx.s[SS], ctx.r[SP], ctx.r[SP]);
            }
            break;
        }
        break;
    }

    default:
        //Prefixes are consumed by execute(); nothing else is left
        clk(2);
        break;
    }
}
