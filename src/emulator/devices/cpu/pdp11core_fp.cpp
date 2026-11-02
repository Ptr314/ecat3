// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: FP11 floating point of the КМ1811ВМ1 (DEC F-11)

#include "pdp11core.h"

// Числа ПЗ DEC: знак, порядок со смещением 128 и мантисса 0,1fff... со скрытой
// старшей единицей, 24 разряда в одинарной точности и 56 в двойной. Внутри
// мантисса держится в 64 разрядах со скрытой единицей в старшем (fp_value),
// округление - прибавлением половины младшего разряда сетки, если не задано
// усечение. Порядок 0 - точный ноль; знак при нём - «минус ноль», неопределённая
// величина. Описание - ТО1 Э-85, раздел 9.
//
// Регистры AC0-AC5 по четыре слова; команды с полем AC достают только AC0-AC3,
// AC4 и AC5 - только как операнд с методом 0.

namespace {
    const uint16_t FPS_FER  = 0100000;      // ошибка
    const uint16_t FPS_FID  = 0040000;      // прерывания запрещены
    const uint16_t FPS_FIUV = 0004000;      // прерывание по неопределённой величине
    const uint16_t FPS_FIU  = 0002000;      // по антипереполнению
    const uint16_t FPS_FIV  = 0001000;      // по переполнению
    const uint16_t FPS_FIC  = 0000400;      // по ошибке преобразования в целое
    const uint16_t FPS_FD   = 0000200;      // двойная точность
    const uint16_t FPS_FL   = 0000100;      // длинное целое
    const uint16_t FPS_FT   = 0000040;      // усечение
    const uint16_t FPS_FN   = 0000010;
    const uint16_t FPS_FZ   = 0000004;
    const uint16_t FPS_FV   = 0000002;
    const uint16_t FPS_FC   = 0000001;
    const uint16_t FPS_MASK = 0147757;

    const uint16_t FEC_OPCODE   = 2;
    const uint16_t FEC_DIV_ZERO = 4;
    const uint16_t FEC_CONVERT  = 6;
    const uint16_t FEC_OVERFLOW = 8;
    const uint16_t FEC_UNDERFLOW = 10;
    const uint16_t FEC_UNDEFINED = 12;

    const uint16_t V_FP = 0244;
    const int EXP_BIAS = 128;
    const uint64_t HIDDEN = (uint64_t)1 << 63;

    // Время команд, такты 6,67 МГц: сложение и умножение из ТО (таблица 2),
    // остальное - по подобию
    const unsigned int C_FP_LOAD   = 160;
    const unsigned int C_FP_SIMPLE = 90;
    const unsigned int C_FP_ADD_F  = 640;
    const unsigned int C_FP_ADD_D  = 1230;
    const unsigned int C_FP_MUL_F  = 750;
    const unsigned int C_FP_MUL_D  = 1870;
    const unsigned int C_FP_DIV_F  = 1100;
    const unsigned int C_FP_DIV_D  = 2600;

    // Старшие 64 разряда произведения: C++11 без __int128 на MinGW 4.9
    uint64_t mul_high(uint64_t a, uint64_t b)
    {
        const uint64_t al = a & 0xFFFFFFFFu, ah = a >> 32;
        const uint64_t bl = b & 0xFFFFFFFFu, bh = b >> 32;
        const uint64_t ll = al * bl;
        const uint64_t lh = al * bh;
        const uint64_t hl = ah * bl;
        const uint64_t hh = ah * bh;
        const uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
        return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    }
}

pdp11core::fp_value pdp11core::fp_unpack(const uint16_t * w) const
{
    fp_value v;
    v.sign = (w[0] & 0100000) != 0;
    v.exp = (w[0] >> 7) & 0377;
    if (v.exp == 0) {
        v.frac = 0;
        return v;
    }
    v.frac = HIDDEN | ((uint64_t)(w[0] & 0177) << 56) | ((uint64_t)w[1] << 40)
           | ((uint64_t)w[2] << 24) | ((uint64_t)w[3] << 8);
    return v;
}

// Накопитель как операнд: при одинарной точности младшие два слова в счёте не
// участвуют, что бы в них ни осталось от прошлых команд
pdp11core::fp_value pdp11core::fp_accumulator(unsigned int ac, bool dbl) const
{
    fp_value v = fp_unpack(m_fac[ac]);
    if (!dbl) v.frac &= ~0xFFFFFFFFFFull;
    return v;
}

// Округление и упаковка. Возвращает код ошибки для fp_raise() или 0: результат
// при этом всё равно записан, если разрешено прерывание, с порядком по модулю
uint16_t pdp11core::fp_pack(fp_value v, uint16_t * w, bool dbl, bool round)
{
    w[0] = w[1] = w[2] = w[3] = 0;
    m_fps &= (uint16_t)~FPS_FV;
    if (v.frac == 0) return 0;

    const uint64_t mask = dbl ? 0xFFu : 0xFFFFFFFFFFull;
    if (round && (m_fps & FPS_FT) == 0) {
        const uint64_t half = (mask >> 1) + 1;
        const uint64_t r = v.frac + half;
        if (r < v.frac) {
            v.frac = HIDDEN;
            v.exp++;
        } else
            v.frac = r;
    }
    v.frac &= ~mask;

    uint16_t error = 0;
    if (v.exp > 0377) {
        m_fps |= FPS_FV;
        error = FEC_OVERFLOW;
        if ((m_fps & FPS_FIV) == 0) return error;
    } else if (v.exp <= 0) {
        error = FEC_UNDERFLOW;
        if ((m_fps & FPS_FIU) == 0) return error;
    }

    w[0] = (uint16_t)((v.sign ? 0100000 : 0) | ((v.exp & 0377) << 7) | ((v.frac >> 56) & 0177));
    w[1] = (uint16_t)(v.frac >> 40);
    w[2] = (uint16_t)(v.frac >> 24);
    w[3] = (uint16_t)(v.frac >> 8);
    return error;
}

void pdp11core::fp_set_cc(const uint16_t * w)
{
    m_fps &= (uint16_t)~(FPS_FN | FPS_FZ | FPS_FC);
    if (w[0] & 0100000) m_fps |= FPS_FN;
    if ((w[0] & 077600) == 0) m_fps |= FPS_FZ;
}

void pdp11core::fp_copy_cc()
{
    context.PSW = (uint16_t)((context.PSW & ~017) | (m_fps & 017));
}

// Ошибка: FER ставится всегда, а регистры адреса и кода и прерывание - только для
// кода команды, деления на ноль и разрешённых условий
void pdp11core::fp_raise(uint16_t code)
{
    m_fps |= FPS_FER;
    bool enabled = true;
    switch (code) {
    case FEC_CONVERT:   enabled = (m_fps & FPS_FIC) != 0; break;
    case FEC_OVERFLOW:  enabled = (m_fps & FPS_FIV) != 0; break;
    case FEC_UNDERFLOW: enabled = (m_fps & FPS_FIU) != 0; break;
    case FEC_UNDEFINED: enabled = (m_fps & FPS_FIUV) != 0; break;
    default: break;
    }
    if (!enabled) return;
    m_fec = code;
    m_fea = (uint16_t)(m_fp_pc);
    if ((m_fps & FPS_FID) == 0) do_trap(V_FP);
}

// Адрес операнда длиной bytes. Метод 27 (непосредственный) всегда одно слово
uint16_t pdp11core::fp_address(unsigned int spec, unsigned int bytes, bool &immediate)
{
    const unsigned int mode = (spec >> 3) & 7;
    const unsigned int reg = spec & 7;
    const uint16_t step = (uint16_t)((reg == PDP11::REG_PC) ? 2 : bytes);
    uint16_t a = 0;
    immediate = false;
    switch (mode) {
    case 1:
        a = context.R[reg];
        break;
    case 2:
        a = context.R[reg];
        context.R[reg] = (uint16_t)(context.R[reg] + step);
        immediate = reg == PDP11::REG_PC;
        break;
    case 3:
        a = read_word_checked(context.R[reg]);
        context.R[reg] = (uint16_t)(context.R[reg] + 2);
        break;
    case 4:
        context.R[reg] = (uint16_t)(context.R[reg] - step);
        a = context.R[reg];
        break;
    case 5:
        context.R[reg] = (uint16_t)(context.R[reg] - 2);
        a = read_word_checked(context.R[reg]);
        break;
    case 6: {
        const uint16_t x = fetch();
        a = (uint16_t)(context.R[reg] + x);
        break;
    }
    default: {
        const uint16_t x = fetch();
        a = read_word_checked((uint16_t)(context.R[reg] + x));
        break;
    }
    }
    return a;
}

// Читает операнд ПЗ без проверки на -0; адрес остаётся для записи на то же место.
// false - команда кончена (авария, ошибка кода)
bool pdp11core::fp_fetch(unsigned int spec, bool dbl, uint16_t * w, uint16_t &address, bool &immediate)
{
    w[0] = w[1] = w[2] = w[3] = 0;
    address = 0;
    immediate = false;
    if ((spec & 070) == 0) {
        const unsigned int reg = spec & 7;
        if (reg > 5) {
            fp_raise(FEC_OPCODE);
            return false;
        }
        const unsigned int n = dbl ? 4 : 2;
        for (unsigned int i = 0; i < n; i++) w[i] = m_fac[reg][i];
        return true;
    }
    address = fp_address(spec, dbl ? 8 : 4, immediate);
    if (m_abort) return false;
    const unsigned int n = immediate ? 1 : (dbl ? 4 : 2);
    for (unsigned int i = 0; i < n; i++) {
        w[i] = read_word_checked((uint16_t)(address + i * 2));
        if (m_abort) return false;
    }
    return true;
}

// Запись по адресу, который дала fp_address() или fp_fetch()
bool pdp11core::fp_store(unsigned int spec, bool dbl, const uint16_t * w, uint16_t address, bool immediate)
{
    if ((spec & 070) == 0) {
        const unsigned int reg = spec & 7;
        if (reg > 5) {
            fp_raise(FEC_OPCODE);
            return false;
        }
        fp_store_ac(reg, w, dbl);
        return true;
    }
    const unsigned int n = immediate ? 1 : (dbl ? 4 : 2);
    for (unsigned int i = 0; i < n; i++) {
        write_word_checked((uint16_t)(address + i * 2), w[i]);
        if (m_abort) return false;
    }
    return true;
}

// Прерывание по -0 разрешено и он считан из памяти: в накопителе -0 его не вызывает
// (ТО1, 9.3). Когда оно приходит, решает команда: LD, ABS, NEG и TST - по-своему
bool pdp11core::fp_undefined(unsigned int spec, const uint16_t * w) const
{
    return (spec & 070) != 0 && (w[0] & 0177600) == 0100000 && (m_fps & FPS_FIUV) != 0;
}

// Читает операнд; false - команда кончена, в том числе прерыванием по -0 перед ней
bool pdp11core::fp_read(unsigned int spec, bool dbl, uint16_t * w)
{
    uint16_t a;
    bool immediate;
    if (!fp_fetch(spec, dbl, w, a, immediate)) return false;
    if (fp_undefined(spec, w)) {
        fp_raise(FEC_UNDEFINED);
        return false;
    }
    return true;
}

bool pdp11core::fp_write(unsigned int spec, bool dbl, const uint16_t * w)
{
    uint16_t a = 0;
    bool immediate = false;
    if ((spec & 070) != 0) {
        a = fp_address(spec, dbl ? 8 : 4, immediate);
        if (m_abort) return false;
    }
    return fp_store(spec, dbl, w, a, immediate);
}

pdp11core::fp_value pdp11core::fp_add(fp_value a, fp_value b)
{
    if (b.frac == 0) return a;
    if (a.frac == 0) return b;
    if (a.exp < b.exp) {
        const fp_value t = a;
        a = b;
        b = t;
    }
    const int shift = a.exp - b.exp;
    const uint64_t small = (shift >= 64) ? 0 : (b.frac >> shift);
    fp_value r;
    r.exp = a.exp;
    if (a.sign == b.sign) {
        r.sign = a.sign;
        r.frac = a.frac + small;
        if (r.frac < a.frac) {
            r.frac = (r.frac >> 1) | HIDDEN;
            r.exp++;
        }
        return r;
    }
    if (a.frac >= small) {
        r.sign = a.sign;
        r.frac = a.frac - small;
    } else {
        r.sign = b.sign;
        r.frac = small - a.frac;
    }
    if (r.frac == 0) {
        r.sign = false;
        r.exp = 0;
        return r;
    }
    while ((r.frac & HIDDEN) == 0) {
        r.frac <<= 1;
        r.exp--;
    }
    return r;
}

pdp11core::fp_value pdp11core::fp_mul(const fp_value &a, const fp_value &b)
{
    fp_value r;
    r.sign = a.sign != b.sign;
    r.exp = 0;
    r.frac = 0;
    if (a.frac == 0 || b.frac == 0) {
        r.sign = false;
        return r;
    }
    r.exp = a.exp + b.exp - EXP_BIAS;
    r.frac = mul_high(a.frac, b.frac);
    if ((r.frac & HIDDEN) == 0) {
        r.frac <<= 1;
        r.exp--;
    }
    return r;
}

pdp11core::fp_value pdp11core::fp_div(const fp_value &a, const fp_value &b)
{
    fp_value r;
    r.sign = a.sign != b.sign;
    r.exp = 0;
    r.frac = 0;
    if (a.frac == 0) {
        r.sign = false;
        return r;
    }
    // Деление столбиком: q - частное с одним целым разрядом
    uint64_t rem = a.frac;
    uint64_t q = 0;
    for (unsigned int i = 0; i < 64; i++) {
        const bool carry = i != 0 && (rem & HIDDEN) != 0;
        if (i != 0) rem <<= 1;
        q <<= 1;
        if (carry || rem >= b.frac) {
            rem -= b.frac;
            q |= 1;
        }
    }
    r.exp = a.exp - b.exp + EXP_BIAS;
    if (q & HIDDEN) {
        r.exp++;
        r.frac = q;
    } else
        r.frac = q << 1;
    return r;
}

void pdp11core::fp_store_ac(unsigned int ac, const uint16_t * w, bool dbl)
{
    const unsigned int n = dbl ? 4 : 2;
    for (unsigned int i = 0; i < n; i++) m_fac[ac][i] = w[i];
}

// Запись операнда-слова или двух слов (длинное целое), как его адресует команда
bool pdp11core::fp_write_integer(unsigned int spec, int32_t value, bool is_long)
{
    if ((spec & 070) == 0) {
        context.R[spec & 7] = (uint16_t)(is_long ? (value >> 16) : value);
        return true;
    }
    bool immediate = false;
    const uint16_t a = fp_address(spec, is_long ? 4 : 2, immediate);
    if (m_abort) return false;
    write_word_checked(a, (uint16_t)(is_long ? (value >> 16) : value));
    if (m_abort) return false;
    if (is_long && !immediate) {
        write_word_checked((uint16_t)(a + 2), (uint16_t)value);
        if (m_abort) return false;
    }
    return true;
}

bool pdp11core::fp_read_integer(unsigned int spec, bool is_long, int32_t &value)
{
    if ((spec & 070) == 0) {
        const uint16_t r = context.R[spec & 7];
        value = is_long ? (int32_t)((uint32_t)r << 16) : (int32_t)(int16_t)r;
        return true;
    }
    bool immediate = false;
    const uint16_t a = fp_address(spec, is_long ? 4 : 2, immediate);
    if (m_abort) return false;
    const uint16_t hi = read_word_checked(a);
    if (m_abort) return false;
    if (!is_long) {
        value = (int32_t)(int16_t)hi;
        return true;
    }
    uint16_t lo = 0;
    if (!immediate) {
        lo = read_word_checked((uint16_t)(a + 2));
        if (m_abort) return false;
    }
    value = (int32_t)(((uint32_t)hi << 16) | lo);
    return true;
}

bool pdp11core::fp_execute(uint16_t ir, unsigned int & cycles)
{
    m_fp_pc = (uint16_t)(context.R[PDP11::REG_PC] - 2);
    const unsigned int group = (ir >> 8) & 017;
    const unsigned int ac = (ir >> 6) & 3;
    const unsigned int spec = ir & 077;
    const bool dbl = (m_fps & FPS_FD) != 0;
    uint16_t src[4], res[4];

    switch (group) {
    case 0:
        cycles += C_FP_SIMPLE;
        switch (ac) {
        case 0:
            switch (spec) {
            case 000: fp_copy_cc(); return true;                        // CFCC
            case 001: m_fps &= (uint16_t)~FPS_FD; return true;          // SETF
            case 002: m_fps &= (uint16_t)~FPS_FL; return true;          // SETI
            case 011: m_fps |= FPS_FD; return true;                     // SETD
            case 012: m_fps |= FPS_FL; return true;                     // SETL
            default:  fp_raise(FEC_OPCODE); return true;
            }
        case 1: {                                                       // LDFPS
            pdp11operand o = decode_operand(spec, false, cycles);
            const uint16_t v = read_operand(o, false);
            if (!m_abort) m_fps = v & FPS_MASK;
            return true;
        }
        case 2: {                                                       // STFPS
            pdp11operand o = decode_operand(spec, false, cycles);
            write_operand(o, false, m_fps);
            return true;
        }
        default: {                                                      // STST
            if ((spec & 070) == 0) {
                context.R[spec & 7] = m_fec;
                return true;
            }
            bool immediate = false;
            const uint16_t a = fp_address(spec, 4, immediate);
            if (m_abort) return true;
            write_word_checked(a, m_fec);
            if (!m_abort && !immediate) write_word_checked((uint16_t)(a + 2), m_fea);
            return true;
        }
        }

    case 1:
        cycles += C_FP_SIMPLE;
        switch (ac) {
        case 0:                                                         // CLRF
            res[0] = res[1] = res[2] = res[3] = 0;
            if (!fp_write(spec, dbl, res)) return true;
            m_fps &= (uint16_t)~(FPS_FN | FPS_FV | FPS_FC);
            m_fps |= FPS_FZ;
            return true;
        case 1: {                                                       // TSTF
            uint16_t a;
            bool immediate;
            if (!fp_fetch(spec, dbl, src, a, immediate)) return true;
            fp_set_cc(src);
            m_fps &= (uint16_t)~FPS_FV;
            if (fp_undefined(spec, src)) fp_raise(FEC_UNDEFINED);       // после исполнения
            return true;
        }
        default: {                                                      // ABSF, NEGF
            // Операнд меняется на месте: адрес считается один раз
            uint16_t a;
            bool immediate;
            if (!fp_fetch(spec, dbl, src, a, immediate)) return true;
            const bool undefined = fp_undefined(spec, src);
            uint16_t word = src[0];
            if ((word & 077600) == 0) word = 0;     // ноль и минус ноль дают ноль
            else if (ac == 2) word &= 077777;
            else word ^= 0100000;
            src[0] = word;
            if (word == 0) src[1] = src[2] = src[3] = 0;
            if (!fp_store(spec, dbl, src, a, immediate)) return true;
            fp_set_cc(src);
            m_fps &= (uint16_t)~FPS_FV;
            if (undefined) fp_raise(FEC_UNDEFINED);                     // после исполнения
            return true;
        }
        }

    case 2:                                                             // MULF
    case 3:                                                             // MODF
    case 4:                                                             // ADDF
    case 6:                                                             // SUBF
    case 9: {                                                           // DIVF
        if (!fp_read(spec, dbl, src)) return true;
        fp_value a = fp_accumulator(ac, dbl);
        fp_value b = fp_unpack(src);
        fp_value r;
        uint16_t error = 0;
        uint16_t whole_error = 0;
        switch (group) {
        case 2:
            r = fp_mul(a, b);
            cycles += dbl ? C_FP_MUL_D : C_FP_MUL_F;
            break;
        case 3: {
            r = fp_mul(a, b);
            cycles += dbl ? C_FP_MUL_D : C_FP_MUL_F;
            const int width = dbl ? 56 : 24;
            fp_value whole = r;
            fp_value part = r;
            const int bits = r.exp - EXP_BIAS;
            if (r.frac == 0 || bits <= 0) {
                whole.frac = 0;
                whole.exp = 0;
            } else if (bits >= width) {
                part.frac = 0;
                part.exp = 0;
            } else {
                whole.frac = r.frac & ~(~(uint64_t)0 >> bits);
                part.frac = r.frac << bits;
                part.exp = EXP_BIAS;
                if (part.frac == 0) part.exp = 0;
                else
                    while ((part.frac & HIDDEN) == 0) {
                        part.frac <<= 1;
                        part.exp--;
                    }
            }
            // Упаковывается всегда: переполнение произведения есть переполнение его
            // целой части, а записывается она только при чётном AC
            uint16_t w[4];
            whole_error = fp_pack(whole, w, dbl, false);
            if ((ac & 1) == 0) fp_store_ac(ac | 1, w, dbl);
            r = part;
            break;
        }
        case 4:
            r = fp_add(a, b);
            cycles += dbl ? C_FP_ADD_D : C_FP_ADD_F;
            break;
        case 6:
            b.sign = !b.sign;
            r = fp_add(a, b);
            cycles += dbl ? C_FP_ADD_D : C_FP_ADD_F;
            break;
        default:
            if (b.frac == 0) {
                fp_raise(FEC_DIV_ZERO);
                return true;
            }
            r = fp_div(a, b);
            cycles += dbl ? C_FP_DIV_D : C_FP_DIV_F;
            break;
        }
        error = fp_pack(r, res, dbl, true);
        if (whole_error != 0) {                 // упаковка дробной части сняла VE
            m_fps |= FPS_FV;
            error = whole_error;
        }
        fp_store_ac(ac, res, dbl);
        fp_set_cc(res);
        if (error != 0) fp_raise(error);
        return true;
    }

    case 5: {                                                           // LDF
        cycles += C_FP_LOAD;
        uint16_t a;
        bool immediate;
        if (!fp_fetch(spec, dbl, src, a, immediate)) return true;
        // Признаки показывают выборку и при -0, прерывание приходит до загрузки AC
        fp_set_cc(src);
        m_fps &= (uint16_t)~FPS_FV;
        if (fp_undefined(spec, src)) {
            fp_raise(FEC_UNDEFINED);
            return true;
        }
        fp_store_ac(ac, src, dbl);
        return true;
    }

    case 7: {                                                           // CMPF
        cycles += C_FP_LOAD;
        if (!fp_read(spec, dbl, src)) return true;
        fp_value a = fp_unpack(src);
        fp_value b = fp_accumulator(ac, dbl);
        b.sign = !b.sign;
        const fp_value d = fp_add(a, b);
        m_fps &= (uint16_t)~(FPS_FN | FPS_FZ | FPS_FV | FPS_FC);
        if (d.frac == 0) m_fps |= FPS_FZ;
        else if (d.sign) m_fps |= FPS_FN;
        return true;
    }

    case 8:                                                             // STF
        cycles += C_FP_LOAD;
        fp_write(spec, dbl, m_fac[ac]);
        return true;

    case 10: {                                                          // STEXP
        cycles += C_FP_SIMPLE;
        const int e = (int)((m_fac[ac][0] >> 7) & 0377) - EXP_BIAS;
        pdp11operand o = decode_operand(spec, false, cycles);
        write_operand(o, false, (uint16_t)e);
        if (m_abort) return true;
        m_fps &= (uint16_t)~(FPS_FN | FPS_FZ | FPS_FV | FPS_FC);
        if (e < 0) m_fps |= FPS_FN;
        if (e == 0) m_fps |= FPS_FZ;
        fp_copy_cc();
        return true;
    }

    case 11: {                                                          // STCFI
        cycles += C_FP_LOAD;
        const fp_value v = fp_accumulator(ac, dbl);
        const bool is_long = (m_fps & FPS_FL) != 0;
        int64_t i = 0;
        bool bad = false;
        const int bits = v.exp - EXP_BIAS;
        if (v.frac != 0 && bits > 0) {
            if (bits >= 33) bad = true;
            else {
                const int64_t m = (int64_t)(v.frac >> (64 - bits));
                i = v.sign ? -m : m;
            }
        }
        const int64_t limit = is_long ? ((int64_t)1 << 31) : ((int64_t)1 << 15);
        if (i >= limit || i < -limit) bad = true;
        if (bad) i = 0;
        if (!fp_write_integer(spec, (int32_t)i, is_long)) return true;
        m_fps &= (uint16_t)~(FPS_FN | FPS_FZ | FPS_FV | FPS_FC);
        if (i < 0) m_fps |= FPS_FN;
        if (i == 0) m_fps |= FPS_FZ;
        if (bad) m_fps |= FPS_FC;
        fp_copy_cc();
        if (bad) fp_raise(FEC_CONVERT);
        return true;
    }

    case 12: {                                                          // STCFD
        cycles += C_FP_LOAD;
        const uint16_t error = fp_pack(fp_accumulator(ac, dbl), res, !dbl, true);
        if (!fp_write(spec, !dbl, res)) return true;
        fp_set_cc(res);
        if (error != 0) fp_raise(error);
        return true;
    }

    case 13: {                                                          // LDEXP
        cycles += C_FP_SIMPLE;
        pdp11operand o = decode_operand(spec, false, cycles);
        const int e = (int16_t)read_operand(o, false);
        if (m_abort) return true;
        // Меняется только поле порядка, что бы ни лежало в регистре: ошибку
        // определяет один операнд. Ноль получает скрытую единицу от нового порядка
        uint16_t error = 0;
        bool to_zero = false;
        m_fps &= (uint16_t)~FPS_FV;
        if (e > 0177) {
            m_fps |= FPS_FV;
            error = FEC_OVERFLOW;
            to_zero = (m_fps & FPS_FIV) == 0;
        } else if (e < -0177) {
            error = FEC_UNDERFLOW;
            to_zero = (m_fps & FPS_FIU) == 0;
        }
        uint16_t * r = m_fac[ac];
        if (to_zero) r[0] = r[1] = r[2] = r[3] = 0;
        else r[0] = (uint16_t)((r[0] & 0100177) | (((e + EXP_BIAS) & 0377) << 7));
        fp_set_cc(r);
        if (error != 0) fp_raise(error);
        return true;
    }

    case 14: {                                                          // LDCIF
        cycles += C_FP_LOAD;
        int32_t i = 0;
        if (!fp_read_integer(spec, (m_fps & FPS_FL) != 0, i)) return true;
        fp_value v;
        v.sign = i < 0;
        v.exp = 0;
        v.frac = 0;
        if (i != 0) {
            const uint64_t m = (uint64_t)(i < 0 ? -(int64_t)i : (int64_t)i);
            v.frac = m << 32;
            v.exp = EXP_BIAS + 32;
            while ((v.frac & HIDDEN) == 0) {
                v.frac <<= 1;
                v.exp--;
            }
        }
        const uint16_t error = fp_pack(v, res, dbl, true);
        fp_store_ac(ac, res, dbl);
        fp_set_cc(res);
        if (error != 0) fp_raise(error);
        return true;
    }

    default: {                                                          // LDCDF
        cycles += C_FP_LOAD;
        if (!fp_read(spec, !dbl, src)) return true;
        const uint16_t error = fp_pack(fp_unpack(src), res, dbl, true);
        fp_store_ac(ac, res, dbl);
        fp_set_cc(res);
        if (error != 0) fp_raise(error);
        return true;
    }
    }
}

void pdp11core::get_fp(uint16_t * out) const
{
    unsigned int n = 0;
    for (unsigned int i = 0; i < 6; i++)
        for (unsigned int k = 0; k < 4; k++) out[n++] = m_fac[i][k];
    out[n++] = m_fps;
    out[n++] = m_fec;
    out[n++] = m_fea;
}

void pdp11core::set_fp(const uint16_t * in)
{
    unsigned int n = 0;
    for (unsigned int i = 0; i < 6; i++)
        for (unsigned int k = 0; k < 4; k++) m_fac[i][k] = in[n++];
    m_fps = in[n++];
    m_fec = in[n++];
    m_fea = in[n++];
}
