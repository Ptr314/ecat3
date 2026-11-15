// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the blitter, header

#pragma once

#include <cstdint>
#include <vector>

class AZBK;
class StateWriter;
class StateReader;

// Блиттер AZBK по описанию MAXIOL (тема 5556, «редакция от 2025-05-26 для
// прошивки 19b1»): пакет до 511 команд по 8 слов из страницы 177272, пуск по
// концу видимой части кадра (начало строки 769) или вручную (разряд 12 при
// уже установленном разряде 14), работа словами в 24-разрядном адресном
// пространстве памяти AZ параллельно процессору.
//
// Команда: 1 - старшие байты адресов (SRC 15-8, DST 7-0); 2 - SRC; 3 - DST;
// 4 - разряды 0 читать SRC, 1 читать DST, 2 NOP, 5-3 операция, 6 обмен
// байтов, 9 зеркало по X, 10 по Y; 5 - ширина в словах минус 1 (мл.) и
// строк (ст.); 6 - приращение DST после строки (мл.); 7 - Y для строк по 256
// слов (DST += Y * 256); 8 - константы (мл. - заливка или прозрачный SRC, ст. -
// прозрачный DST, у операции 5 - прозрачный SRC). Маски байтов - по Verilog
// MAXIOL. DST внутри строки идет на слово вправо (с зеркалом X - влево), после
// строки к нему прибавляется приращение (с зеркалом Y - вычитается): так
// сходятся его примеры (79 = 128 - 49, 177 = 128 + 49). SRC линейный.
//
// Время - подбор под замеры MAXIOL (спрайт 60x60, прошивка 19b1): запись
// слова 12.28 точки 65 МГц (заливка, 2940 спрайтов в секунду), чтение - 1.26
// (копия, 2667), слово, у которого оба байта закрыты маской, не пишется.
// Выборки VGA не вычитаются (со слоями блиттер у MAXIOL на 4-9% медленнее).
class AZBKBlitter
{
public:
    // Цены в сотых долях точки 65 МГц
    unsigned int write_cost = 1228;
    unsigned int read_cost = 126;

    explicit AZBKBlitter(AZBK * board): m_board(board) {}
    void set_board(AZBK * board) { m_board = board; }

    void reset();                                   // B_HALT_L: регистр в 0, все стоит
    unsigned int read_control() const;              // 177270
    void write_control(unsigned int value);
    unsigned int page() const { return m_page; }
    void write_page(unsigned int value) { m_page = value & 017777; }

    void frame_end();                               // начало строки 769: автоматический пуск
    void run(unsigned int dots);                    // работа за столько точек
    bool busy() const { return m_busy; }

    uint64_t words() const { return m_words; }      // слов обработано с включения
    uint64_t ticks() const { return m_ticks; }      // точек в работе

    void save_state(StateWriter &w) const;
    void load_state(const StateReader &r);

private:
    AZBK * m_board;

    unsigned int m_count = 0;       // разряды 0-8 177270
    bool m_manual = false;          // разряд 14
    unsigned int m_page = 0;        // 177272
    bool m_busy = false;            // разряд 15
    bool m_reading = false;         // разряд 9: пакет читается в буфер

    std::vector<uint16_t> m_packet; // прочитанный пакет
    unsigned int m_packet_words = 0;    // уже прочитано слов пакета
    unsigned int m_cmd = 0;         // номер исполняемой команды
    bool m_cmd_loaded = false;
    unsigned int m_op = 0;
    uint32_t m_src = 0, m_dst = 0, m_row_dst = 0;
    unsigned int m_width = 0, m_rows = 0, m_inc = 0;
    unsigned int m_col = 0, m_row = 0;
    unsigned int m_const = 0;

    int64_t m_budget = 0;           // сотые доли точки, которые можно потратить
    uint64_t m_words = 0;
    uint64_t m_ticks = 0;

    void start();
    void stop();
    bool load_command();
    unsigned int step_word();       // одно слово команды, возвращает цену
    uint16_t read_word(uint32_t address) const;
    void write_word(uint32_t address, uint16_t value, unsigned int keep);
};
