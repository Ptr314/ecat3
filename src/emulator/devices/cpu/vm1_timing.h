// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: bus cycle timing of the К1801ВМ1 and К1801ВМ2 (timing = vm1, vm2)

#pragma once

#include <cstdint>

struct BusReply;
class WaitSource;

// The 1801VM1 runs one bus cycle at a time, and what it does between two of
// them is fixed by its microprogram: the next SYNC comes a fixed number of half
// clocks after the previous strobe was released, whatever the memory. A
// simulation of the reverse engineered chip (github 1801BM1/cpu11) gives these
// gaps for every instruction form, tools/bk-timing turns them into
// vm1_timing_table.inc. What the memory adds is the moment it replies: a
// static memory at once, the dynamic RAM of a БК only at the next point of the
// 1801ВП1-037 cycle it was asked early enough for (BusReply).
//
// A template covers everything after the instruction's own opcode fetch up to
// and including the prefetch of the next opcode. Some forms keep the
// microprogram busy after that prefetch: the next instruction starts that many
// half clocks (the form's tail) after the prefetch is released. It is added,
// not overlapped - a prefetch that waited for the 037 does not hide it, which
// is what makes a register instruction three windows long on a БК0011М.
//
// The 1801VM2 (timing = vm2, tools/vm2-timing, vm2_timing_table.inc) is timed
// the same way, with three differences. It fetches the word after its opcode
// first and only then goes for its data, so a template runs from that fetch up
// to the same fetch of the next instruction, and the fetches beyond the
// instruction's own words (the next opcode, a speculative one, a jump target)
// are steps of their own role, R_PREFETCH, wherever they fall. It divides CLC
// by two, so its table is in CLC clocks and it notices RPLY only on every
// other one, the edges its strobes come on. And a memory may reply through a
// WaitSource (the КЦГД video memory) instead of a BusReply.
//
// Time is absolute, in twelfths of a processor clock, so that the 037 cycle is
// a whole number of ticks at 3, 4 and 6 MHz (48, 64, 96).
class Vm1BusTiming
{
public:
    enum Chip : uint8_t { CHIP_VM1 = 0, CHIP_VM2 = 1 };
    enum Kind : uint8_t { K_READ = 0, K_WRITE = 1, K_RMW = 2 };
    // R_NONE: a cycle the emulator performs without a memory access (IAKO);
    // R_PREFETCH: ВМ2, a fetch beyond the instruction's own words
    enum Role : uint8_t { R_DATA = 0, R_STREAM = 1, R_NONE = 2, R_PREFETCH = 3 };
    struct Step {
        uint8_t kind;       // Kind
        uint8_t role;       // Role
        uint8_t gap;        // table units from the previous release to SYNC
        uint8_t strobe;     // table units from SYNC to DIN or DOUT
        uint8_t r2w;        // K_RMW: table units from the read release to DOUT
    };

    // Special forms, after the ones decoded from an opcode (see form_of())
    enum {
        F_DOUBLE = 0,                       // 12 operations x 16 x 16 operand kinds
        F_SINGLE = F_DOUBLE + 12 * 256,     // 28 operations x 16
        F_XOR    = F_SINGLE + 28 * 16,
        F_JMP    = F_XOR + 16,
        F_JSR    = F_JMP + 16,
        F_RTS    = F_JSR + 16,
        F_RTI, F_RTT, F_TRAP, F_MARK,
        F_CC_CLEAR,                         // NOP and CLx
        F_CC_SET,                           // SEx: the microprogram runs on after the prefetch
        F_BR_TAKEN, F_BR_NOT, F_SOB_TAKEN, F_SOB_NOT,
        F_INTERRUPT,                        // IRQ2, IRQ3, a trap to a fixed vector
        F_INTERRUPT_VIRQ,                   // with the IAKO cycle for the vector
        F_WAIT, F_RESET,
        F_COUNT
    };
    // The forms of the ВМ2 (form2_of()): the same, with MUL, DIV, ASH, ASHC
    // and a DIV that overflowed or divided by zero, 16 operand kinds each
    enum {
        F2_DOUBLE = 0,
        F2_SINGLE = F2_DOUBLE + 12 * 256,
        F2_EIS    = F2_SINGLE + 28 * 16,
        F2_XOR    = F2_EIS + 5 * 16,
        F2_JMP    = F2_XOR + 16,
        F2_JSR    = F2_JMP + 16,
        F2_RTS    = F2_JSR + 16,
        F2_RTI, F2_RTT, F2_TRAP, F2_MARK, F2_CC_CLEAR, F2_CC_SET,
        F2_BR_TAKEN, F2_BR_NOT, F2_SOB_TAKEN, F2_SOB_NOT,
        F2_INTERRUPT, F2_INTERRUPT_VIRQ, F2_WAIT, F2_RESET,
        F2_COUNT
    };
    // ВМ2: CLC clocks each step of an ASH or ASHC shift adds to the tail
    static const int VM2_SHIFT_CLOCKS = 4;

    static const int TICKS_PER_CLOCK = 12;
    static const int TICKS_PER_HALF = 6;

    explicit Vm1BusTiming(unsigned int clock_hz, Chip chip = CHIP_VM1);

    // One bus access as the emulator performed it. Whether a read and the
    // write after it are one read-modify-write cycle is the template's to say:
    // MOV (R1),@0(R1) reads a pointer from where it then writes, in two cycles
    void record(unsigned int address, bool write, bool stream, const BusReply * reply, WaitSource * wait = nullptr)
    {
        if (m_count < MAX_ACCESSES) {
            Access & a = m_acc[m_count++];
            a.address = address;
            a.kind = write ? K_WRITE : K_READ;
            a.role = stream ? R_STREAM : R_DATA;
            a.reply = reply;
            a.wait = wait;
        }
    }
    void discard() { m_count = 0; }

    // Times an executed form: the accesses recorded since the last finish(),
    // and the memory the next opcode will be prefetched from (at
    // prefetch_address). skip_opcode drops the first instruction stream
    // access, the opcode fetch, which the previous template already placed as
    // its prefetch. extra_tail: table units the form runs on beyond its
    // template (the steps of an ASH). Returns the processor clocks elapsed,
    // or -1 when the table has no template for the form (the caller falls
    // back to its own count)
    int finish(unsigned int form, const BusReply * prefetch_reply, bool skip_opcode,
               WaitSource * prefetch_wait = nullptr, unsigned int prefetch_address = 0,
               unsigned int extra_tail = 0);

    // Time that passes without the bus: WAIT, a DMA hold, a processor held in
    // reset. Moves the chain along so no step is placed in the past
    void idle(unsigned int clocks);

    // taken: the condition of a branch or SOB held. The chip spends the same
    // on a branch taken and not taken, and on SOB looping and falling out:
    // the table has one template for both, and taken changes nothing today
    static unsigned int form_of(uint16_t command, bool taken);
    // The ВМ2 form; div_v: a DIV that ended with V set
    static unsigned int form2_of(uint16_t command, bool taken, bool div_v);

    // The template of a form, for the self test: its steps, 0 when the table
    // has none
    static unsigned int steps_of(unsigned int form, const Step ** steps);
    static unsigned int steps2_of(unsigned int form, const Step ** steps);
    static unsigned int tail2_of(unsigned int form);

    // After m_start was set from outside (a restored snapshot)
    void state_restored() { m_phase_window = 0; m_count = 0; }

    // Saved state: the phase of the chain against the 037 cycle
    uint64_t m_now = 0;         // ticks already returned as clocks
    uint64_t m_start = 0;       // when the next instruction may start (ВМ2: the last release)
    uint64_t m_s0 = 0;          // ВМ2: where the next instruction's microprogram starts
    uint8_t m_kind2 = 0;        // ВМ2: the kind of the last cycle released
    uint8_t m_flags2 = 0;       // ВМ2: F2_* of the last form
    enum { F2_LAST_DATA_READ = 1, F2_PREV_DATA_FREE = 2 };

private:
    enum { MAX_ACCESSES = 16 };
    struct Access {
        unsigned int address;
        uint8_t kind;
        uint8_t role;
        const BusReply * reply;
        WaitSource * wait;
    };
    Access m_acc[MAX_ACCESSES];
    unsigned int m_count = 0;

    Chip m_chip;

    // BusReply in ticks of this processor, converted on first use
    struct ReplyTicks {
        const BusReply * reply = nullptr;
        uint32_t window, setup_read, setup_write, setup_rmw, delay, rmw_extra;
    };
    ReplyTicks m_conv[4];
    unsigned int m_clock_hz;
    const ReplyTicks * ticks_of(const BusReply * reply);

    // Moment the processor sees RPLY for a strobe at t, both counted from
    // m_start: a whole instruction fits 32 bits easily
    uint32_t reply_time(uint32_t strobe, uint8_t kind, bool rmw_write, const BusReply * reply);
    // ВМ2: release of the strobe at t, counted from m_start
    uint32_t release_vm2(uint32_t strobe, uint8_t kind, const BusReply * reply, WaitSource * wait,
                         unsigned int address);
    // ВМ2: m_start on the edges the chip releases its strobes on
    void align_vm2();

    // m_start modulo the window of the memory that answered last: the one
    // 64-bit division an instruction would otherwise need per slow cycle
    uint32_t m_phase_window = 0;    // 0: not known, work it out
    uint32_t m_phase = 0;
    uint32_t phase_of_start(uint32_t window);
};
