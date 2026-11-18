// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the blitter

#include "azbk_blitter.h"
#include "azbk.h"
#include "emulator/state.h"

static const uint32_t ADDRESS_MASK = 0xFFFFFF;     // 24 разряда адреса слова

void AZBKBlitter::reset()
{
    stop();
    m_count = 0;
    m_manual = false;
}

void AZBKBlitter::stop()
{
    m_busy = false;
    m_reading = false;
    m_cmd_loaded = false;
    m_packet.clear();
    m_packet_words = 0;
    m_cmd = 0;
    m_budget = 0;
}

unsigned int AZBKBlitter::read_control() const
{
    return (m_busy ? 0100000u : 0u) | (m_manual ? 040000u : 0u) | (m_reading ? 01000u : 0u) | m_count;
}

// Запись 177270. Ноль команд - асинхронная остановка. Пока блиттер занят,
// меняется только число команд (для следующего пуска; пока читается пакет -
// и оно нет), а смена режима и пуск не принимаются. Ручной пуск - разряд 12
// при уже стоящем разряде 14 (MAXIOL: «устанавливаем бит 14; следующей
// командой бит 12 и количество команд») или вместе с ним в одной записи
// (плата, AZHWT 2026-11-17: 050001 после 0 пускает)
void AZBKBlitter::write_control(unsigned int value)
{
    const unsigned int count = value & 0777;
    if (count == 0) {
        stop();
        m_count = 0;
        m_manual = (value & 040000) != 0;
        return;
    }
    if (m_busy) {
        if (!m_reading) m_count = count;
        return;
    }
    const bool was_manual = m_manual;
    m_count = count;
    m_manual = (value & 040000) != 0;
    if ((was_manual || m_manual) && (value & 010000) != 0) start();
}

void AZBKBlitter::frame_end()
{
    if (!m_busy && !m_manual && m_count != 0) start();
}

void AZBKBlitter::start()
{
    m_busy = true;
    m_reading = true;
    m_packet.assign(m_count * 8, 0);
    m_packet_words = 0;
    m_cmd = 0;
    m_cmd_loaded = false;
    m_budget = 0;
}

uint16_t AZBKBlitter::read_word(uint32_t address) const
{
    const uint8_t * mem = m_board->memory();
    const uint64_t b = (uint64_t)(address & ADDRESS_MASK) * 2;
    if (mem == nullptr || b + 1 >= m_board->memory_size()) return 0;
    return (uint16_t)(mem[b] | (mem[b + 1] << 8));
}

// keep: разряд 0 - младший байт не пишется, 1 - старший
void AZBKBlitter::write_word(uint32_t address, uint16_t value, unsigned int keep)
{
    uint8_t * mem = m_board->memory();
    const uint64_t b = (uint64_t)(address & ADDRESS_MASK) * 2;
    if (mem == nullptr || b + 1 >= m_board->memory_size()) return;
    if ((keep & 1) == 0) mem[b] = (uint8_t)value;
    if ((keep & 2) == 0) mem[b + 1] = (uint8_t)(value >> 8);
    m_board->touch_page((unsigned int)(b / AZBK::PAGE_BYTES));
}

bool AZBKBlitter::load_command()
{
    const uint16_t * p = &m_packet[m_cmd * 8];
    if (p[3] & 4) return false;                 // NOP
    m_op = p[3];
    m_src = (((uint32_t)p[0] >> 8) << 16) | p[1];
    m_dst = ((((uint32_t)p[0] & 0377) << 16) | p[2]) + ((uint32_t)p[6] << 8);
    m_src &= ADDRESS_MASK;
    m_dst &= ADDRESS_MASK;
    m_width = (p[4] & 0377) + 1;
    // 0 строк - одно слово (плата, AZHWT 2026-11-17, вопрос Б.8): конец
    // команды проверяется после каждого слова. У MAXIOL «максимальное
    // количество строк - 256», у gid 0 - одна строка
    m_rows = (p[4] >> 8) & 0377;
    m_inc = p[5] & 0377;
    m_const = p[7];
    m_col = m_row = 0;
    m_cmd_loaded = true;
    return true;
}

// Одно слово команды по Verilog MAXIOL (STATE_BLT_DATA_PRC): данные и маска
// записи, затем адреса
unsigned int AZBKBlitter::step_word()
{
    unsigned int cost = 0;
    uint16_t s = 0, d = 0;
    if (m_op & 1) { s = read_word(m_src); cost += read_cost; }
    if (m_op & 2) { d = read_word(m_dst); cost += read_cost; }
    const unsigned int c = m_const & 0377, ch = (m_const >> 8) & 0377;
    // Обмен байтов (для зеркала по X) - у того, что пишется
    const bool swap = (m_op & 0100) != 0;
    if (swap) s = (uint16_t)((s >> 8) | (s << 8));
    uint16_t data = 0;
    uint32_t target = m_dst;
    unsigned int keep = 0;
    switch ((m_op >> 3) & 7) {
    case 0:     // заливка константой
        data = (uint16_t)(c | (c << 8));
        break;
    case 1:     // копия SRC в DST
        data = s;
        break;
    case 2:     // наложение: байты SRC, не равные прозрачному SRC
        data = s;
        keep = (((s >> 8) == c) ? 2u : 0u) | (((s & 0377) == c) ? 1u : 0u);
        break;
    case 3:     // то же, и только поверх прозрачного DST
        data = s;
        keep = ((((d >> 8) != ch) || ((s >> 8) == c)) ? 2u : 0u)
             | ((((d & 0377) != ch) || ((s & 0377) == c)) ? 1u : 0u);
        break;
    case 4:     // DST в линейный SRC (сохранение фона)
        data = swap ? (uint16_t)((d >> 8) | (d << 8)) : d;
        target = m_src;
        break;
    case 5:     // константа по контуру: байты SRC, не равные прозрачному (старший байт)
        data = (uint16_t)(c | (c << 8));
        keep = (((s >> 8) == ch) ? 2u : 0u) | (((s & 0377) == ch) ? 1u : 0u);
        break;
    default:    // неизвестная операция ничего не пишет
        keep = 3;
        break;
    }
    if (keep != 3) {
        write_word(target, data, keep);
        cost += write_cost;
    }
    m_words++;

    m_src = (m_src + 1) & ADDRESS_MASK;
    if (++m_col < m_width) {
        m_dst += (m_op & 01000) ? (uint32_t)-1 : 1u;
    } else {
        m_col = 0;
        m_dst += (m_op & 02000) ? (uint32_t)(0 - m_inc) : m_inc;
        m_row++;
    }
    if (m_row >= m_rows) {
        m_cmd_loaded = false;
        m_cmd++;
    }
    m_dst &= ADDRESS_MASK;
    return cost;
}

void AZBKBlitter::run(unsigned int dots)
{
    if (!m_busy) return;
    m_ticks += dots;
    m_budget += (int64_t)dots * 100;
    while (m_busy && m_budget > 0) {
        if (m_reading) {
            m_packet[m_packet_words] = read_word(((uint32_t)m_page << 11) + m_packet_words);
            m_packet_words++;
            m_budget -= read_cost;
            if (m_packet_words >= m_packet.size()) m_reading = false;
            continue;
        }
        if (!m_cmd_loaded) {
            if (m_cmd * 8 >= m_packet.size()) {
                stop();
                break;
            }
            if (!load_command()) {
                m_cmd++;
                continue;
            }
        }
        m_budget -= step_word();
    }
}

void AZBKBlitter::save_state(StateWriter &w) const
{
    w.u("blitter_count", m_count, 16);
    w.b("blitter_manual", m_manual);
    w.u("blitter_page", m_page, 16);
    w.b("blitter_busy", m_busy);
    w.b("blitter_reading", m_reading);
    w.n("blitter_packet_size", (uint32_t)m_packet.size());
    if (!m_packet.empty()) w.array("blitter_packet", m_packet.data(), m_packet.size());
    w.n("blitter_packet_words", m_packet_words);
    w.n("blitter_command", m_cmd);
    w.b("blitter_loaded", m_cmd_loaded);
    w.u("blitter_op", m_op, 16);
    w.n("blitter_src", m_src);
    w.n("blitter_dst", m_dst);
    w.n("blitter_width", m_width);
    w.n("blitter_rows", m_rows);
    w.n("blitter_inc", m_inc);
    w.n("blitter_col", m_col);
    w.n("blitter_row", m_row);
    w.u("blitter_const", m_const, 16);
    w.n64("blitter_budget", (uint64_t)m_budget);
    w.n64("blitter_words", m_words);
    w.n64("blitter_ticks", m_ticks);
}

void AZBKBlitter::load_state(const StateReader &r)
{
    r.u("blitter_count", m_count);
    r.b("blitter_manual", m_manual);
    r.u("blitter_page", m_page);
    r.b("blitter_busy", m_busy);
    r.b("blitter_reading", m_reading);
    unsigned int size = 0;
    r.u("blitter_packet_size", size);
    if (size > 511 * 8) size = 0;
    m_packet.assign(size, 0);
    if (size > 0) r.array("blitter_packet", m_packet.data(), size);
    r.u("blitter_packet_words", m_packet_words);
    if (m_packet_words > size) m_packet_words = size;
    r.u("blitter_command", m_cmd);
    r.b("blitter_loaded", m_cmd_loaded);
    r.u("blitter_op", m_op);
    r.u("blitter_src", m_src);
    r.u("blitter_dst", m_dst);
    r.u("blitter_width", m_width);
    r.u("blitter_rows", m_rows);
    r.u("blitter_inc", m_inc);
    r.u("blitter_col", m_col);
    r.u("blitter_row", m_row);
    r.u("blitter_const", m_const);
    uint64_t budget = 0;
    if (r.n64("blitter_budget", budget)) m_budget = (int64_t)budget;
    r.n64("blitter_words", m_words);
    r.n64("blitter_ticks", m_ticks);
    if (m_busy && m_packet.empty()) stop();
}
