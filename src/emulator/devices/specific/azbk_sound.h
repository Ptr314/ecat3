// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): Covox and sound DMA, header

#pragma once

#include <cstdint>

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"

class AZBK;

// Звук AZBK (MAXIOL, тема 5556): Covox 16 бит стерео (177200-177206),
// управление перехватами 177212 и звуковой DMA (177160-177170: PCM16 моно,
// IMA ADPCM моно и стерео, 44100 Гц, из памяти AZ).
//
// Covox и DMA - стереоисточник, его подмешивает динамик БК (mix). Они
// складываются (общий ли у них ЦАП, MAXIOL не описал). DMA берет отсчеты по
// эмулируемому времени; прерываний у него нет. Формат IMA - по кодировщику
// MAXIOL: стандартный IMA ADPCM, состояние с нуля, в моно два отсчета в байте
// (младший полубайт первый), в стерео кадр в байте (младший полубайт левый).
//
// Пара AY платы (177172-177175) - два устройства ay8910 в карте памяти
// (BK-0011M-azbk.ext); их тип - разряд 3 177212 (0 - YM2149, 1 - AY-3-8910),
// он выходит на линию ym (1 - YM2149). Перехваты 177714 и 177716 не
// моделируются: 177714 остается за звуковой приставкой БК, динамик БК звучит
// сам; остальные разряды 177212 только хранятся.
//
// Параметры:
//   board - устройство azbk
//   rate  - частота отсчетов DMA, Гц (44100)
class AZBKSound: public AddressableDevice, public SoundSource
{
private:
    AZBK * m_board = nullptr;
    Interface i_ym;                     // 1 - пара AY играет как YM2149
    unsigned int m_rate = 44100;

    // DMA
    unsigned int m_dma_csr = 0;         // 177160
    unsigned int m_dma_page = 0;        // 177162
    unsigned int m_dma_len_hi = 0;      // 177164
    unsigned int m_dma_len_lo = 0;      // 177166
    bool m_dma_on = false;
    uint32_t m_dma_pos = 0;             // байт от начала
    uint32_t m_dma_bytes = 0;           // длина в байтах
    unsigned int m_nibble = 0;          // IMA моно: второй полубайт байта
    int32_t m_pred[2]{};                // IMA: отсчет и номер шага по каналам
    int32_t m_index[2]{};
    uint64_t m_acc = 0;                 // такты * частота отсчетов
    int32_t m_dma_left = 0;
    int32_t m_dma_right = 0;
    uint32_t m_samples = 0;
    uint32_t m_hash = 2166136261u;      // отсчеты DMA для скриптов

    // Covox и управление
    int32_t m_left = 0;
    int32_t m_right = 0;
    unsigned int m_control = 0;         // 177212

    unsigned int read_register(unsigned int address) const;
    void write_register(unsigned int address, unsigned int value, bool word);
    void dma_control(unsigned int value);
    void dma_start();
    void dma_sample();
    uint8_t dma_byte(uint32_t offset) const;
    static int32_t ima_step(int32_t &pred, int32_t &index, unsigned int nibble);

public:
    AZBKSound(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    int32_t sound_sample(int64_t amplitude) override;
    int32_t sound_sample_left(int64_t amplitude) override;
    int32_t sound_sample_mono(int64_t amplitude) override;
    bool sound_stereo() override { return true; }
    bool sound_volatile() override { return false; }

    // Для самопроверки: IMA ADPCM как у кодировщика MAXIOL
    static void ima_decode(const uint8_t * data, size_t bytes, bool stereo, int16_t * out);

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_azbk_sound(InterfaceManager *im, EmulatorConfigDevice *cd);
