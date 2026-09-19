// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A vectored interrupt request of the 1801 bus, with its daisy chain

#pragma once

#include "emulator/core.h"

// The request line and the vector a device offers a К1801 processor (~virq,
// ~vector). The processor latches a request, and the vector with it, when the
// line goes active, and drops the latch when it takes the interrupt; a line
// that simply stays active raises nothing new (pdp11core::set_virq). The
// device learns its request was taken from the acknowledge (~iako). So:
//
//   - the vector goes out before the line;
//   - when the source changes (one request taken, the next one waiting) the
//     line is released and pulled again, or the processor would never see the
//     new one;
//   - while the same source stays pending the line is left alone: a handler
//     that does not clear its cause is not called again and again until the
//     stack runs off the bottom of memory.
//
// Devices sharing one line form a chain: each offers its own request first and
// passes on the one coming from further along (~virq_in / ~vector_in, the
// ~virq / ~vector of the next device). chain() picks between the two.
class VirqLine
{
public:
    VirqLine(Interface &virq, Interface &vector): m_virq(virq), m_vector(vector) {}

    // Own vector, or 0 for none, against the one arriving on the chain
    static unsigned int chain(unsigned int own, const Interface &virq_in, const Interface &vector_in)
    {
        if (own != 0) return own;
        return ((virq_in.value & 1) == 0) ? (vector_in.value & 0xFFFF) : 0;
    }

    // Offers a vector, 0 to withdraw the request
    void offer(unsigned int vector)
    {
        if (vector == m_offered) return;
        if (vector != 0) {
            m_vector.change(vector);
            m_virq.change(1);
            m_virq.change(0);
        } else
            m_virq.change(1);
        m_offered = vector;
    }

    unsigned int offered() const { return m_offered; }
    // Forgets the offer without touching the line, for a reset
    void clear() { m_offered = 0; }

private:
    Interface &m_virq;
    Interface &m_vector;
    unsigned int m_offered = 0;
};
