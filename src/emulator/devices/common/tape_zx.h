// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ZX Spectrum tape encoding (.tap, simple .tzx)
//
// The numbers were read off the very ROM the Арго loads: ZX.COM copies a stock
// 48K Spectrum ROM into RAM, and its LD-BYTES ($0556) and LD-EDGE-1 ($05E7) are
// byte for byte the original. The tape arrives on bit 6 of a read of port $FE
// (DB FE / 1F / E6 20), and the loader reads the half row $7F, giving up on
// bit 0 of it - SPACE, the BREAK key. So the keyboard and the tape share one
// read, which is why the level goes to the keyboard device rather than to a
// port of its own.
//
// Half period lengths, in Z80 cycles:
//
//   pilot tone   2168, 8063 of them before a header, 3223 before data
//   sync         667, then 735
//   bit 0        855 per half period, bit 1 is 1710
//   pause        about 1000 ms between blocks
//
// The ROM counts ITS OWN cycles, so the waveform is built in cycles of the
// machine from these very numbers and needs no scaling: the Арго runs at
// 3 379 200 Hz rather than 3.5 MHz, and the loader cannot tell.
//
// NOTHING IS RENDERED AHEAD OF TIME. The other encoders here expand a tape into
// an array of levels, which is cheap for them because their grid is one sample
// per bit - a 16K .rko is 32K of samples. The Spectrum has half periods of five
// different lengths, so a uniform grid has to resolve the shortest one and
// starts growing with TIME instead of with data: two to six megabytes for one
// game, and every length rounded to the grid on the way. Instead the blocks are
// kept as they came off disk and Pulser walks them, handing out one half period
// at a time. The lengths are then exact, and the memory is the size of the file.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/devices/common/tape_record.h"

namespace zx_tape
{
    const unsigned int T_PILOT = 2168;
    const unsigned int T_SYNC1 = 667;
    const unsigned int T_SYNC2 = 735;
    const unsigned int T_BIT0  = 855;
    const unsigned int T_BIT1  = 1710;

    const unsigned int PILOT_HEADER = 8063;
    const unsigned int PILOT_DATA   = 3223;

    const unsigned int PAUSE_MS = 1000;

    // A block starting with a byte below $80 is a header, and the ROM gives it
    // the long pilot tone so a human has time to read the name it prints
    inline unsigned int pilot_of(const uint8_t * d, size_t n)
    {
        return (n > 0 && d[0] < 0x80)? PILOT_HEADER : PILOT_DATA;
    }

    // How long the block plays, in cycles. Counted rather than accumulated
    // while playing: the transport wants the length of the whole tape the
    // moment it is loaded
    inline uint64_t block_cycles(const uint8_t * d, size_t n)
    {
        uint64_t t = (uint64_t)pilot_of(d, n) * T_PILOT + T_SYNC1 + T_SYNC2;
        for (size_t i = 0; i < n; i++)
        {
            //Eight bits, each two half periods long
            for (int k = 7; k >= 0; k--)
                t += 2 * (uint64_t)(((d[i] >> k) & 1)? T_BIT1 : T_BIT0);
        }
        return t;
    }

    // Walks one block, half period by half period. The caller flips the line on
    // every step, so only the lengths live here
    struct Pulser
    {
        const uint8_t * data = nullptr;
        size_t          size = 0;

        enum Phase { P_PILOT, P_SYNC1, P_SYNC2, P_DATA, P_DONE };
        Phase        phase = P_DONE;
        unsigned int pilot_left = 0;
        size_t       byte = 0;
        int          bit  = 7;          //Most significant first, as the ROM writes it
        unsigned int half = 0;          //Every bit is two half periods of one length

        void start(const uint8_t * d, size_t n)
        {
            data = d; size = n;
            phase = (n > 0)? P_PILOT : P_DONE;
            pilot_left = pilot_of(d, n);
            byte = 0; bit = 7; half = 0;
        }

        bool done() const { return phase == P_DONE; }

        //Length of the next half period in cycles, 0 once the block is over
        unsigned int next()
        {
            switch (phase)
            {
                case P_PILOT:
                    if (--pilot_left == 0) phase = P_SYNC1;
                    return T_PILOT;
                case P_SYNC1:
                    phase = P_SYNC2;
                    return T_SYNC1;
                case P_SYNC2:
                    phase = (size > 0)? P_DATA : P_DONE;
                    return T_SYNC2;
                case P_DATA:
                {
                    const unsigned int t = ((data[byte] >> bit) & 1)? T_BIT1 : T_BIT0;
                    if (++half == 2)
                    {
                        half = 0;
                        if (--bit < 0) { bit = 7; if (++byte >= size) phase = P_DONE; }
                    }
                    return t;
                }
                default:
                    return 0;
            }
        }
    };

    // .tap: nothing but [length:2 LE][block] over and over. A block is a flag
    // byte, the data, and their XOR - but none of that matters here, the whole
    // block goes to tape as it lies
    inline bool parse_tap(const uint8_t * raw, size_t size, unsigned int clock,
                          std::vector<TapeRecord> &out, std::string &err)
    {
        const uint64_t pause = (uint64_t)PAUSE_MS * clock / 1000;
        size_t p = 0;
        while (p + 2 <= size)
        {
            const size_t n = (size_t)raw[p] | ((size_t)raw[p + 1] << 8);
            p += 2;
            if (n == 0 || p + n > size) { err = "Truncated .tap block"; return false; }
            TapeRecord r;
            r.gap = pause;
            r.data.assign(raw + p, raw + p + n);
            r.units = block_cycles(r.data.data(), r.data.size());
            out.push_back(r);
            p += n;
        }
        if (out.empty()) { err = "Not a ZX tape image"; return false; }
        return true;
    }

    // .tzx: a signature, a version, then blocks with an identifier each. Only
    // the standard speed block ($10) carries a signal; $20 is a pause and the
    // rest listed here are descriptions, which are skipped. Anything else is
    // refused outright - half a tape played and then silence is worse than a
    // refusal, because it looks like a fault in the machine
    inline bool parse_tzx(const uint8_t * raw, size_t size, unsigned int clock,
                          std::vector<TapeRecord> &out, std::string &err)
    {
        if (size < 10 || std::string((const char*)raw, 7) != "ZXTape!" || raw[7] != 0x1A)
        {
            err = "Not a ZX tape image";
            return false;
        }

        //A lead-in before the first block, then whatever each block asks for.
        //Somebody presses Play by hand at a moment of their own choosing, and
        //a tape that opened straight into a pilot tone would depend on that
        //moment
        uint64_t pending = (uint64_t)PAUSE_MS * clock / 1000;
        size_t p = 10;
        while (p < size)
        {
            const uint8_t id = raw[p++];
            if (id == 0x10)
            {
                if (p + 4 > size) { err = "Truncated .tzx block"; return false; }
                const unsigned int ms = (unsigned int)raw[p] | ((unsigned int)raw[p + 1] << 8);
                const size_t n = (size_t)raw[p + 2] | ((size_t)raw[p + 3] << 8);
                p += 4;
                if (n == 0 || p + n > size) { err = "Truncated .tzx block"; return false; }
                TapeRecord r;
                //The pause of a .tzx block follows it, but a tape is read as
                //"silence, then a block", so it is carried to the next one
                r.gap = pending;
                r.data.assign(raw + p, raw + p + n);
                r.units = block_cycles(r.data.data(), r.data.size());
                out.push_back(r);
                pending = (uint64_t)ms * clock / 1000;
                p += n;
            }
            else if (id == 0x20)
            {
                if (p + 2 > size) { err = "Truncated .tzx block"; return false; }
                const unsigned int ms = (unsigned int)raw[p] | ((unsigned int)raw[p + 1] << 8);
                p += 2;
                //A pause of zero means "stop the tape". There is nobody to
                //restart it here, so it becomes an ordinary long silence -
                //rendering a zero would leave the loader waiting for an edge
                //that never comes
                pending += (uint64_t)(ms? ms : PAUSE_MS) * clock / 1000;
            }
            else if (id == 0x30)                                    //Text
            {
                if (p + 1 > size) break;
                p += 1 + raw[p];
            }
            else if (id == 0x31)                                    //Message
            {
                if (p + 2 > size) break;
                p += 2 + raw[p + 1];
            }
            else if (id == 0x32)                                    //Archive info
            {
                if (p + 2 > size) break;
                p += 2 + ((size_t)raw[p] | ((size_t)raw[p + 1] << 8));
            }
            else if (id == 0x33)                                    //Hardware type
            {
                if (p + 1 > size) break;
                p += 1 + (size_t)raw[p] * 3;
            }
            else if (id == 0x35)                                    //Custom info
            {
                if (p + 20 > size) break;
                uint64_t n = 0;
                for (int i = 0; i < 4; i++) n |= (uint64_t)raw[p + 16 + i] << (8 * i);
                p += 20 + (size_t)n;
            }
            else if (id == 0x5A)                                    //Glue
            {
                p += 9;
            }
            else
            {
                const char * hex = "0123456789ABCDEF";
                err = std::string("Unsupported .tzx block $")
                    + hex[(id >> 4) & 0xF] + hex[id & 0xF];
                return false;
            }
        }
        if (out.empty()) { err = "Not a ZX tape image"; return false; }
        return true;
    }

    // Обратное Pulser: полупериоды приходят по одному, а на выходе блоки - те
    // же, что лягут в .tap. Это SAVE: ПЗУ дрыгает разрядом 3 порта $FE из
    // SA-BYTES ($04C2), теми же длительностями, какие потом читает LD-BYTES.
    //
    // Мерить их абсолютными числами нельзя, и вот почему. ВГ75 снимает
    // процессор с шины пачками - восемьдесят пересылок подряд в начале каждой
    // знакоместной строки, - а SA-BYTES считает СВОИ такты. Пачка либо попала
    // в полупериод, либо нет, и один и тот же ноль выходит то 855 тактов, то
    // 1175: разброс в треть, больше любого разумного допуска. Машина этого не
    // замечает, потому что LD-BYTES меряет тем же аршином и с тем же
    // разбросом, а декодер обязан.
    //
    // Поэтому здесь два приема, оба взяты у настоящего загрузчика:
    //   - сначала ловится пилот-тон и по нему берется мерка. Все границы
    //     потом считаются в долях от нее, а не от 2168;
    //   - бит решается по СУММЕ двух полупериодов. Пачка попадает в один из
    //     них, и в сумме ее вес вдвое меньше; вдобавок между половинками бита
    //     машина успевает сделать еще что-то, и первая всегда чуть длиннее.
    // Что остается абсолютным - только конец блока, и тот по тишине.
    const unsigned int LIM_SUM   = T_BIT0 + T_BIT1;             //2565

    // Столько полупериодов подряд считаются пилот-тоном, а не помехой. ПЗУ
    // выдает 3223 даже перед данными, так что запас здесь огромный
    const unsigned int PILOT_LOCK = 64;

    // В какие рамки должен уложиться полупериод, чтобы вообще считаться
    // пилот-тоном, пока мерки еще нет
    const unsigned int PILOT_MIN = T_PILOT / 2;
    const unsigned int PILOT_MAX = T_PILOT * 3;

    // Тишина, после которой блок считается кончившимся. Сам сигнал молчать
    // так долго не может: самый длинный его полупериод - пилот-тон
    const unsigned int SILENCE = 4 * T_PILOT;

    struct Decoder
    {
        enum Phase { D_IDLE, D_PILOT, D_SYNC2, D_DATA };

        Phase        phase = D_IDLE;
        unsigned int lock  = 0;             //Насчитано полупериодов пилот-тона
        uint64_t     sum   = 0;             //Их сумма: из нее и берется мерка
        unsigned int unit  = T_PILOT;       //Измеренный полупериод пилот-тона
        std::vector<uint8_t> bytes;         //Собранный блок
        uint8_t      cur   = 0;
        unsigned int bits  = 0;
        bool         half  = false;         //Первый полупериод бита уже был
        unsigned int first = 0;             //Его длина

        //Заметно короче пилот-тона - это синхроимпульс: 667 и 735 против 2168,
        //и никакой разброс их не сблизит
        bool is_short(unsigned int t) const { return t * 5 < (uint64_t)unit * 3; }
        //Заметно длиннее - это уже не сигнал
        bool is_long(unsigned int t)  const { return t * 2 > (uint64_t)unit * 3; }

        //Пилот-тон - это не «импульс нужной длины», а ряд ОДИНАКОВЫХ импульсов.
        //Так он и ловится: длина берется какая есть, лишь бы соседние сходились
        void pilot_edge(unsigned int t)
        {
            if (t < PILOT_MIN || t > PILOT_MAX) { lock = 0; sum = 0; return; }
            if (lock != 0)
            {
                const unsigned int mean = (unsigned int)(sum / lock);
                const unsigned int d = (t > mean)?(t - mean):(mean - t);
                if (d * 4 > mean) { lock = 0; sum = 0; }
            }
            lock++;
            sum += t;
            if (lock >= PILOT_LOCK)
            {
                unit = (unsigned int)(sum / lock);
                phase = D_PILOT;
            }
        }

        //Автомат что-то слышит: тишина в этот момент и есть конец блока
        bool active() const { return phase != D_IDLE || lock != 0; }

        void clear()
        {
            phase = D_IDLE; lock = 0; sum = 0; unit = T_PILOT;
            cur = 0; bits = 0; half = false; first = 0;
            bytes.clear();
        }

        //Блок кончился. Недобранный байт не байт: блок обрывается на целом,
        //а сами байты остаются в bytes - их забирает тот, кто спрашивал
        bool close()
        {
            const bool got = (phase == D_DATA) && !bytes.empty();
            phase = D_IDLE; lock = 0; sum = 0;
            cur = 0; bits = 0; half = false; first = 0;
            return got;
        }

        //true - блок собран и лежит в bytes
        bool add(unsigned int t)
        {
            switch (phase)
            {
                case D_IDLE:
                    pilot_edge(t);
                    return false;

                case D_PILOT:
                    //Пилот-тон кончается синхроимпульсом, и только им
                    if (is_short(t)) { phase = D_SYNC2; return false; }
                    if (is_long(t)) close();
                    return false;

                case D_SYNC2:
                    //Второй синхроимпульс длиннее первого, но мерка им одна:
                    //различать их незачем, важно что их два
                    if (is_short(t))
                    {
                        phase = D_DATA;
                        bytes.clear(); cur = 0; bits = 0; half = false;
                    }
                    else close();
                    return false;

                default:    //D_DATA
                {
                    //Блок кончается тишиной, а не коротким импульсом: ноль с
                    //пачкой ПДП внутри и синхроимпульс без нее различаются уже
                    //плохо, и строгая нижняя граница рвала бы блок на середине
                    if (is_long(t)) return close();
                    if (!half) { first = t; half = true; return false; }
                    half = false;
                    const unsigned int lim = (unsigned int)((uint64_t)LIM_SUM * unit / T_PILOT);
                    cur = (uint8_t)((cur << 1) | (((first + t) >= lim)?1u:0u));
                    if (++bits == 8) { bytes.push_back(cur); cur = 0; bits = 0; }
                    return false;
                }
            }
        }
    };

    // .tap из готовых блоков: длина и блок, длина сама себя не считает
    inline void build_tap(const std::vector<TapeRecord> &recs, std::vector<uint8_t> &out)
    {
        out.clear();
        for (size_t i = 0; i < recs.size(); i++)
        {
            const std::vector<uint8_t> &d = recs[i].data;
            out.push_back((uint8_t)(d.size() & 0xFF));
            out.push_back((uint8_t)((d.size() >> 8) & 0xFF));
            out.insert(out.end(), d.begin(), d.end());
        }
    }
}
