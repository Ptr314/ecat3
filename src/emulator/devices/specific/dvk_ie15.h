// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 15ИЭ-00-013 terminal (ДВК-1, ДВК-2): its logic block and 15ВВВ keyboard

#pragma once

#include <atomic>
#include <deque>

#include "emulator/core.h"
#include "emulator/thread_compat.h"
#include "emulator/devices/common/mapkeyboard.h"

class IE15IO;
class IE15Display;

// Клавиатура 15ВВВ-97-006 терминала 15ИЭ. Коды - те же, что у Ириши
// (data/15vvv.keys, ПЗУ-шифратор 15bbb.rt5), только уходят они не в порт
// машины, а терминалу (ie15-io). Своё здесь - пять переключателей режимов
// терминала, у которых кода нет (ДУП, ЛИН, РЕД, ПЧ, СДВ, KEY_ROLE_TOGGLE): их
// читает прошивка терминала, - и восемь ламп над клавиатурой (MAME ie15.cpp):
// ЛАТ - регистр, НР - защёлка регистра, ПРД горит всегда, остальные пять -
// переключатели. Клавиши блока курсора отмечены линией ДК: стрелки и «домой»
// превращает в ESC-последовательности прошивка, $08 «домой» и $08 СУ+H
// различает только эта линия.
class IE15Keyboard: public MapKeyboard
{
private:
    IE15IO * m_terminal = nullptr;
    bool m_dk = false;
    // Переключатели разрядами SW_*: прошивка опрашивает их постоянно, а
    // toggled() берёт мьютекс и ищет строку
    std::atomic<unsigned> m_switches{0};

protected:
    void toggles_changed() override;
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

    void set_terminal(IE15IO * t) { m_terminal = t; }
    enum { SW_DUP = 1, SW_LIN = 2, SW_RED = 4, SW_PCH = 8, SW_SDV = 16 };
    bool switch_on(unsigned sw) const { return (m_switches.load(std::memory_order_relaxed) & sw) != 0; }
    // Регистр шифратора ведёт флаг 4 прошивки (MAME set_ruslat): она меняет
    // его и по SO/SI от ЭВМ. Из потока эмуляции
    void follow_ruslat(bool lat) { if (rus_mode == lat) set_rus(!lat); }
};

// Изображение терминала: поверхность 800 x 275 (25 строк по 11 линий,
// знакоместо 10 x 11, знак 7 x 8 из знакогенератора: 256 знаков по 8 байт,
// КОИ-8, левая точка - разряд 7). Своей развёртки здесь нет: линии рисует
// ie15-io в тот момент, когда луч их проходит (draw_line), из потока
// эмуляции, и он же сообщает о конце кадра.
class IE15Display: public GenericDisplay
{
private:
    Memory * m_font_rom = nullptr;
    std::vector<uint8_t> m_font;
    uint32_t m_colors[2] = {0, 0};
    uint8_t m_palette[2][3] = {{0, 0, 0}, {0, 255, 0}};
    const uint8_t * m_vram = nullptr;

    // С какого адреса показана каждая из 25 строк последнего кадра, _FFFF -
    // строка погашена; для поля text
    unsigned int m_row_addr[25];

protected:
    void render_all(bool force_render) override;

public:
    static const unsigned WIDTH = 800;
    static const unsigned ROWS = 25;
    static const unsigned CELL_H = 11;
    static const unsigned HEIGHT = ROWS * CELL_H;

    IE15Display(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void set_renderer(VideoRenderer &vr) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    bool own_frames() const override { return true; }

    // Линия y растра (0 - верхняя линия служебной строки): 80 знаков видеопамяти
    // с адреса offset, линия знакоместа line (0-10). blank - линия погашена
    // целиком; cursor_on - на линиях 8-10 показывается то, что лежит по адресу
    // (маркер), иначе они тёмные; ctrl_on - управляющие коды видны (мигают)
    void draw_line(unsigned y, unsigned offset, unsigned line, bool blank, bool cursor_on, bool ctrl_on);
    void end_frame();
    void set_vram(const uint8_t * vram) { m_vram = vram; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// Блок логики терминала вокруг МПУ (ie15-cpu): порты 00-17 и флаги 20-27 его
// маппера. Карта портов - MAME ie15.cpp, устройство плат - ТО 15ИЭ (6.1):
//
//   00  чтение - байт ЗУ, запись - байт в ЗУ (пишет второй OTA 00 из пары),
//       после обращения адрес ЗУ растёт на 1
//   01  чтение - принят байт по линии (0 - есть)
//   02  запись - адрес ЗУ, разряды 4-11;  03 - разряды 0-3 (из старших 4)
//   04  запись - адрес ЗУ + 1;  05 - адрес ЗУ - 1
//   06  чтение - принятый байт, запись - байт в линию
//   10  чтение - передатчик свободен (200), запись - звонок
//   11  код клавиши;  12 - РЕД (200 - не «Редактирование»)
//   13  клавиша нажата (0 - есть код);  14 чтение - СДВ (установка), запись -
//       скорость линии: 0 - 9600 ... 7 - 75 бод
//   15  чтение - ДК (клавиша блока курсора), запись - код клавиши взят
//   16  ДУП;  17 - ЛИН
//   20  флаг 0: запись - РИН (показ), чтение - строчный гасящий импульс
//   21  флаг 1: запись - маркер под этой строкой, чтение - МСВ (линии 8-10)
//   22  флаг 2: запись - длинный звонок, чтение - не ОКРН (не кадровое гашение)
//   23  флаг 3: запись - служебная строка погашена
//   24  флаг 4: РУС/ЛАТ (1 - ЛАТ)
//   27  флаг 7: ФЛ7, семибитные коды (запоминается, не моделируется)
//
// Развёртка - в тактах МПУ (3,08 МГц, точка 15,4 МГц, такт - 5 точек): линия
// 200 тактов, из них 40 - гашение; кадр 28 строк по 11 линий = 61600 тактов,
// 50 Гц. Видимы 25 строк со 2-й, кадровое гашение (ОКРН) - 3 строки. Линия
// рисуется, когда кончается её строчное гашение, с того адреса, который
// прошивка к этому моменту загрузила в ЗУ (при показе адрес начала линии
// идёт за адресом ЗУ). Так устроено в MAME; там же признак маркерной линии -
// у следующей линии, а не у текущей.
//
// Линия к ЭВМ - байтовая, как у dl11 с КСМ: ~txd в конце времени символа
// (10 посылок на выбранной скорости), ~rxd - байт от ЭВМ, который приёмник
// берёт сразу (не забранный прошлый затирается, это переполнение).
class IE15IO: public AddressableDevice
{
private:
    Interface i_txd;
    Interface i_rxd;

    IE15Display * m_display = nullptr;
    IE15Keyboard * m_keyboard = nullptr;
    Memory * m_vram_dev = nullptr;
    uint8_t * m_vram = nullptr;

    // ЗУ
    unsigned int m_ptr = 0;             // адрес ЗУ (счётчик)
    unsigned int m_line_ptr = 0;        // адрес, с которого луч берёт линию
    bool m_latch = false;               // первый OTA 00 из пары

    // Флаги
    bool m_video = false;               // 0: РИН
    bool m_cursor = false;              // 1: маркер
    bool m_long_beep = false;           // 2
    bool m_status_off = false;          // 3: служебная строка погашена
    bool m_lat = false;                 // 4: ЛАТ
    bool m_fl7 = false;                 // 7

    // Развёртка
    unsigned int m_pos = 0;             // такт в линии
    unsigned int m_line = 0;            // линия растра
    bool m_line_drawn = false;
    bool m_marker = false;              // МСВ следующей линии
    unsigned int m_frame = 0;

    // Линия
    uint8_t m_rx_char = 0;
    bool m_rx_full = false;
    uint8_t m_tx_char = 0;
    bool m_tx_busy = false;
    uint64_t m_tx_left = 0;
    unsigned int m_speed = 0;           // код скорости, 0 - 9600
    std::deque<uint8_t> m_feed;         // байты команды receive
    uint64_t m_feed_left = 0;

    // Клавиатура: код ждёт, пока прошивка не возьмёт прошлый
    uint8_t m_kb_code = 0;
    bool m_kb_dk = false;
    bool m_kb_ready = false;
    std::deque<unsigned int> m_key_queue;   // код, разряд 8 - ДК; из потока окна
    compat_mutex m_key_mutex;
    std::atomic<unsigned> m_keys_waiting{0};    // без мьютекса на каждом такте

    unsigned int m_received = 0;
    unsigned int m_sent = 0;
    unsigned int m_overruns = 0;
    unsigned int m_bells = 0;

    uint64_t char_ticks() const;
    void receive(uint8_t c);
    void scanline();
    unsigned int read_flag(unsigned int n);
    void write_flag(unsigned int n, bool v);

public:
    IE15IO(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force = false) override;

    // Код с клавиатуры, из любого потока; dk - клавиша блока курсора
    void key_code(uint8_t c, bool dk);

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_ie15_io(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_ie15_display(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_ie15_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
