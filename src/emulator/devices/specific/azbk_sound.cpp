// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): Covox and sound DMA

#include <algorithm>

#include "azbk_sound.h"
#include "azbk.h"
#include "emulator/utils.h"

static const unsigned int R_DMA_CSR = 0177160;
static const unsigned int R_DMA_PG  = 0177162;
static const unsigned int R_DMA_LH  = 0177164;
static const unsigned int R_DMA_LL  = 0177166;
static const unsigned int R_DMA_CUR = 0177170;
static const unsigned int R_COV_L   = 0177200;
static const unsigned int R_COV_R   = 0177202;
static const unsigned int R_COV_M   = 0177204;
static const unsigned int R_COV_8   = 0177206;
static const unsigned int R_CTRL    = 0177212;

// 177160
static const unsigned int DMA_START   = 1;
static const unsigned int DMA_ONESHOT = 2;
static const unsigned int DMA_STOP    = 4;
static const unsigned int DMA_DONE    = 010;
static const unsigned int DMA_STREAM  = 020;

// Таблицы IMA ADPCM (стандарт IMA, как у кодировщика MAXIOL)
static const int16_t IMA_STEP[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t IMA_INDEX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

AZBKSound::AZBKSound(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_ym(this, im, 1, "ym", MODE_W)
{
    addresable_size = 0200000;
    can_read = true;
    can_write = true;
    m_clocked = true;
}

emulator::Result AZBKSound::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_board = dynamic_cast<AZBK*>(im->dm->get_device_by_name(cd->get_parameter("board").value));
    if (m_board == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);
    m_rate = read_confg_value(cd, "rate", false, 44100u);
    if (m_rate == 0) m_rate = 44100;
    return emulator::Result::ok();
}

void AZBKSound::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (!cold) return;
    m_dma_csr = m_dma_page = m_dma_len_hi = m_dma_len_lo = 0;
    m_dma_on = false;
    m_dma_pos = m_dma_bytes = 0;
    m_nibble = 0;
    m_pred[0] = m_pred[1] = m_index[0] = m_index[1] = 0;
    m_acc = 0;
    m_dma_left = m_dma_right = 0;
    m_samples = 0;
    m_hash = 2166136261u;
    m_left = m_right = 0;
    m_control = 0;
    // После включения 177212 = 0: YM2149 (стартовое ПЗУ потом пишет туда
    // выбор SETUP, по умолчанию тоже YM)
    i_ym.change(1);
    sound_changed();
}

//------------------------------------------------------------------ IMA ---

int32_t AZBKSound::ima_step(int32_t &pred, int32_t &index, unsigned int nibble)
{
    const int32_t step = IMA_STEP[index];
    int32_t diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    pred += (nibble & 8) ? -diff : diff;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    index += IMA_INDEX[nibble & 7];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return pred;
}

void AZBKSound::ima_decode(const uint8_t * data, size_t bytes, bool stereo, int16_t * out)
{
    int32_t pred[2] = { 0, 0 }, index[2] = { 0, 0 };
    for (size_t i = 0; i < bytes; i++) {
        if (stereo) {
            *out++ = (int16_t)ima_step(pred[0], index[0], data[i] & 15);
            *out++ = (int16_t)ima_step(pred[1], index[1], data[i] >> 4);
        } else {
            *out++ = (int16_t)ima_step(pred[0], index[0], data[i] & 15);
            *out++ = (int16_t)ima_step(pred[0], index[0], data[i] >> 4);
        }
    }
}

//------------------------------------------------------------------ DMA ---

uint8_t AZBKSound::dma_byte(uint32_t offset) const
{
    const uint64_t a = (uint64_t)m_dma_page * AZBK::PAGE_BYTES + offset;
    return (a < m_board->memory_size()) ? m_board->memory()[a] : 0;
}

void AZBKSound::dma_start()
{
    m_dma_on = true;
    m_dma_pos = 0;
    m_nibble = 0;
    m_dma_bytes = (((m_dma_len_hi & 0377) << 16) | (m_dma_len_lo & 0177777)) * 2;
    m_pred[0] = m_pred[1] = m_index[0] = m_index[1] = 0;
    m_acc = 0;
    m_dma_csr &= ~DMA_DONE;
    const unsigned int rate = (m_dma_csr >> 6) & 7;
    const unsigned int format = (m_dma_csr >> 9) & 7;
    if (rate != 0) note_unsupported("DMA rate " + std::to_string(rate));
    if (format != 0 && format != 4 && format != 5) note_unsupported("DMA format " + std::to_string(format));
}

// Запись 177160: 2 - немедленная остановка, 0 - пуск (если не идет)
void AZBKSound::dma_control(unsigned int value)
{
    m_dma_csr = (m_dma_csr & DMA_DONE) | (value & 07767);
    if (value & DMA_STOP) {
        m_dma_on = false;
        m_dma_csr &= ~DMA_START;
        m_dma_left = m_dma_right = 0;
        sound_changed();
        return;
    }
    if ((value & DMA_START) && !m_dma_on) dma_start();
}

// Очередной отсчет 44100 Гц
void AZBKSound::dma_sample()
{
    if (m_dma_pos >= m_dma_bytes) {
        // Конец: однократный встает с флагом 3, циклический идет сначала
        if (m_dma_csr & DMA_ONESHOT) {
            m_dma_on = false;
            m_dma_csr = (m_dma_csr | DMA_DONE) & ~DMA_START;
            m_dma_left = m_dma_right = 0;
            sound_changed();
            return;
        }
        m_dma_pos = 0;
        m_nibble = 0;
        if ((m_dma_csr & DMA_STREAM) == 0) m_pred[0] = m_pred[1] = m_index[0] = m_index[1] = 0;
        if (m_dma_bytes == 0) return;
    }
    const unsigned int format = (m_dma_csr >> 9) & 7;
    int32_t l = 0, r = 0;
    if (format == 0) {              // PCM16 моно
        const uint8_t lo = dma_byte(m_dma_pos), hi = dma_byte(m_dma_pos + 1);
        l = r = (int16_t)(lo | (hi << 8));
        m_dma_pos += 2;
    } else if (format == 4) {       // IMA моно: младший полубайт первым
        const uint8_t b = dma_byte(m_dma_pos);
        l = r = ima_step(m_pred[0], m_index[0], m_nibble ? (b >> 4) : (b & 15));
        if (m_nibble) m_dma_pos++;
        m_nibble ^= 1;
    } else if (format == 5) {       // IMA стерео: кадр в байте
        const uint8_t b = dma_byte(m_dma_pos);
        l = ima_step(m_pred[0], m_index[0], b & 15);
        r = ima_step(m_pred[1], m_index[1], b >> 4);
        m_dma_pos++;
    } else {
        m_dma_pos += 2;
    }
    m_samples++;
    m_hash = (m_hash ^ (uint32_t)(l & 0xFFFF)) * 16777619u;
    m_hash = (m_hash ^ (uint32_t)(r & 0xFFFF)) * 16777619u;
    if (l != m_dma_left || r != m_dma_right) {
        m_dma_left = l;
        m_dma_right = r;
        sound_changed();
    }
}

void AZBKSound::clock(unsigned int counter)
{
    if (!m_dma_on || m_system_clock == 0) return;
    m_acc += (uint64_t)counter * m_rate;
    while (m_acc >= m_system_clock && m_dma_on) {
        m_acc -= m_system_clock;
        dma_sample();
    }
}

//------------------------------------------------------------ регистры ---

// Covox по чтению не отдает записанное (MAXIOL); что отдает - вопрос Б.6, здесь 0
unsigned int AZBKSound::read_register(unsigned int address) const
{
    switch (address & ~1u) {
    case R_DMA_CSR: return m_dma_csr;
    case R_DMA_PG:  return m_dma_page;
    case R_DMA_LH:  return m_dma_len_hi;
    case R_DMA_LL:  return m_dma_len_lo;
    case R_DMA_CUR: return (unsigned int)((m_dma_page + (m_dma_pos / AZBK::PAGE_BYTES)) & 017777);
    case R_CTRL:    return m_control;
    default:        return 0;
    }
}

void AZBKSound::write_register(unsigned int address, unsigned int value, bool word)
{
    const unsigned int a = address & ~1u;
    switch (a) {
    case R_DMA_CSR: dma_control(value); break;
    case R_DMA_PG:  m_dma_page = value & 017777; break;
    case R_DMA_LH:  m_dma_len_hi = value & 0377; break;
    case R_DMA_LL:  m_dma_len_lo = value & 0177777; break;
    case R_COV_L:   m_left = (int16_t)value; sound_changed(); break;
    case R_COV_R:   m_right = (int16_t)value; sound_changed(); break;
    case R_COV_M:   m_left = m_right = (int16_t)value; sound_changed(); break;
    case R_COV_8:
        // Слово - стерео: младший байт левый, старший правый; байт - моно.
        // 8 бит без знака, середина 0200 (знак - вопрос Б.6)
        if (word) {
            m_left = ((int32_t)(value & 0377) - 0200) << 8;
            m_right = ((int32_t)((value >> 8) & 0377) - 0200) << 8;
        } else
            m_left = m_right = ((int32_t)(value & 0377) - 0200) << 8;
        sound_changed();
        break;
    case R_CTRL:
        m_control = value & 017;
        i_ym.change((m_control & 010) ? 0 : 1);
        break;
    default:        break;
    }
}

unsigned int AZBKSound::get_value(unsigned int address)
{
    const unsigned int v = read_register(address);
    return (address & 1) ? (v >> 8) & 0xFF : v & 0xFF;
}

unsigned int AZBKSound::get_value_word(unsigned int address)
{
    return read_register(address);
}

void AZBKSound::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    const unsigned int a = address & ~1u;
    value &= 0xFF;
    if (a == R_COV_8) {
        write_register(address, (address & 1) ? value << 8 : value, false);
        return;
    }
    const unsigned int old = read_register(a);
    write_register(a, (address & 1) ? ((old & 0xFF) | (value << 8)) : ((old & 0xFF00) | value), true);
}

void AZBKSound::set_value_word(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    write_register(address, value & 0177777, true);
}

//----------------------------------------------------------------- звук ---

static int32_t mix(int32_t a, int32_t b, int64_t amplitude)
{
    int32_t s = a + b;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return (int32_t)((int64_t)s * amplitude / 32768);
}

int32_t AZBKSound::sound_sample(int64_t amplitude)
{
    return mix(m_right, m_dma_right, amplitude);
}

int32_t AZBKSound::sound_sample_left(int64_t amplitude)
{
    return mix(m_left, m_dma_left, amplitude);
}

// Covox and DMA are true stereo: the sound of a video takes both sides
int32_t AZBKSound::sound_sample_mono(int64_t amplitude)
{
    return mix((m_left + m_right) / 2, (m_dma_left + m_dma_right) / 2, amplitude);
}

//---------------------------------------------------------------- снимок ---

void AZBKSound::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("dma_csr", m_dma_csr, 16);
    w.u("dma_page", m_dma_page, 16);
    w.u("dma_length_high", m_dma_len_hi, 16);
    w.u("dma_length_low", m_dma_len_lo, 16);
    w.b("dma_on", m_dma_on);
    w.n("dma_pos", m_dma_pos);
    w.n("dma_bytes", m_dma_bytes);
    w.n("nibble", m_nibble);
    w.n("pred_left", (uint32_t)m_pred[0]);
    w.n("pred_right", (uint32_t)m_pred[1]);
    w.n("index_left", (uint32_t)m_index[0]);
    w.n("index_right", (uint32_t)m_index[1]);
    w.n64("acc", m_acc);
    w.n("dma_left", (uint32_t)m_dma_left);
    w.n("dma_right", (uint32_t)m_dma_right);
    w.n("samples", m_samples);
    w.n("hash", m_hash);
    w.n("left", (uint32_t)m_left);
    w.n("right", (uint32_t)m_right);
    w.u("control", m_control, 8);
}

emulator::Result AZBKSound::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    unsigned int v = 0;
    r.u("dma_csr", m_dma_csr);
    r.u("dma_page", m_dma_page);
    r.u("dma_length_high", m_dma_len_hi);
    r.u("dma_length_low", m_dma_len_lo);
    r.b("dma_on", m_dma_on);
    r.u("dma_pos", m_dma_pos);
    r.u("dma_bytes", m_dma_bytes);
    r.u("nibble", m_nibble);
    if (r.u("pred_left", v)) m_pred[0] = (int32_t)v;
    if (r.u("pred_right", v)) m_pred[1] = (int32_t)v;
    if (r.u("index_left", v)) m_index[0] = std::min<int32_t>((int32_t)v, 88);
    if (r.u("index_right", v)) m_index[1] = std::min<int32_t>((int32_t)v, 88);
    r.n64("acc", m_acc);
    if (r.u("dma_left", v)) m_dma_left = (int32_t)v;
    if (r.u("dma_right", v)) m_dma_right = (int32_t)v;
    r.u("samples", m_samples);
    r.u("hash", m_hash);
    if (r.u("left", v)) m_left = (int32_t)v;
    if (r.u("right", v)) m_right = (int32_t)v;
    r.u("control", m_control);
    if (m_index[0] < 0) m_index[0] = 0;
    if (m_index[1] < 0) m_index[1] = 0;
    sound_changed();
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> AZBKSound::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"dma",       "0177160 DMA control",                          false});
    r.push_back({"dma_page",  "0177170: page the DMA reads",                   false});
    r.push_back({"samples",   "DMA samples played since power-on",             false});
    r.push_back({"hash",      "Hash of the DMA samples played (left, right)",  false});
    r.push_back({"output",    "DMA output left and right, 16 bits",            false});
    r.push_back({"control",   "0177212 sound control",                         false});
    r.push_back({"covox",     "Covox left and right, 16 bits",                 false});
    return r;
}

bool AZBKSound::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "dma")       { out.values.push_back(m_dma_csr); return true; }
    if (field == "dma_page")  { out.values.push_back(read_register(R_DMA_CUR)); return true; }
    if (field == "control")   { out.values.push_back(m_control); return true; }
    if (field == "output")    { out.values.push_back((unsigned int)m_dma_left & 0xFFFF); out.values.push_back((unsigned int)m_dma_right & 0xFFFF); return true; }
    if (field == "covox")     { out.values.push_back((unsigned int)m_left & 0xFFFF); out.values.push_back((unsigned int)m_right & 0xFFFF); return true; }
    out.width = 32;
    if (field == "samples")   { out.values.push_back(m_samples); return true; }
    if (field == "hash")      { out.values.push_back(m_hash); return true; }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_azbk_sound(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AZBKSound(im, cd);
}
