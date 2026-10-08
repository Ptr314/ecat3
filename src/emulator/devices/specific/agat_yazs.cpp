// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat sound synthesis card 5/2 (ЯЗС 5/2)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "agat_yazs.h"
#include "emulator/utils.h"

//------------------- Circuit ----------------------------------------------//
//
// Component names are those of the schematic. Voltages are in volts, from the
// ground of the card; the mixer output is taken from its bias of +8 V.

namespace {

const double VCC        = 5.0;
const double V_LATCH_HI = 4.6;      // К555ТМ7 output with its 2K2 pull-up
const double V_LATCH_LO = 0.15;
const double V_T_HI     = 3.8;      // 580ВИ53 output
const double V_T_LO     = 0.2;
const double VBE        = 0.65;
const double VD         = 0.6;      // КД521А forward voltage
const double G_ON       = 1.0 / 20; // conducting diode or saturated transistor
const double G_OFF      = 1e-9;
const double R_SW       = 300;      // К561КТ3 key

// Tone channel (one of five identical ones)
// The envelope former does not behave by the schematic either: the burst of
// D4 died in 0.2 s where the card's fades evenly for 0.5 s and more, the
// level D5 holds was 7 dB under the card's, and a note ran down to silence
// between two writes the card plays legato. These values are fitted to the
// same recording - all 270 notes of the test, with the drums for the balance
// - and are an equivalent of the card, not its parts: other sets fit nearly
// as well (R23 at 120K with C3 15 uF, R17 20K, R21 9.1K, for one)
const double R16 = 620, R22 = 68e3;
const double R17 = 270e3;           // 150K
const double R21 = 100e3;           // 24K
const double R23 = 2.7e6;           // 120K
const double R24 = 150e3, R26 = 150e3;
const double R25 = 180e3;           // 150K
const double C3 = 1.8e-6;           // 5 uF
const double C4 = 47e-9, C5 = 3.3e-9, C6 = 0.1e-6;
const double V_NEG = -12.0;
const double R18 = 15e3, R19 = 47e3, C2 = 22e-9;
// The key VT1 conducts once the voltage behind C2 lifts its base to VBE
const double X_ON = VBE * (R18 + R19) / R19;

enum { NB = 0, NC, NF, NG, NH, NJ };

// Buses, band filter and mixer
const double R_BUS = 24e3;          // R28-R30 of each channel
const double R127 = 10e3, R129 = 10e3, R130 = 10e3, C48 = 10e-6;
const double C43 = 33e-9, C46 = 3.3e-9, R131 = 68e3;
const double C44 = 22e-9, R128 = 33e3, C49 = 15e-9, R132 = 33e3;
const double C45 = 3.3e-9, R126 = 24e3, C47 = 3.3e-9, R133 = 47e3;
const double C50 = 1e-6, R136 = 100e3, C53 = 150e-12;
// К157УД2 at +12 V, its non-inverting input at +8 V
const double VO_MAX = 2.5, VO_MIN = -6.5;
// DA1.2, the all-pass of the LEFT output
const double R137 = 47e3, C55 = 22e-9;

enum { MH = 0, LH, HH, LM, HM, N48, SND, MC, HC };

// The drums do not sound by the values of the schematic: drum 7 would buzz
// at 100 Hz where the card hisses at 6-13 kHz. Their values below are fitted
// to a recording of a real card (the automatic mode of SOUNDTEST by
// avivanov76, forum.agatcomp.ru topic 515), with the schematic's in comments:
// pitch, envelope and level of all 24 sounds of the test. On that card D0 of
// drum 6 changed nothing - each pitch sounded twice, D0 = 0 and 1 alike - but
// on the card of the NCL demo recording (DemoNCL/01-Hangs up) it does: the
// demo plays drum 6 with D0 set, and that card's drum sounds at 120-140 Hz,
// which D0 and R106 give. The first card had the key of R106 broken
//
// Drum 6
const double C27 = 6.8e-6;          // 5 uF
const double R97 = 33e3, C29 = 1e-6, R98 = 510e3, R100 = 15e3;
const double C65 = 0.15e-6, R103 = 33e3;
const double C30 = 15e-9;           // 22 nF
const double C31 = 20e-9;           // 22 nF
const double R101 = 680e3, R102 = 680e3;
const double R105 = 180e3;          // 150K
const double R106 = 390e3;          // 680K
const double R107 = 360e3;
const double R96 = 68e3, C28 = 10e-9, R99 = 47e3;
// The output resistors of both drums are twice the schematic's: by it the
// drums came out 8 dB too loud against the tones
const double R112 = 47e3, C33 = 47e-9, C35 = 0.1e-6;
const double R115 = 91e3;           // 47K
const double C32 = 0.1e-6, R110 = 240e3, R111 = 10e3, C34 = 15e-9;
const double R113 = 6.8e3;          // 3K3
const double R114 = 130e3;          // 68K
const double C36 = 33e-9;
// Drum 7
const double C37 = 10e-6;           // 5 uF
const double R117 = 67e3, C38 = 1e-6, R118 = 10e3, R119 = 24e3;
const double C39 = 390e-12;         // 22 nF
const double C40 = 270e-12;         // 22 nF
const double C41 = 560e-12;         // 1 nF
const double R121 = 330e3, R122 = 330e3, R124 = 510e3;
const double R150 = 150e3, R123 = 2.2e3, C42 = 330e-12;
const double R125 = 390e3;          // 240K
// VD19 (КД521А) as an exponential diode: by the threshold alone it stops
// chopping the envelope once that falls under VD, and the drum broke off
// 20 dB above where the card's sound fades
const double VD_IS = 2.5e-9, VD_NVT = 1.75 * 0.026;
// Noise: R14 and C1 after the shift register
const double TAU_NOISE = 47e3 * 330e-12;

double par(double a, double b) { return a * b / (a + b); }

// Conductance g between nodes a and b of an n x n matrix; -1 is the ground
void stamp(double *m, int n, int a, int b, double g)
{
    if (a >= 0) m[a * n + a] += g;
    if (b >= 0) m[b * n + b] += g;
    if (a >= 0 && b >= 0) {
        m[a * n + b] -= g;
        m[b * n + a] -= g;
    }
}

// Gauss-Jordan with partial pivoting; the matrices here are never singular
void invert(const double *a, int n, double *inv)
{
    std::vector<double> m(a, a + n * n);
    for (int i = 0; i < n * n; i++) inv[i] = 0;
    for (int i = 0; i < n; i++) inv[i * n + i] = 1;
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (std::fabs(m[r * n + c]) > std::fabs(m[p * n + c])) p = r;
        if (p != c)
            for (int k = 0; k < n; k++) {
                std::swap(m[p * n + k], m[c * n + k]);
                std::swap(inv[p * n + k], inv[c * n + k]);
            }
        const double d = 1.0 / m[c * n + c];
        for (int k = 0; k < n; k++) {
            m[c * n + k] *= d;
            inv[c * n + k] *= d;
        }
        for (int r = 0; r < n; r++) {
            if (r == c) continue;
            const double f = m[r * n + c];
            if (f == 0) continue;
            for (int k = 0; k < n; k++) {
                m[r * n + k] -= f * m[c * n + k];
                inv[r * n + k] -= f * inv[c * n + k];
            }
        }
    }
}

void mul(const double *inv, int n, const double *rhs, double *x)
{
    for (int r = 0; r < n; r++) {
        double s = 0;
        const double *row = inv + r * n;
        for (int k = 0; k < n; k++) s += row[k] * rhs[k];
        x[r] = s;
    }
}

// A 2x2 system
void solve2(double a, double b, double c, double d, double e, double f, double &x, double &y)
{
    const double det = a * d - b * c;
    x = (e * d - b * f) / det;
    y = (a * f - c * e) / det;
}

// The root t of a * (exp(t) - 1) + c * t = y, with a, c > 0. The left side is
// convex and increasing, and Newton started right of the root comes down to
// it without overshooting
double exp_root(double a, double c, double y)
{
    double t = (y > 0) ? std::log1p(y / a) : 0.0;
    for (int it = 0; it < 30; it++) {
        const double e = std::exp(t);
        const double d = (a * (e - 1) + c * t - y) / (a * e + c);
        t -= d;
        if (std::fabs(d) < 1e-9) break;
    }
    return t;
}

// Two cross-coupled CMOS inverters (К561ЛН2), each fed back by its own
// resistor, the other's output coupled to its input by a capacitor. The
// inputs are clamped by the protection diodes when an output jumps
void flip_pair(bool &oa, bool &ob, double &ca, double &cb)
{
    // ca: capacitor from the output of B to the input of A, cb the other way
    for (int it = 0; it < 4; it++) {
        const double Oa = oa ? VCC : 0, Ob = ob ? VCC : 0;
        const bool na = (Ob - ca) < VCC / 2;
        const bool nb = (Oa - cb) < VCC / 2;
        if (na == oa && nb == ob) return;
        oa = na;
        ob = nb;
        const double Oa2 = oa ? VCC : 0, Ob2 = ob ? VCC : 0;
        const double ia = Ob2 - ca, ib = Oa2 - cb;
        if (ia > VCC + VD) ca = Ob2 - (VCC + VD); else if (ia < -VD) ca = Ob2 + VD;
        if (ib > VCC + VD) cb = Oa2 - (VCC + VD); else if (ib < -VD) cb = Oa2 + VD;
    }
}

}   // namespace

//------------------- 580ВИ53 counter --------------------------------------//

void Vi53Counter::control(unsigned int cw)
{
    const unsigned int r = (cw >> 4) & 3;
    if (r == 0) return;                     // latch: nothing reads it here
    rw = (uint8_t)r;
    mode = (uint8_t)((cw >> 1) & 7);
    if (mode > 5) mode -= 4;                // 6 and 7 are 2 and 3
    bcd = (cw & 1) != 0;
    msb_next = false;
    has_pending = false;
    state = IDLE;
    phase = 0;
    out = (mode != 0);
}

void Vi53Counter::write(unsigned int value)
{
    uint32_t v;
    value &= 0xFF;
    if (rw == 1)
        v = value;
    else if (rw == 2)
        v = value << 8;
    else {
        if (!msb_next) {
            lsb = (uint8_t)value;
            msb_next = true;
            // Mode 0 stops on the first byte and drops its output
            if (mode == 0) {
                out = false;
                state = IDLE;
            }
            return;
        }
        msb_next = false;
        v = lsb | (value << 8);
    }
    uint32_t count;
    if (bcd) {
        count = ((v >> 12) & 15) * 1000 + ((v >> 8) & 15) * 100 + ((v >> 4) & 15) * 10 + (v & 15);
        if (count == 0) count = 10000;
    } else
        count = v ? v : 65536;

    switch (mode) {
    case 0:
    case 4:
        n = count;
        out = (mode == 4);
        phase = 0;
        state = LOAD;
        break;
    case 1:
    case 5:
        n = count;
        if (state == IDLE) state = WAIT;
        break;
    default:
        if (state == IDLE || state == PAUSED) {
            // GATE reloads the count written last, not one left pending
            n = count;
            has_pending = false;
            state = gate ? LOAD : PAUSED;
        } else {
            // Taken at the end of the current period or half-period
            pending_n = count;
            has_pending = true;
        }
        break;
    }
}

void Vi53Counter::set_gate(bool g)
{
    if (g == gate) return;
    gate = g;
    switch (mode) {
    case 2:
    case 3:
        if (!g) {
            out = true;
            phase = 0;
            if (state == RUN || state == LOAD) state = PAUSED;
        } else if (state == PAUSED)
            state = LOAD;
        break;
    case 1:
    case 5:
        if (g && state != IDLE) state = LOAD;
        break;
    default:
        break;
    }
}

uint32_t Vi53Counter::until_event() const
{
    if ((mode == 0 || mode == 4) && !gate) return NEVER;
    if (state == LOAD) return 1;
    if (state == RUN) return left;
    return NEVER;
}

void Vi53Counter::advance(uint32_t k)
{
    if (k == 0) return;
    const uint32_t e = until_event();
    if (e == NEVER) return;
    if (k < e) {
        left -= k;      // only RUN has e > 1
        return;
    }
    if (state == LOAD) load(); else event();
}

void Vi53Counter::load()
{
    if (has_pending) {
        n = pending_n;
        has_pending = false;
    }
    phase = 0;
    state = RUN;
    switch (mode) {
    case 0:
    case 1:
        out = false;
        left = n;
        break;
    case 2:
        out = true;
        left = (n > 1) ? n - 1 : 1;
        break;
    case 3:
        out = true;
        left = (n + 1) / 2;
        break;
    default:
        out = true;
        left = n;
        break;
    }
}

void Vi53Counter::event()
{
    switch (mode) {
    case 0:
    case 1:
        out = true;
        state = DONE;
        break;
    case 2:
        if (phase == 0) {
            out = false;
            phase = 1;
            left = 1;
        } else {
            out = true;
            phase = 0;
            if (has_pending) {
                n = pending_n;
                has_pending = false;
            }
            left = (n > 1) ? n - 1 : 1;
        }
        break;
    case 3:
        if (has_pending) {
            n = pending_n;
            has_pending = false;
        }
        if (n < 2) {
            out = true;         // a count of 1 never goes low
            left = 65536;
        } else {
            out = !out;
            left = out ? (n + 1) / 2 : n / 2;
        }
        break;
    default:
        if (phase == 0) {
            out = false;
            phase = 1;
            left = 1;
        } else {
            out = true;
            phase = 0;
            state = DONE;
        }
        break;
    }
}

//------------------- Device -----------------------------------------------//

AgatYazs::AgatYazs(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irq_in(this, im, 1, "irq_in", MODE_R, 1)
    , i_irq(this, im, 1, "irq", MODE_W)
    , i_ext(this, im, 1, "ext", MODE_R, 2)
    // reset() reads m_cet to decide whether CET is rising
    , m_cet(true)
    , m_ext_en(true)
    , m_flag_t(false)
    , m_flag_e(false)
    , m_port_value(0)
    , m_substep(8)
    , m_h(0)
    , m_tick_on(0)
    , m_tick_off(0)
    , m_gain(1 / 1.5)
{
    // Read here and not in load_config(): the mixer asks sound_stereo() in its
    // own load_config(), which may come first
    // Mono by default: summed by a mono speaker or a mono downmix, the two
    // outputs cancel the treble - the tones lose 8-16 dB, drum 7 nearly all
    m_stereo = read_confg_value(cd, "stereo", false, false);
    m_clocked = true;
    addresable_size = 16;
    can_read = true;
    can_write = true;
    reset(true);
}

emulator::Result AgatYazs::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    // 8 clocks (128 kHz) differ from 4 by under 0.5 dB below 5 kHz and cost half
    m_substep = read_confg_value(cd, "substep", false, (unsigned int)8);
    if (m_substep < 1 || m_substep > 16 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{AgatYazs|" + std::string(QT_TRANSLATE_NOOP("AgatYazs", "Incorrect step or clock")) + "}");
    m_port_value = read_confg_value(cd, "port_value", false, (unsigned int)0) & 0xFF;
    // volume, percent: at 100 the full scale is 1.5 V at the mixer output,
    // about where a loud chord with a drum peaks. The card is far quieter
    // than the speaker by its circuit: a channel alone gives 0.1-0.3 V
    m_gain = read_confg_value(cd, "volume", false, (unsigned int)100) / 100.0 / 1.5;

    prepare();
    return emulator::Result::ok();
}

// Everything derived from the step: the decay of the base drive per clock and
// the inverted matrices of the tone channels
void AgatYazs::prepare()
{
    const double clk = (double)m_system_clock;
    m_h = m_substep / clk;
    m_tick_on  = std::exp(-1.0 / (clk * R18 * C2));
    m_tick_off = std::exp(-1.0 / (clk * (R18 + R19) * C2));

    // Configuration bits: VD2, VD3, the key VT1, the key of D3
    m_ch_inv.assign(16 * 36, 0.0);
    for (unsigned int cfg = 0; cfg < 16; cfg++) {
        double a[36] = {0};
        const int n = CH_NODES;
        stamp(a, n, NB, -1, 1 / R16);
        stamp(a, n, NB, NC, C3 / m_h);
        stamp(a, n, NB, NC, 1 / R17);
        stamp(a, n, NC, -1, 1 / R21);
        stamp(a, n, NC, -1, 1 / R23);
        stamp(a, n, NC, -1, (cfg & 1) ? G_ON : G_OFF);
        stamp(a, n, NC, NF, (cfg & 2) ? G_ON : G_OFF);
        stamp(a, n, NF, -1, C4 / m_h);
        stamp(a, n, NF, NG, 1 / R22);
        stamp(a, n, NG, -1, C5 / m_h);
        stamp(a, n, NG, -1, (cfg & 4) ? G_ON : G_OFF);
        stamp(a, n, NG, NH, C6 / m_h);
        stamp(a, n, NH, NJ, 1 / R25);
        if (cfg & 8) stamp(a, n, NH, NJ, 1 / R_SW);
        stamp(a, n, NJ, -1, 1 / R26);
        stamp(a, n, NJ, -1, 1 / R24);
        invert(a, n, &m_ch_inv[cfg * 36]);
    }

    m_bus_inv.assign(216 * BUS_NODES * BUS_NODES, 0.0);
    m_bus_valid.assign(216, 0);
}

// The buses' matrix depends on how many channels drive each of them
const double * AgatYazs::bus_inverse(unsigned int config)
{
    double *inv = &m_bus_inv[config * BUS_NODES * BUS_NODES];
    if (m_bus_valid[config]) return inv;
    const unsigned int n_hh = config / 36, n_mh = (config / 6) % 6, n_lh = config % 6;
    double a[BUS_NODES * BUS_NODES] = {0};
    const int n = BUS_NODES;
    const double h = m_h;
    stamp(a, n, MH, -1, C43 / h);
    stamp(a, n, MH, MC, C46 / h);
    stamp(a, n, MC, SND, 1 / R131);
    stamp(a, n, MH, N48, 1 / R127);
    stamp(a, n, LH, -1, C44 / h);
    stamp(a, n, LH, LM, 1 / R128);
    stamp(a, n, LM, -1, C49 / h);
    stamp(a, n, LM, SND, 1 / R132);
    stamp(a, n, LH, N48, 1 / R129);
    stamp(a, n, HH, HM, C45 / h);
    stamp(a, n, HM, -1, 1 / R126);
    stamp(a, n, HM, HC, C47 / h);
    stamp(a, n, HC, SND, 1 / R133);
    stamp(a, n, HH, N48, 1 / R130);
    stamp(a, n, N48, -1, C48 / h);
    stamp(a, n, SND, -1, C50 / h);
    // A bus nobody drives still has its 10K and the filter: never singular
    stamp(a, n, MH, -1, n_mh / R_BUS);
    stamp(a, n, LH, -1, n_lh / R_BUS);
    stamp(a, n, HH, -1, n_hh / R_BUS);
    invert(a, n, inv);
    m_bus_valid[config] = 1;
    return inv;
}

void AgatYazs::reset(bool cold)
{
    // RESET clears the interrupt flags and the enable triggers D11; the
    // latches of the channels, the counters and the analog part keep what
    // they have, and power-on starts them silent
    if (cold) {
        memset(m_regs, 0, sizeof(m_regs));
        for (int i = 0; i < COUNTERS; i++) m_cnt[i] = Vi53Counter();
        m_lfsr = 0x1FFFF;
        m_div = 0;
        m_pending = 0;
        m_irqs = 0;
        cold_analog();
    }
    m_flag_t = false;
    m_flag_e = false;
    m_ext_en = true;
    if (!m_cet)
        for (int c = 0; c < TONES; c++) wake_channel(c);
    m_regs[15] &= 0x3F;
    set_cet(true);
    m_flag_t = false;
    update_irq();
}

// The resting point of a silent card: no envelope, no channel on a bus
void AgatYazs::cold_analog()
{
    for (int c = 0; c < TONES; c++) {
        m_v[c][NB] = V_LATCH_LO;
        m_v[c][NC] = -VD;
        m_v[c][NF] = 0;
        m_v[c][NG] = 0;
        m_v[c][NH] = VCC / 2;
        m_v[c][NJ] = VCC / 2;
        m_x[c] = 0;
        m_tout[c] = true;
        m_d2[c] = true;
        m_d3[c] = false;
    }
    for (int i = 0; i < BUS_NODES; i++) m_bus[i] = 0;
    m_vo = 0;
    m_d6_c27 = V_LATCH_LO; m_d6_e = 0; m_d6_p0 = 0;
    m_d6_c30 = 0; m_d6_c31 = VCC / 2; m_d6_nz = 0;
    m_d6_a33 = 0; m_d6_c35 = 0; m_d6_c32 = 0; m_d6_n = 0; m_d6_m = 0; m_d6_c36 = 0;
    m_d6_o1 = true; m_d6_o2 = false;
    m_d7_c37 = V_LATCH_LO; m_d7_e = 0;
    m_d7_c39 = 0; m_d7_c40 = VCC / 2; m_d7_k = 0; m_d7_c42 = 0;
    m_d7_o3 = true; m_d7_o4 = false;
    m_noise_v = VCC;
    m_out_sum = 0;
    m_out_count = 0;
    m_out = 0;
    m_lp = 0;
    m_out_left_sum = 0;
    m_out_left = 0;
    m_sleeping = true;
    m_quiet = 0;
    m_delta = 0;
    for (int c = 0; c < TONES; c++) m_ch_quiet[c] = 0;
    m_dr_quiet = 0;
    for (int i = 0; i < 7; i++) m_peak[i] = 0;
}

void AgatYazs::wake()
{
    m_quiet = 0;
    if (!m_sleeping) return;
    m_sleeping = false;
    // The base drives were not followed while asleep: start them settled
    for (int c = 0; c < TONES; c++) {
        m_x[c] = 0;
        m_tout[c] = m_cnt[c].out;
    }
    sound_mode_changed();
}

void AgatYazs::wake_channel(int c)
{
    if (m_ch_quiet[c] >= IDLE_STEPS) {
        // Its base drive was not followed while idle
        m_x[c] = 0;
        m_tout[c] = m_cnt[c].out;
    }
    m_ch_quiet[c] = 0;
    wake();
}

void AgatYazs::wake_drums()
{
    if (m_dr_quiet >= IDLE_STEPS) {
        // The output networks were left as they settled - no current, every
        // node but SOUND at 0 - while SOUND went on relaxing. Put them at
        // rest against where it is now, or the first step lets the
        // difference through as a click
        const double vs = m_bus[SND];
        m_d6_a33 = 0;
        m_d6_c35 = -vs;
        m_d6_c32 = 0;
        m_d6_n = 0;
        m_d6_m = 0;
        m_d6_c36 = -vs;
        m_d7_k = 0;
        m_d7_c42 = -vs;
    }
    m_dr_quiet = 0;
    wake();
}

//------------------- Bus --------------------------------------------------//

unsigned int AgatYazs::status() const
{
    return 0x1F | ((i_ext.value & 1) ? 0x20 : 0) | (m_flag_e ? 0x40 : 0) | (m_flag_t ? 0x80 : 0);
}

unsigned int AgatYazs::get_value(unsigned int address)
{
    if (address & 4) {
        // The external port; reading it clears the external request
        if (m_flag_e) {
            m_flag_e = false;
            update_irq();
        }
        return m_port_value;
    }
    return status();
}

unsigned int AgatYazs::get_direct(unsigned int address)
{
    return (address & 4) ? m_port_value : status();
}

void AgatYazs::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    const unsigned int a = address & 15;
    value &= 0xFF;
    m_regs[a] = (uint8_t)value;
    if (a < 8) {
        write_timer(a >> 2, a & 3, value);
        // Any write to the counters clears the timer flag
        m_flag_t = false;
        update_irq();
        return;
    }
    if (a == 15) {
        m_ext_en = (value & 0x40) == 0;
        if (m_cet != ((value & 0x80) == 0))
            for (int c = 0; c < TONES; c++) wake_channel(c);
        set_cet((value & 0x80) == 0);
        update_irq();
        return;
    }
    m_peak[a - 8] = 0;
    if (a < 13) wake_channel(a - 8); else wake_drums();
}

void AgatYazs::write_timer(unsigned int chip, unsigned int reg, unsigned int value)
{
    unsigned int idx;
    if (reg == 3) {
        const unsigned int sc = value >> 6;
        if (sc == 3) return;            // no read-back command in the 8253
        idx = chip * 3 + sc;
        m_cnt[idx].control(value);
    } else {
        idx = chip * 3 + reg;
        m_cnt[idx].write(value);
    }
    if (idx < (unsigned int)TONES) wake_channel((int)idx);
    // The write holds the timer flag in reset (WRT on R of D6.1): an edge of
    // OUT2 it causes, as a control word raising it, sets nothing
}

// CET drives the GATE of all six counters and the D input of the timer flag
void AgatYazs::set_cet(bool cet)
{
    m_cet = cet;
    const bool o5 = m_cnt[5].out;
    for (int i = 0; i < COUNTERS; i++) m_cnt[i].set_gate(cet);
    irq_edge(o5);
}

// The timer flag is a D flip-flop clocked by OUT2 of the second chip, with
// CET on D: a rising edge sets it while enabled and clears it otherwise
void AgatYazs::irq_edge(bool old_out)
{
    if (old_out || !m_cnt[5].out) return;
    m_flag_t = m_cet;
    if (m_cet) m_irqs++;
    update_irq();
}

void AgatYazs::update_irq()
{
    const unsigned int v = ((i_irq_in.value & 1) && !m_flag_t && !m_flag_e) ? 1 : 0;
    if ((i_irq.value & 1) != v) i_irq.change(v);
}

void AgatYazs::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == 2) {
        if ((new_value & 1) && !(old_value & 1)) m_flag_e = m_ext_en;
    }
    update_irq();
}

//------------------- Time -------------------------------------------------//

void AgatYazs::clock(unsigned int counter)
{
    run(counter);
    if (m_out_count) {
        m_out = m_out_sum / m_out_count;
        m_out_left = m_out_left_sum / m_out_count;
        m_out_sum = 0;
        m_out_left_sum = 0;
        m_out_count = 0;
    }
}

void AgatYazs::run(unsigned int clocks)
{
    m_pending += clocks;
    while (m_pending >= m_substep) {
        m_pending -= m_substep;
        step();
    }
}

void AgatYazs::step()
{
    const unsigned int h = m_substep;

    // Tone counters, and the base drive of each key between their edges
    unsigned int on[TONES];
    for (int c = 0; c < TONES; c++) {
        Vi53Counter &k = m_cnt[c];
        unsigned int rem = h;
        unsigned int n_on = 0;
        double x = m_x[c];
        while (rem) {
            if (k.out != m_tout[c]) {
                // An edge passes through C2 as it is
                x += k.out ? (V_T_HI - V_T_LO) : (V_T_LO - V_T_HI);
                m_tout[c] = k.out;
            }
            const uint32_t e = k.until_event();
            const unsigned int seg = (e < rem) ? (unsigned int)e : rem;
            if (!m_sleeping && m_ch_quiet[c] < IDLE_STEPS)
                for (unsigned int i = 0; i < seg; i++) {
                    if (x > X_ON) {
                        n_on++;
                        x = VBE + (x - VBE) * m_tick_on;
                    } else
                        x *= m_tick_off;
                }
            k.advance(seg);
            rem -= seg;
        }
        m_x[c] = x;
        on[c] = n_on;
    }

    // The divider chain: noise at 1/16, the interrupt counter at 1/128
    const unsigned int d0 = m_div;
    m_div += h;
    for (unsigned int s = m_div / 16 - d0 / 16; s > 0; s--) {
        const uint32_t bit = ((m_lfsr >> 11) ^ (m_lfsr >> 16)) & 1;
        m_lfsr = ((m_lfsr << 1) | bit) & 0x1FFFF;
    }
    while (m_div >= 128) {
        m_div -= 128;
        const bool o5 = m_cnt[5].out;
        m_cnt[5].advance(1);
        irq_edge(o5);
    }

    if (m_sleeping) return;

    const double nt = ((m_lfsr >> 16) & 1) ? VCC : 0;
    m_noise_v += (nt - m_noise_v) * m_h / (TAU_NOISE + m_h);

    // A channel that has settled and cannot move by itself - no envelope, or
    // a counter that will not change its output again - is not stepped: its
    // voltages are those it settled at. A write to its register or counter
    // brings it back. Most of the time a tune plays on fewer than five
    bool idle = true;
    double chout[TONES];
    for (int c = 0; c < TONES; c++) {
        if (m_ch_quiet[c] < IDLE_STEPS) {
            idle = false;
            const double d = tone_step(c, on[c]);
            // Below 10 mV the channel is 50 dB under a loud one, and with
            // node C held down by VD2 nothing can lift it but a write
            const bool still = (m_v[c][NF] < 0.01 && m_v[c][NC] < -0.3)
                || (d < 1e-7 && m_cnt[c].until_event() == Vi53Counter::NEVER);
            m_ch_quiet[c] = still ? m_ch_quiet[c] + 1 : 0;
        }
        chout[c] = std::max(0.0, m_v[c][NJ] - VBE);
        m_peak[c] = std::max(m_peak[c], m_v[c][NF]);
    }

    // The drums the same way, once neither follower lets anything through and
    // their output networks have settled: only the envelopes go on decaying
    const bool full = m_dr_quiet < IDLE_STEPS;
    m_delta = 0;
    const double i_drums = drums_step(m_bus[SND], full);
    m_peak[5] = std::max(m_peak[5], m_d6_e);
    m_peak[6] = std::max(m_peak[6], m_d7_e);
    if (full) {
        idle = false;
        // 0.1 mV: the multivibrators run on, and through the leakage of a
        // closed diode they still move the output nodes by some 10 uV
        const bool still = m_delta < 1e-4 && m_dr_silent;
        m_dr_quiet = still ? m_dr_quiet + 1 : 0;
    } else if (m_delta > 1e-6)
        idle = false;

    m_delta = 0;
    bus_step(chout, i_drums);

    m_out_sum += m_vo;
    m_out_count++;
    if (m_stereo) {
        m_lp += (m_vo - m_lp) * m_h / (R137 * C55 + m_h);
        m_out_left_sum += std::min(VO_MAX, std::max(VO_MIN, 2 * m_lp - m_vo));
    }

    // Asleep once nothing else moves and the mixer has settled as well
    if (idle && m_delta < 1e-6) {
        if (++m_quiet > IDLE_STEPS) {
            m_sleeping = true;
            sound_mode_changed();
        }
    } else
        m_quiet = 0;
}

//------------------- Tone channels ----------------------------------------//

// One step of a channel with the key held on or off; returns the voltages in x
double AgatYazs::tone_solve(int c, bool key, double *x)
{
    const double *v = m_v[c];
    const uint8_t r = m_regs[8 + c];
    const double va4 = (r & 0x10) ? V_LATCH_HI : V_LATCH_LO;
    const double va5 = (r & 0x20) ? V_LATCH_HI : V_LATCH_LO;
    const double g3 = C3 / m_h, g4 = C4 / m_h, g5 = C5 / m_h, g6 = C6 / m_h;
    bool d2 = m_d2[c], d3 = m_d3[c];
    for (int it = 0; it < 4; it++) {
        double rhs[CH_NODES];
        rhs[NB] = va4 / R16 + g3 * (v[NB] - v[NC]);
        rhs[NC] = va5 / R21 + V_NEG / R23 - g3 * (v[NB] - v[NC])
                + (d2 ? -G_ON * VD : 0) + (d3 ? G_ON * VD : 0);
        rhs[NF] = g4 * v[NF] + (d3 ? -G_ON * VD : 0);
        rhs[NG] = g5 * v[NG] + g6 * (v[NG] - v[NH]);
        rhs[NH] = -g6 * (v[NG] - v[NH]);
        rhs[NJ] = VCC / R26;
        const unsigned int cfg = (d2 ? 1 : 0) | (d3 ? 2 : 0) | (key ? 4 : 0) | ((r & 8) ? 8 : 0);
        mul(&m_ch_inv[cfg * 36], CH_NODES, rhs, x);
        const bool n2 = x[NC] < -VD;
        const bool n3 = x[NC] - x[NF] > VD;
        if (n2 == d2 && n3 == d3) break;
        d2 = n2;
        d3 = n3;
    }
    m_d2[c] = d2;
    m_d3[c] = d3;
    return x[NJ];
}

// The key VT1 conducted for on_clocks of the step: the step is solved both
// ways and weighted, which keeps the edges of a tone off the step grid
double AgatYazs::tone_step(int c, unsigned int on_clocks)
{
    double x[CH_NODES];
    if (on_clocks == 0 || on_clocks >= m_substep)
        tone_solve(c, on_clocks != 0, x);
    else {
        double x1[CH_NODES];
        tone_solve(c, true, x1);
        tone_solve(c, false, x);
        const double f = (double)on_clocks / m_substep;
        for (int i = 0; i < CH_NODES; i++) x[i] = f * x1[i] + (1 - f) * x[i];
    }
    double d = 0;
    for (int i = 0; i < CH_NODES; i++) {
        d = std::max(d, std::fabs(x[i] - m_v[c][i]));
        m_v[c][i] = x[i];
    }
    return d;
}

//------------------- Buses and mixer --------------------------------------//

void AgatYazs::bus_step(const double *chout, double i_drums)
{
    // D0 puts a channel on the HF bus, D1 on the MF, D2 on the LF one
    unsigned int n_hh = 0, n_mh = 0, n_lh = 0;
    double s_hh = 0, s_mh = 0, s_lh = 0;
    for (int c = 0; c < TONES; c++) {
        const uint8_t r = m_regs[8 + c];
        if (r & 1) { n_hh++; s_hh += chout[c]; }
        if (r & 2) { n_mh++; s_mh += chout[c]; }
        if (r & 4) { n_lh++; s_lh += chout[c]; }
    }
    const double h = m_h;
    const double *o = m_bus;
    double rhs[BUS_NODES];
    rhs[MH]  = C43 / h * o[MH] + C46 / h * (o[MH] - o[MC]) + s_mh / R_BUS;
    rhs[MC]  = -C46 / h * (o[MH] - o[MC]);
    rhs[LH]  = C44 / h * o[LH] + s_lh / R_BUS;
    rhs[LM]  = C49 / h * o[LM];
    rhs[HH]  = C45 / h * (o[HH] - o[HM]) + s_hh / R_BUS;
    rhs[HM]  = -C45 / h * (o[HH] - o[HM]) + C47 / h * (o[HM] - o[HC]);
    rhs[HC]  = -C47 / h * (o[HM] - o[HC]);
    rhs[N48] = C48 / h * o[N48];
    rhs[SND] = C50 / h * o[SND] + i_drums;

    double x[BUS_NODES];
    mul(bus_inverse(n_hh * 36 + n_mh * 6 + n_lh), BUS_NODES, rhs, x);

    // SOUND reaches the virtual ground of DA1.1 through C50
    const double i_in = C50 / h * (x[SND] - o[SND]);
    for (int i = 0; i < BUS_NODES; i++) {
        m_delta = std::max(m_delta, std::fabs(x[i] - m_bus[i]));
        m_bus[i] = x[i];
    }
    const double g53 = C53 / h;
    double vo = (g53 * m_vo - i_in) / (g53 + 1 / R136);
    vo = std::min(VO_MAX, std::max(VO_MIN, vo));
    m_delta = std::max(m_delta, std::fabs(vo - m_vo));
    m_vo = vo;
}

//------------------- Drums ------------------------------------------------//

// Both drum channels for one step; returns the current they push into SOUND.
// Without "full" only the envelopes are followed: the drums are quiet then.
// Their output networks see SOUND at its voltage of the step before: it is
// held near the virtual ground by C50 and moves little within a step
double AgatYazs::drums_step(double vs, bool full)
{
    const double h = m_h;
    double old;

    //--- Channel 6 (C0nD) ---
    const uint8_t r6 = m_regs[13];
    // Start: D4 through C27 to the base of the follower VT11
    double a = (r6 & 0x10) ? V_LATCH_HI : V_LATCH_LO;
    double b = a - m_d6_c27;
    if (b < -VD) { b = -VD; m_d6_c27 = a + VD; }
    m_d6_c27 += h * (b / R97) / C27;

    const double O1 = m_d6_o1 ? VCC : 0, O2 = m_d6_o2 ? VCC : 0;
    const double I1 = O2 - m_d6_c30, I2 = O1 - m_d6_c31;
    // C29 discharges into the multivibrator (R98, VD13) and towards VT12
    // (R100, shorted by D3 - the longer sound)
    const double i98 = (m_d6_e - VD > I1) ? (m_d6_e - VD - I1) / R98 : 0;
    const double i100 = (m_d6_e - m_d6_p0) / ((r6 & 8) ? R_SW : R100);
    old = m_d6_e;
    m_d6_e -= h * (i98 + i100) / C29;
    if (m_d6_e < b - VBE) m_d6_e = b - VBE;
    if (m_d6_e < 0) m_d6_e = 0;
    m_delta = std::max(m_delta, std::fabs(m_d6_e - old));
    m_d6_p0 += h * (i100 - m_d6_p0 / R103) / C65;
    const double p = std::max(0.0, m_d6_p0 - VBE);     // emitter of VT12

    //--- Channel 7 (C0nE) ---
    const uint8_t r7 = m_regs[14];
    a = (r7 & 0x10) ? V_LATCH_HI : V_LATCH_LO;
    b = a - m_d7_c37;
    if (b < -VD) { b = -VD; m_d7_c37 = a + VD; }
    m_d7_c37 += h * (b / R117) / C37;
    // C38 feeds the base of VT15 through R118, shorted by D3
    const double rl = (r7 & 8) ? R119 : R118 + R119;
    old = m_d7_e;
    m_d7_e -= h * (m_d7_e / rl) / C38;
    if (m_d7_e < b - VBE) m_d7_e = b - VBE;
    if (m_d7_e < 0) m_d7_e = 0;
    m_delta = std::max(m_delta, std::fabs(m_d7_e - old));
    const double b15 = (r7 & 8) ? m_d7_e : m_d7_e * R119 / (R118 + R119);
    const double e7 = std::max(0.0, b15 - VBE);

    // Quiet drums: only the envelopes move, nothing reaches the output
    m_dr_silent = (p == 0 && e7 == 0);
    if (!full) return 0;


    // The multivibrator: D0, D1, D2 shunt R102 and so raise the pitch; with
    // D2 = 0 the noise pushes its second input through D10.1
    double rb = R102;
    if (r6 & 1) rb = par(rb, R106);
    if (r6 & 2) rb = par(rb, R107);
    if (r6 & 4) rb = par(rb, R105);
    double i_n = 0;
    if (!(r6 & 4) && m_d6_nz - VD > I2) i_n = (m_d6_nz - VD - I2) / R99;
    m_d6_c30 += -h * ((O1 - I1) / R101 + i98) / C30;
    m_d6_c31 += -h * ((O2 - I2) / rb + i_n) / C31;
    m_d6_nz += h * ((m_noise_v - m_d6_nz) / R96 - i_n) / C28;
    flip_pair(m_d6_o1, m_d6_o2, m_d6_c30, m_d6_c31);

    // VD15 cuts the envelope with the output of D9.2
    const double q = m_d6_o1 ? p : std::min(p, VD);
    // R112, C33 and C35, R115 into SOUND
    double xa, xx;
    const double g33 = C33 / h, g35 = C35 / h;
    solve2(1 / R112 + g33 + g35, -g35, -g35, g35 + 1 / R115,
           q / R112 + g33 * m_d6_a33 + g35 * m_d6_c35, -g35 * m_d6_c35 + vs / R115, xa, xx);
    m_delta = std::max(m_delta, std::fabs(xa - m_d6_a33));
    m_d6_a33 = xa;
    m_d6_c35 = xa - xx;
    double i_out = (xx - vs) / R115;

    // The noise branch: C32 and R110 from the envelope, VT13 shorting the
    // node by the noise, R113/C34 and R114/C36 into SOUND. A chain of four
    // nodes - Y (C32, R111), N, M (C34), Z (C36) - solved as tridiagonal
    {
        const double g32 = C32 / h, g34 = C34 / h, g36 = C36 / h;
        const double gk = (m_noise_v / 2 > VBE) ? G_ON : G_OFF;
        // rows: diag d, lower l, upper u, right side f
        double d[4], l[4], u[4], f[4];
        d[0] = g32 + 1 / R111;             u[0] = -1 / R111;  l[0] = 0;
        f[0] = g32 * (p - m_d6_c32);
        d[1] = 1 / R111 + 1 / R110 + gk + 1 / R113;  l[1] = -1 / R111;  u[1] = -1 / R113;
        f[1] = p / R110;
        d[2] = 1 / R113 + g34 + 1 / R114;  l[2] = -1 / R113;  u[2] = -1 / R114;
        f[2] = g34 * m_d6_m;
        d[3] = 1 / R114 + g36;             l[3] = -1 / R114;  u[3] = 0;
        f[3] = g36 * (vs + m_d6_c36);
        for (int i = 1; i < 4; i++) {
            const double w = l[i] / d[i - 1];
            d[i] -= w * u[i - 1];
            f[i] -= w * f[i - 1];
        }
        double y[4];
        y[3] = f[3] / d[3];
        for (int i = 2; i >= 0; i--) y[i] = (f[i] - u[i] * y[i + 1]) / d[i];
        m_d6_c32 = p - y[0];
        m_d6_n = y[1];
        m_delta = std::max(m_delta, std::fabs(y[2] - m_d6_m));
        m_d6_m = y[2];
        i_out += g36 * ((y[3] - vs) - m_d6_c36);
        m_d6_c36 = y[3] - vs;
    }

    // Multivibrator D9.4/D9.1: D0 adds R124 to R121, D1 = 0 adds C41 to
    // C40; the noise pushes whichever input is lower through R150
    const double O3 = m_d7_o3 ? VCC : 0, O4 = m_d7_o4 ? VCC : 0;
    const double I3 = O4 - m_d7_c39, I4 = O3 - m_d7_c40;
    const double r4 = (r7 & 1) ? par(R121, R124) : R121;
    const double c4 = C40 + ((r7 & 2) ? 0 : C41);
    double i_n3 = 0, i_n4 = 0;
    const double lo = std::min(I3, I4);
    if (m_noise_v - VD > lo) {
        const double i = (m_noise_v - VD - lo) / R150;
        if (I3 <= I4) i_n3 = i; else i_n4 = i;
    }
    m_d7_c39 += -h * ((O3 - I3) / R122 + i_n3) / C39;
    m_d7_c40 += -h * ((O4 - I4) / r4 + i_n4) / c4;
    flip_pair(m_d7_o3, m_d7_o4, m_d7_c39, m_d7_c40);

    // R123 from the emitter of VT15, VD19 to the output of D9.4, C42 and
    // R125 into SOUND. W, behind C42, is linear in K, which leaves one
    // equation in K: Gk * K - Bk + Id(K - o3) = 0
    {
        const double g42 = C42 / h;
        const double o3 = m_d7_o3 ? VCC : 0;
        const double gw = g42 + 1 / R125;
        const double gk = 1 / R123 + g42 * (1 - g42 / gw);
        const double bk = e7 / R123 + g42 * m_d7_c42 * (1 - g42 / gw) + g42 * vs / (R125 * gw);
        const double k = o3 + VD_NVT * exp_root(VD_IS, gk * VD_NVT, bk - gk * o3);
        const double w = (g42 * (k - m_d7_c42) + vs / R125) / gw;
        m_delta = std::max(m_delta, std::fabs(k - m_d7_k));
        m_d7_k = k;
        m_d7_c42 = k - w;
        i_out += (w - vs) / R125;
    }
    return i_out;
}

//------------------- Sound ------------------------------------------------//

int32_t AgatYazs::sound_sample(int64_t amplitude)
{
    double s = m_out * m_gain;
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    return (int32_t)(s * amplitude);
}

int32_t AgatYazs::sound_sample_left(int64_t amplitude)
{
    double s = m_out_left * m_gain;
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    return (int32_t)(s * amplitude);
}

//------------------- State ------------------------------------------------//

void AgatYazs::analog_vars(std::vector<double*> &v)
{
    v.clear();
    for (int c = 0; c < TONES; c++) {
        for (int i = 0; i < CH_NODES; i++) v.push_back(&m_v[c][i]);
        v.push_back(&m_x[c]);
    }
    for (int i = 0; i < BUS_NODES; i++) v.push_back(&m_bus[i]);
    double * other[] = {
        &m_vo,
        &m_d6_c27, &m_d6_e, &m_d6_p0, &m_d6_c30, &m_d6_c31, &m_d6_nz,
        &m_d6_a33, &m_d6_c35, &m_d6_c32, &m_d6_n, &m_d6_m, &m_d6_c36,
        &m_d7_c37, &m_d7_e, &m_d7_c39, &m_d7_c40, &m_d7_k, &m_d7_c42,
        &m_noise_v, &m_out_sum, &m_out,
        &m_peak[0], &m_peak[1], &m_peak[2], &m_peak[3], &m_peak[4], &m_peak[5], &m_peak[6]
    };
    for (double *p : other) v.push_back(p);
}

void AgatYazs::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.array("regs", m_regs, 16);
    w.b("cet", m_cet);
    w.b("ext_en", m_ext_en);
    w.b("flag_t", m_flag_t);
    w.b("flag_e", m_flag_e);
    w.n("irqs", m_irqs);
    w.n("pending", m_pending);
    w.n("div", m_div);
    w.u("lfsr", m_lfsr, 32);
    w.b("sleeping", m_sleeping);
    w.n("quiet", m_quiet);
    w.array("ch_quiet", m_ch_quiet, TONES);
    w.n("dr_quiet", m_dr_quiet);
    w.n("out_count", m_out_count);

    uint32_t f[13][COUNTERS];
    for (int i = 0; i < COUNTERS; i++) {
        const Vi53Counter &k = m_cnt[i];
        f[0][i] = k.mode;  f[1][i] = k.rw;  f[2][i] = k.bcd;  f[3][i] = k.msb_next;
        f[4][i] = k.lsb;   f[5][i] = k.n;   f[6][i] = k.pending_n;  f[7][i] = k.has_pending;
        f[8][i] = k.gate;  f[9][i] = k.out; f[10][i] = k.state; f[11][i] = k.phase; f[12][i] = k.left;
    }
    static const char * names[13] = {
        "cnt_mode", "cnt_rw", "cnt_bcd", "cnt_msb_next", "cnt_lsb", "cnt_n", "cnt_pending_n",
        "cnt_has_pending", "cnt_gate", "cnt_out", "cnt_state", "cnt_phase", "cnt_left"
    };
    for (int j = 0; j < 13; j++) w.array(names[j], f[j], COUNTERS);

    bool bits[TONES * 3 + 4];
    for (int c = 0; c < TONES; c++) {
        bits[c * 3] = m_tout[c];
        bits[c * 3 + 1] = m_d2[c];
        bits[c * 3 + 2] = m_d3[c];
    }
    bits[TONES * 3]     = m_d6_o1;
    bits[TONES * 3 + 1] = m_d6_o2;
    bits[TONES * 3 + 2] = m_d7_o3;
    bits[TONES * 3 + 3] = m_d7_o4;
    w.array("bits", bits, TONES * 3 + 4);

    // The analog part as the bits of its doubles: a decimal rendering would
    // not come back to the same values
    std::vector<double*> v;
    analog_vars(v);
    std::vector<uint8_t> raw(v.size() * 8);
    for (size_t i = 0; i < v.size(); i++) {
        uint64_t u;
        memcpy(&u, v[i], 8);
        for (int b = 0; b < 8; b++) raw[i * 8 + b] = (uint8_t)(u >> (b * 8));
    }
    w.hex("analog", raw.data(), raw.size());

    // Apart from "analog", which keeps its layout for older snapshots
    double lr[3] = {m_lp, m_out_left_sum, m_out_left};
    uint8_t rlr[24];
    for (int i = 0; i < 3; i++) {
        uint64_t u;
        memcpy(&u, &lr[i], 8);
        for (int b = 0; b < 8; b++) rlr[i * 8 + b] = (uint8_t)(u >> (b * 8));
    }
    w.hex("left", rlr, 24);
}

emulator::Result AgatYazs::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.array("regs", m_regs, 16);
    r.b("cet", m_cet);
    r.b("ext_en", m_ext_en);
    r.b("flag_t", m_flag_t);
    r.b("flag_e", m_flag_e);
    r.u("irqs", m_irqs);
    r.u("pending", m_pending);
    r.u("div", m_div);
    r.u("lfsr", m_lfsr);
    r.b("sleeping", m_sleeping);
    r.u("quiet", m_quiet);
    r.array("ch_quiet", m_ch_quiet, TONES);
    r.u("dr_quiet", m_dr_quiet);
    r.u("out_count", m_out_count);

    static const char * names[13] = {
        "cnt_mode", "cnt_rw", "cnt_bcd", "cnt_msb_next", "cnt_lsb", "cnt_n", "cnt_pending_n",
        "cnt_has_pending", "cnt_gate", "cnt_out", "cnt_state", "cnt_phase", "cnt_left"
    };
    uint32_t f[13][COUNTERS];
    for (int j = 0; j < 13; j++) {
        for (int i = 0; i < COUNTERS; i++) f[j][i] = 0;
        r.array(names[j], f[j], COUNTERS);
    }
    for (int i = 0; i < COUNTERS; i++) {
        Vi53Counter &k = m_cnt[i];
        k.mode = (uint8_t)f[0][i];  k.rw = (uint8_t)f[1][i];  k.bcd = f[2][i] != 0;
        k.msb_next = f[3][i] != 0;  k.lsb = (uint8_t)f[4][i]; k.n = f[5][i];
        k.pending_n = f[6][i];      k.has_pending = f[7][i] != 0;
        k.gate = f[8][i] != 0;      k.out = f[9][i] != 0;     k.state = (uint8_t)f[10][i];
        k.phase = (uint8_t)f[11][i]; k.left = f[12][i];
        // A snapshot may come from a link: a counter that would never reach its
        // next event (nothing to count, a state that does not exist) would
        // hold the emulation thread in step() for good
        if (k.mode > 5) k.mode = 0;
        if (k.rw < 1 || k.rw > 3) k.rw = 3;
        if (k.state > Vi53Counter::DONE) k.state = Vi53Counter::IDLE;
        if (k.n == 0 || k.n > 65536) k.n = 65536;
        if (k.pending_n == 0 || k.pending_n > 65536) k.has_pending = false;
        if (k.left == 0) k.left = 1;
    }

    bool bits[TONES * 3 + 4];
    if (r.array("bits", bits, TONES * 3 + 4)) {
        for (int c = 0; c < TONES; c++) {
            m_tout[c] = bits[c * 3];
            m_d2[c] = bits[c * 3 + 1];
            m_d3[c] = bits[c * 3 + 2];
        }
        m_d6_o1 = bits[TONES * 3];
        m_d6_o2 = bits[TONES * 3 + 1];
        m_d7_o3 = bits[TONES * 3 + 2];
        m_d7_o4 = bits[TONES * 3 + 3];
    }

    std::vector<double*> v;
    analog_vars(v);
    std::vector<uint8_t> raw(v.size() * 8);
    if (r.hex("analog", raw.data(), raw.size()))
        for (size_t i = 0; i < v.size(); i++) {
            uint64_t u = 0;
            for (int b = 0; b < 8; b++) u |= (uint64_t)raw[i * 8 + b] << (b * 8);
            memcpy(v[i], &u, 8);
            if (!std::isfinite(*v[i])) *v[i] = 0;
        }

    double *lr[3] = {&m_lp, &m_out_left_sum, &m_out_left};
    uint8_t rlr[24];
    if (r.hex("left", rlr, 24))
        for (int i = 0; i < 3; i++) {
            uint64_t u = 0;
            for (int b = 0; b < 8; b++) u |= (uint64_t)rlr[i * 8 + b] << (b * 8);
            memcpy(lr[i], &u, 8);
            if (!std::isfinite(*lr[i])) *lr[i] = 0;
        }
    else {
        // A snapshot from before the left output: start it where the mixer is
        m_lp = m_vo;
        m_out_left = m_out;
        m_out_left_sum = 0;
    }
    return emulator::Result::ok();
}

void AgatYazs::state_restored()
{
    // The mixer asks sound_volatile() once, when the sources change
    sound_mode_changed();
}

//------------------- Introspection ----------------------------------------//

ConfigFields AgatYazs::get_config_fields()
{
    ConfigField f;
    f.name = "stereo";
    f.title = QT_TRANSLATE_NOOP("DeviceOptions", "Sound output");
    f.type = CONFIG_FIELD_CHOICE;
    f.def = "0";
    f.values.push_back({"0", QT_TRANSLATE_NOOP("DeviceOptions", "Mono (RIGHT output)")});
    f.values.push_back({"1", QT_TRANSLATE_NOOP("DeviceOptions", "Stereo (both line outputs)")});
    return {f};
}

std::vector<DeviceFieldInfo> AgatYazs::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"regs",     "Last values written to the 16 registers; without a range all of them", true});
    r.push_back({"status",   "Interrupt status as read at C0n0 (D7 timer, D6 external, D5 input)", false});
    r.push_back({"cet",      "1 if counting and the timer interrupt are enabled", false});
    r.push_back({"irqs",     "Times the timer interrupt flag was set", false});
    r.push_back({"counters", "Counts in effect of the six counters (tones 1-5, timer)", false});
    r.push_back({"outs",     "Outputs of the six counters, bit n = counter n", false});
    r.push_back({"level",    "Mixer output, mV", false});
    r.push_back({"env",      "Envelope of the five tone channels (node F), mV", false});
    r.push_back({"drums",    "Envelopes of the drums 6 and 7, mV", false});
    r.push_back({"peak",     "Highest envelope of each channel and drum since the last write to it, mV", false});
    r.push_back({"sleeping", "1 while the analog part is silent and not stepped", false});
    r.push_back({"idle",     "Tone channels (bits 0-4) and drums (bit 5) that are not stepped", false});
    return r;
}

bool AgatYazs::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "regs") {
        if (from == 0 && to == 0) to = 15;
        if (to > 15) to = 15;
        if (from > to) from = to;
        out.width = 8;
        out.has_start = true;
        out.start = from;
        for (unsigned int i = from; i <= to; i++) out.values.push_back(m_regs[i]);
        return true;
    }
    if (field == "status")   { out.width = 8; out.values.push_back(status()); return true; }
    if (field == "cet")      { out.values.push_back(m_cet ? 1 : 0); return true; }
    if (field == "sleeping") { out.values.push_back(m_sleeping ? 1 : 0); return true; }
    if (field == "idle") {
        unsigned int v = 0;
        for (int c = 0; c < TONES; c++) if (m_ch_quiet[c] >= IDLE_STEPS) v |= 1u << c;
        if (m_dr_quiet >= IDLE_STEPS) v |= 0x20;
        out.width = 8;
        out.values.push_back(v);
        return true;
    }
    if (field == "irqs")     { out.width = 32; out.values.push_back(m_irqs); return true; }
    if (field == "counters") {
        out.width = 32;
        for (int i = 0; i < COUNTERS; i++)
            out.values.push_back(m_cnt[i].state == Vi53Counter::IDLE ? 0 : m_cnt[i].n);
        return true;
    }
    if (field == "outs") {
        unsigned int v = 0;
        for (int i = 0; i < COUNTERS; i++) if (m_cnt[i].out) v |= 1u << i;
        out.width = 8;
        out.values.push_back(v);
        return true;
    }
    out.numeric = false;
    char buf[32];
    if (field == "level") {
        snprintf(buf, sizeof(buf), "%d", (int)std::lround(m_out * 1000));
        out.text = buf;
        return true;
    }
    if (field == "env") {
        out.text.clear();
        for (int c = 0; c < TONES; c++) {
            snprintf(buf, sizeof(buf), "%s%d", c ? " " : "", (int)std::lround(m_v[c][NF] * 1000));
            out.text += buf;
        }
        return true;
    }
    if (field == "peak") {
        // Unlike env, it does not depend on where in the slice a script
        // command landed, which moves with the load of the host: a test
        // reading a decaying envelope gets a few mV more or less each run
        out.text.clear();
        for (int i = 0; i < 7; i++) {
            snprintf(buf, sizeof(buf), "%s%d", i ? " " : "", (int)std::lround(m_peak[i] * 1000));
            out.text += buf;
        }
        return true;
    }
    if (field == "drums") {
        snprintf(buf, sizeof(buf), "%d %d", (int)std::lround(m_d6_e * 1000), (int)std::lround(m_d7_e * 1000));
        out.text = buf;
        return true;
    }
    return ComputerDevice::get_field(field, from, to, out);
}

ComputerDevice * create_agat_yazs(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AgatYazs(im, cd);
}
