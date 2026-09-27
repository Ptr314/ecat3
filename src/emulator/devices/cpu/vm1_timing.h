// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: bus cycle timing of the К1801ВМ1 (timing = vm1)

#pragma once

#include <cstdint>

struct BusReply;

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
// Time is absolute, in twelfths of a processor clock, so that the 037 cycle is
// a whole number of ticks at 3, 4 and 6 MHz (48, 64, 96).
class Vm1BusTiming
{
public:
    enum Kind : uint8_t { K_READ = 0, K_WRITE = 1, K_RMW = 2 };
    // R_NONE: a cycle the emulator performs without a memory access (IAKO)
    enum Role : uint8_t { R_DATA = 0, R_STREAM = 1, R_NONE = 2 };
    struct Step {
        uint8_t kind;       // Kind
        uint8_t role;       // Role
        uint8_t gap;        // half clocks from the previous release to SYNC
        uint8_t strobe;     // half clocks from SYNC to DIN or DOUT
        uint8_t r2w;        // K_RMW: half clocks from the read release to DOUT
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

    static const int TICKS_PER_CLOCK = 12;
    static const int TICKS_PER_HALF = 6;

    explicit Vm1BusTiming(unsigned int clock_hz);

    // One bus access as the emulator performed it. Whether a read and the
    // write after it are one read-modify-write cycle is the template's to say:
    // MOV (R1),@0(R1) reads a pointer from where it then writes, in two cycles
    void record(unsigned int address, bool write, bool stream, const BusReply * reply)
    {
        if (m_count < MAX_ACCESSES) {
            Access & a = m_acc[m_count++];
            a.address = address;
            a.kind = write ? K_WRITE : K_READ;
            a.role = stream ? R_STREAM : R_DATA;
            a.reply = reply;
        }
    }
    void discard() { m_count = 0; }

    // Times an executed form: the accesses recorded since the last finish(),
    // and the memory the next opcode will be prefetched from. skip_opcode
    // drops the first instruction stream access, the opcode fetch, which the
    // previous template already placed as its prefetch. Returns the processor
    // clocks elapsed, or -1 when the table has no template for the form (the
    // caller falls back to its own count)
    int finish(unsigned int form, const BusReply * prefetch_reply, bool skip_opcode);

    // Time that passes without the bus: WAIT, a DMA hold, a processor held in
    // reset. Moves the chain along so no step is placed in the past
    void idle(unsigned int clocks);

    // taken: the condition of a branch or SOB held. The chip spends the same
    // on a branch taken and not taken, and on SOB looping and falling out:
    // the table has one template for both, and taken changes nothing today
    static unsigned int form_of(uint16_t command, bool taken);

    // The template of a form, for the self test: its steps, 0 when the table
    // has none
    static unsigned int steps_of(unsigned int form, const Step ** steps);

    // After m_start was set from outside (a restored snapshot)
    void state_restored() { m_phase_window = 0; m_count = 0; }

    // Saved state: the phase of the chain against the 037 cycle
    uint64_t m_now = 0;         // ticks already returned as clocks
    uint64_t m_start = 0;       // when the next instruction may start

private:
    enum { MAX_ACCESSES = 16 };
    struct Access {
        unsigned int address;
        uint8_t kind;
        uint8_t role;
        const BusReply * reply;
    };
    Access m_acc[MAX_ACCESSES];
    unsigned int m_count = 0;

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

    // m_start modulo the window of the memory that answered last: the one
    // 64-bit division an instruction would otherwise need per slow cycle
    uint32_t m_phase_window = 0;    // 0: not known, work it out
    uint32_t m_phase = 0;
    uint32_t phase_of_start(uint32_t window);
};
