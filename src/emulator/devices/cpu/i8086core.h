// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 (К1810ВМ86/ВМ88) CPU core

#pragma once

#include <cstdint>

#include "i8086_context.h"

//The processor is two machines sharing one bus. The execution unit (EU) runs
//the instructions and takes their bytes from the prefetch queue; the bus
//interface unit (BIU) fills the queue whenever the bus is idle and the queue
//has room, and runs the bus cycles the EU asks for. A bus cycle is T1-T4, plus
//the wait states (Tw) the memory adds after T3.
//
//Both units are timed in clocks of the processor. The EU keeps its own time
//(t); the BIU is simulated lazily, only up to the moment the EU next needs it:
//every prefetch cycle that would have begun before that moment is run, in
//order, and occupies the bus until it ends. So the EU waits for a fetch in
//progress, and an instruction that spends many clocks inside the EU finds the
//queue full afterwards, exactly as on the chip.
//
//What the EU does inside is not simulated clock by clock (no microcode): its
//clocks come from the Intel tables, less 4 for every bus transfer the table
//counts, so the bus cycles themselves - whose length depends on the queue, the
//bus width and the memory - are counted by the BIU instead.
//
//The 8088 has an 8-bit bus and a 4-byte queue, and fetches as soon as one byte
//is free; the 8086 has a 16-bit bus and a 6-byte queue, fetches words at even
//addresses and needs two free bytes. Nothing else differs: the instruction set
//is the same, including the undocumented forms of the NMOS chip.
struct i8086bus_state
{
    //All times are absolute, in clocks
    uint64_t t;             //of the execution unit
    uint64_t bus_free;      //end of the last bus cycle
    uint64_t pf_ready;      //earliest a prefetch may begin: room in the queue
    uint16_t pf_ip;         //offset in CS of the next byte to prefetch
    uint8_t  q[I8086::QUEUE_MAX];
    uint64_t q_avail[I8086::QUEUE_MAX];  //when each byte entered the queue
    unsigned q_head;
    unsigned q_len;

    bool     nmi_pending;   //latched on the rising edge of NMI
    bool     nmi_level;
    bool     intr;          //level of INTR
    bool     inhibit;       //no interrupt after this instruction (MOV SS, STI...)
    bool     trap;          //TF was set when the last instruction began

    //A repeated string instruction runs one iteration per execute(); between
    //iterations its decoded form is kept here
    bool     rep_active;
    uint8_t  rep_opcode;
    uint8_t  rep_prefix;    //F2 or F3
    int      rep_seg;       //segment override or -1
    uint16_t rep_ip;        //of the first prefix
    uint16_t rep_last_prefix_ip;
};

class i8086core
{
public:
    explicit i8086core(int family);
    virtual ~i8086core() {}

    //One bus cycle each. The word forms are asked for by the 8086 only, and
    //only at an even address; the 8088 splits every word into two cycles
    virtual uint8_t  mem_read8(uint32_t address) = 0;
    virtual void     mem_write8(uint32_t address, uint8_t value) = 0;
    virtual uint16_t mem_read16(uint32_t address) = 0;
    virtual void     mem_write16(uint32_t address, uint16_t value) = 0;
    virtual uint8_t  io_read8(uint16_t port) = 0;
    virtual void     io_write8(uint16_t port, uint8_t value) = 0;
    virtual uint16_t io_read16(uint16_t port) = 0;
    virtual void     io_write16(uint16_t port, uint16_t value) = 0;

    //Asked right after each memory cycle: how many wait states the memory
    //that has just answered adds to it. offset is the T1 of that cycle, in
    //clocks from the start of the current execute()
    virtual unsigned mem_wait(uint32_t address, bool write, unsigned offset) { (void)address; (void)write; (void)offset; return 0; }

    //The second INTA cycle: the vector the interrupt controller puts on the bus
    virtual uint8_t  int_ack() = 0;

    void reset();
    //One instruction, one iteration of a repeated string instruction, or the
    //entry into an interrupt. Returns the clocks it took, never 0
    unsigned execute();
    //Clocks that passed with the bus taken away (HOLD)
    void idle(unsigned clocks);

    void set_nmi(bool level);
    void set_intr(bool level) { bus.intr = level; }

    i8086context * get_context() { return &ctx; }
    i8086bus_state * get_bus_state() { return &bus; }
    int family() const { return m_family; }
    unsigned queue_size() const { return m_qsize; }

    //Linear address of the next instruction (of the first prefix of an
    //interrupted repetition)
    uint32_t get_pc() const;
    //Changes IP from outside, the queue is emptied
    void set_ip(uint16_t ip);
    void set_cs(uint16_t cs);

    //For the tests that start with bytes already in the queue: they stand for
    //what was prefetched from CS:IP on, and are ready at once
    void preload_queue(const uint8_t * bytes, unsigned count);

    //When the first byte of the last instruction (its first prefix) was taken
    //from the queue: the tests count an instruction from there to the first
    //byte of the next one
    uint64_t first_byte_time() const { return m_first_take; }
    //Takes the next byte from the queue as an instruction would and returns
    //when that happened. For the tests only: the byte is consumed
    uint64_t take_next_byte() { q_take(); return bus.t; }

    //The last interrupts entered, for debugging: the vector, AX and the
    //linear address the interrupt returns to
    struct IntRecord { uint8_t vector; uint16_t ax; uint32_t from; };
    enum { INT_LOG_SIZE = 64 };
    IntRecord int_log[INT_LOG_SIZE] = {};
    unsigned int_log_pos = 0;

    uint16_t get_flags() const { return (uint16_t)((ctx.flags & I8086::F_MASK) | I8086::F_ONES); }
    void set_flags(uint16_t f) { ctx.flags = f & I8086::F_MASK; }

    //I/O cycles of the machine that have extra wait states (an XT adds one)
    unsigned io_wait = 0;

private:
    i8086context   ctx;
    i8086bus_state bus;
    const int      m_family;
    const unsigned m_qsize;         //4 or 6
    const unsigned m_fetch_room;    //free bytes needed to start a fetch: 1 or 2
    const bool     m_wide;          //16-bit bus

    uint64_t m_start;               //t at the start of execute()
    uint64_t m_first_take;

    //The instruction being decoded
    int      m_seg;                 //segment override, -1: none
    uint8_t  m_rep;                 //0, F2 or F3
    uint16_t m_instr_ip;            //first prefix
    uint16_t m_last_prefix_ip;      //the prefix right before the opcode
    uint16_t m_opcode_ip;
    uint8_t  m_mod, m_reg, m_rm;
    uint16_t m_ea;                  //offset of the memory operand
    int      m_ea_seg;              //its segment, override applied

    //Timing
    void clk(unsigned n) { bus.t += n; }
    void prefetch_until(uint64_t t);
    void fetch_cycle(uint64_t start);
    uint8_t q_take();
    uint16_t q_take16();
    void flush(uint16_t new_ip);
    //A jump to another segment: the cycles the BIU began before it still
    //fetch from the old CS
    void far_jump(uint16_t cs, uint16_t ip);

    unsigned offset_of(uint64_t start) const { return (unsigned)(start - m_start); }

    //Bus cycles of the execution unit; seg is the value of a segment register
    uint8_t  rd8(uint16_t seg, uint16_t off);
    uint16_t rd16(uint16_t seg, uint16_t off);
    void     wr8(uint16_t seg, uint16_t off, uint8_t v);
    void     wr16(uint16_t seg, uint16_t off, uint16_t v);
    uint8_t  in8(uint16_t port);
    uint16_t in16(uint16_t port);
    void     out8(uint16_t port, uint8_t v);
    void     out16(uint16_t port, uint16_t v);
    uint64_t eu_cycle_start();
    void     eu_cycle_end(uint64_t start, unsigned waits);
    uint8_t  inta_cycles();

    void push(uint16_t v);
    uint16_t pop();

    //Registers
    uint8_t  get_r8(unsigned i) const { return (i < 4) ? (uint8_t)ctx.r[i] : (uint8_t)(ctx.r[i - 4] >> 8); }
    void     set_r8(unsigned i, uint8_t v);

    //Addressing
    void     decode_modrm();
    uint8_t  get_rm8();
    uint16_t get_rm16();
    void     set_rm8(uint8_t v);
    void     set_rm16(uint16_t v);
    int      seg_or(int def) const { return (m_seg >= 0) ? m_seg : def; }

    //Flags
    bool     flag(uint16_t f) const { return (ctx.flags & f) != 0; }
    void     set_flag(uint16_t f, bool on) { if (on) ctx.flags |= f; else ctx.flags &= ~f; }
    void     set_szp8(uint8_t v);
    void     set_szp16(uint16_t v);
    bool     condition(unsigned cc) const;

    //Arithmetic
    uint8_t  alu8(unsigned op, uint8_t a, uint8_t b);
    uint16_t alu16(unsigned op, uint16_t a, uint16_t b);
    uint8_t  inc8(uint8_t v);
    uint8_t  dec8(uint8_t v);
    uint16_t inc16(uint16_t v);
    uint16_t dec16(uint16_t v);
    uint16_t shift(unsigned op, uint16_t v, unsigned count, bool word);
    void     group3_8();
    void     group3_16();
    bool     div8(bool sign, uint8_t d);
    bool     div16(bool sign, uint16_t d);

    //Control
    void     interrupt(uint8_t vector, bool hardware);
    bool     string_op(uint8_t opcode);
    void     execute_opcode(uint8_t opcode);
    bool     interrupt_pending() const;
    void     take_interrupt_at_boundary();
};
