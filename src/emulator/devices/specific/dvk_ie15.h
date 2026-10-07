// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 15ИЭ-00-013 terminal (ДВК-1, ДВК-2) and its 15ВВВ keyboard

#pragma once

#include <deque>

#include "emulator/core.h"
#include "emulator/thread_compat.h"
#include "emulator/devices/common/mapkeyboard.h"

class IE15Terminal;

// Клавиатура 15ВВВ-97-006 терминала 15ИЭ. Коды - те же, что у Ириши
// (data/15vvv.keys, ПЗУ-шифратор 15bbb.rt5), только уходят они не в порт
// машины, а терминалу. Своё здесь - пять переключателей режимов терминала,
// у которых кода нет (ДУП, ЛИН, РЕД, ПЧ, СДВ, KEY_ROLE_TOGGLE), и восемь ламп
// над клавиатурой (MAME ie15.cpp): ЛАТ - регистр, НР - защёлка регистра,
// ПРД горит всегда, остальные пять - переключатели.
class IE15Keyboard: public MapKeyboard
{
private:
    IE15Terminal * m_terminal = nullptr;
    // Код идёт от блока курсора (линия ДК клавиатуры): $08 «домой» и $08
    // СУ+H - один код, отличает их только эта линия
    bool m_dk = false;

protected:
    void send_key(unsigned int value, bool alt = false) override;
    void send_key_id(const std::string &id, bool press) override;
    bool port_required() const override { return false; }

public:
    IE15Keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cool) override;
    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;
    std::vector<Indicator> indicators() const override;

    void set_terminal(IE15Terminal * t) { m_terminal = t; }
    bool online() const { return toggled("key_lin"); }   // ЛИН: на линии
    bool duplex() const { return toggled("key_dup"); }   // ДУП: эхо даёт ЭВМ
};

// Терминал 15ИЭ-00-013. У настоящего - свой процессор на рассыпной логике с
// прошивкой (в docs-external/DVK/15ИЭ лежат её дампы, MAME гоняет их на своём
// ядре ie15); здесь его поведение написано в коде: почти VT52 (без Hold
// Screen и графического набора, MAME), второй набор знаков по SO/SI -
// кириллица КОИ-7 Н2. Знаки рисует настоящий знакогенератор (chargen-15ie.bin:
// 256 знаков по 8 байт, КОИ-8, левая точка - разряд 7), знакоместо 10 x 11,
// знак 7 x 8, 80 x 24.
//
// Линия к ДВК - байтовая, как у dl11 с КСМ: ~txd в конце времени символа
// (baud, 9600), ~rxd - байт от ЭВМ. Команды: ВК, ПС, ЗБ (08), ГТ (09), ЗВ
// (07, считается в bells), SO/SI, ESC A/B/C/D/H/J/K, ESC Y строка столбец,
// ESC Z (ответ ESC / K), ESC = / > / F / G принимаются и ничего не меняют.
// Строка не переносится: знак за 80-м столбцом пишется в последний, как у
// VT52. Стрелки ($1C/$1D/$19/$1A) и «домой» ($08) блока курсора терминал
// превращает в ESC A/B/C/D/H; тот же код с основного поля (СУ+H, ЗБ хоста)
// уходит как есть - у клавиатуры их различает линия ДК.
//
// ЛИН выключен - терминал «местный»: клавиши идут на экран, линия молчит,
// принятое не показывается. ДУП выключен - полудуплекс: набранное ещё и
// показывается самим терминалом. При включении питания оба включены.
class IE15Terminal: public GenericDisplay
{
private:
    Interface i_txd;
    Interface i_rxd;

    Memory * m_font_rom = nullptr;
    IE15Keyboard * m_keyboard = nullptr;
    std::vector<uint8_t> m_font;
    uint32_t m_colors[2] = {0, 0};
    uint8_t m_palette[2][3] = {{0, 0, 0}, {0, 255, 0}};

    static const unsigned COLUMNS = 80;
    static const unsigned ROWS = 24;
    uint8_t m_screen[ROWS][COLUMNS];    // номера знаков знакогенератора
    unsigned m_row = 0, m_col = 0;
    bool m_shift_out = false;           // SO: кириллица
    unsigned m_esc = 0;                 // 0, ESC, ESC Y (строка), ESC Y (столбец)
    unsigned m_esc_row = 0;

    // Передача в ЭВМ: очередь и время символа
    std::deque<uint8_t> m_tx_queue;
    unsigned int m_baud = 9600;
    uint64_t m_char_ticks = 1;
    uint64_t m_tx_left = 0;

    unsigned int m_frame_ticks = 1;
    unsigned int m_ticks = 0;
    unsigned int m_frame = 0;
    unsigned int m_bells = 0;
    unsigned int m_received = 0;
    unsigned int m_sent = 0;

    void put_char(uint8_t c);
    void process(uint8_t c);
    void scroll();
    void clear_screen(unsigned from_row, unsigned from_col);
    void transmit(uint8_t c);
    void keyboard_byte(uint8_t c, bool dk);

    // Коды с клавиатуры: окно жмёт клавиши из своего потока, а экран и линия
    // принадлежат потоку эмуляции - коды ждут здесь до clock()
    std::deque<unsigned int> m_key_queue;     // код, разряд 8 - ДК
    compat_mutex m_key_mutex;

protected:
    void render_all(bool force_render) override;

public:
    IE15Terminal(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void set_renderer(VideoRenderer &vr) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    // Код с клавиатуры, из любого потока; dk - клавиша блока курсора
    void key_code(uint8_t c, bool dk);

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_ie15_terminal(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_ie15_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
