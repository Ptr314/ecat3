// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): VGA timebase, video registers, frame timer, header

#pragma once

#include <cstdint>

#include "emulator/core.h"
#include "azbk_blitter.h"

class AZBK;
class AZBKDisplay;

// По чему рисуется строка: регистры, как они стоят для нее
struct AZBKLineState {
    unsigned int mode;          // 177230, защелкнутый на кадр
    unsigned int pages[3];      // слои 0 (верх), 1, 2
    unsigned int vscroll[3];
    unsigned int hscroll[3];
    unsigned int legacy_pal;    // палитра 4-цветного режима из 177662
    bool quarter;               // 177664 бит 9 = 0: видна четверть экрана БК
};

// Видеоконтроллер AZBK: развертка VGA 1024x768@60 (65 МГц, строка 1344
// точки, кадр 806 строк - таблица VESA у MAXIOL, тема 5556), регистры режима,
// слоев, скроллов и палитры (177230-177256), кадровый таймер вектора 100
// (60 Гц от кадра VGA или 48.8 Гц делителем 65 МГц - разряды 2-3 177346),
// регистры блиттера (177270/177272) и LFSR 177550.
//
// Запись программы в 177662/177664 доходит и сюда (through): старший байт
// 177662 задает палитру 4-цветного режима и страницу слоя 0 (034 или 4), а
// 177664 - вертикальный скролл слоя 0 и «расширенную память» (бит 9).
//
// Устройство тактируется 1:1 с процессором и само пересчитывает его такты в
// точки 65 МГц (frequency). Кадровое прерывание - импульс длиной в строку на
// выходе irq (1 - запрос), начиная со строки 769.
//
// Параметры:
//   board     - устройство azbk
//   frequency - частота точек, Гц (65 МГц)
class AZBKVga: public AddressableDevice
{
public:
    static const unsigned int DOTS_PER_LINE = 1344;     // 1024 + 24 + 136 + 160
    static const unsigned int LINES = 806;              // 768 + 3 + 6 + 29
    static const unsigned int VISIBLE_LINES = 768;
    static const unsigned int IRQ_LINE = 769;           // кадровое прерывание, пуск блиттера
    static const unsigned int DOTS_PER_FRAME = DOTS_PER_LINE * LINES;
    static const unsigned int DIV48 = 1331200;          // 65 МГц / 1331200 = 48.8 Гц
    static const unsigned int PALETTE_SIZE = 338;

    // Длины рулона в словах, поле 15-12 177230 (больше 11 - как 11)
    static const unsigned int ROLL_WORDS[12];

    unsigned int mode_reg() const { return m_mode; }

private:
    AZBK * m_board = nullptr;
    MemoryMapper * m_mapper = nullptr;
    AZBKDisplay * m_display = nullptr;
    Interface i_irq;

    uint64_t m_dot_freq = 65000000;
    uint64_t m_acc = 0;                 // остаток: такты ЦП * частота точек
    uint64_t m_ticks = 0;               // точки с включения
    unsigned int m_dot = 0;             // точка внутри кадра
    unsigned int m_div48 = 0;           // делитель 48.8 Гц
    unsigned int m_irq_left = 0;        // точек до снятия запроса
    uint64_t m_frames = 0;
    uint64_t m_irqs = 0;

    // Регистры
    unsigned int m_mode = 012100;       // 177230
    unsigned int m_page[3]{};           // слои 0 (верх), 1, 2: 177232, 177240, 177242
    unsigned int m_vscroll[3]{};        // слои 0, 1, 2: 177250, 177246, 177244
    unsigned int m_hscroll[3]{};        // слои 0, 1, 2: 177252, 177254, 177256
    unsigned int m_pal_index = 0;       // 177234
    uint16_t m_palette[PALETTE_SIZE]{}; // R5G5B5
    unsigned int m_legacy_pal = 0;      // разряды 8-11 177662
    bool m_extended = false;            // 177664 бит 9 = 0: видна четверть экрана
    AZBKBlitter m_blit{nullptr};        // 177270, 177272
    // Блиттер, пущенный записью процессора, работает с момента записи, а не с
    // начала команды: такты команды, в которой он встал, ему не достаются
    bool m_blit_was_busy = false;
    bool m_blit_hold = false;
    unsigned int m_halts_seen = 0;      // низкий HALT сбрасывает блиттер
    uint32_t m_palette_version = 0;     // растет с каждой записью палитры

    // Защелкнуто в начале кадра: режим, вертикальные скроллы, четверть
    // экрана; с разрядом 11 режима - и страницы с горизонтальными скроллами
    unsigned int m_f_mode = 012100;
    unsigned int m_f_pages[3]{};
    unsigned int m_f_vscroll[3]{};
    unsigned int m_f_hscroll[3]{};
    bool m_f_quarter = false;
    void latch_frame();

    // LFSR 177550: 128 бит, шаг 65 МГц. Отводы программе не видны - новое
    // слово на каждое обращение; здесь xorshift128 и время в точках
    uint32_t m_lfsr[4]{};

    void advance(unsigned int dots);
    void frame_event();
    void line_event(unsigned int line);
    void raise_irq();
    // Точек до события таймера (lines - и до начала следующей строки)
    unsigned int dots_to_event(bool lines) const;

    unsigned int read_register(unsigned int address, bool direct);
    void write_register(unsigned int address, unsigned int value, bool word);

public:
    AZBKVga(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    unsigned int clocks_to_event() const override;
    bool has_clock_events() const override { return true; }

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    static void default_palette(uint16_t * palette);

    // Для экрана
    void attach_display(AZBKDisplay * display) { m_display = display; }
    AZBK * board() const { return m_board; }
    void line_state(AZBKLineState &s) const;
    const uint16_t * palette() const { return m_palette; }
    uint32_t palette_version() const { return m_palette_version; }

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_azbk_vga(InterfaceManager *im, EmulatorConfigDevice *cd);
