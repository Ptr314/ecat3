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
// переключатели. Регистр РУС/ЛАТ (и его лампу) ведёт только прошивка флагом 4:
// клавиши РУС и ЛАТ в рабочем режиме (SA1 в 1-2) - обычные клавиши основного
// поля, шлют $0E и $0F и повторяются. Клавиши блока курсора отмечены линией ДК: стрелки и «домой»
// превращает в ESC-последовательности прошивка, $08 «домой» и $08 СУ+H
// различает только эта линия.
//
// Терминалу уходят и нажатие, и отпускание: флаг готовности клавиатуры
// держится, пока клавиша нажата (IE15IO::key_press()). Клавиша опознаётся
// по метке: код хоста или имя на рисунке.
class IE15Keyboard: public MapKeyboard
{
private:
    IE15IO * m_terminal = nullptr;
    bool m_dk = false;
    std::string m_token;            // клавиша, код которой сейчас уходит в send_key()
    std::string m_caps_id;          // РУС или ЛАТ, которую держит РУС/ЛАТ хоста
    // Клавиши хоста приходят из потока окна, TYPE и вставка - из потока
    // эмуляции, а метка клавиши и ДК здесь одни на клавиатуру: нажатия идут по
    // очереди. Внутри очереди - только send_id(), не send_key_id()
    compat_mutex m_turn;
    void send_id(const std::string &id, bool press);
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
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

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
    bool m_fl7 = false;             // ФЛ7 при показе последней строки, для поля text

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

    // Знакоместо линии, как его застал луч: код из ЗУ и что с ним делает ГС
    static const uint16_t CELL_SHOW = 0x100;    // знак показывается (иначе тёмное место)
    static const uint16_t CELL_CTRL = 0x200;    // служебные знаки видны (мигают)
    static const uint16_t CELL_FL7 = 0x400;     // ФЛ7: вместо разряда 7 кода - разряд 5

    // Линия y растра (0 - верхняя линия служебной строки), линия знакоместа
    // line (0-10): 80 знакомест cells. row_addr - адрес ЗУ первого знака, _FFFF -
    // он не показан; для поля text, берётся с линии 0 строки
    void draw_line(unsigned y, unsigned line, const uint16_t * cells, unsigned row_addr);
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
//   07  запись - разрыв линии (не моделируется)
//   10  чтение - передатчик свободен (200), запись - звонок
//   11  код клавиши;  12 - РЕД (200 - не «Редактирование»)
//   13  флаг готовности клавиатуры (0 - есть код);  14 чтение - СДВ
//       (установка), запись - скорость линии: 0 - 9600 ... 7 - 75 бод
//   15  чтение - ДК (клавиша блока курсора), запись - сброс флага готовности
//   16  ДУП;  17 - ЛИН
//   20  флаг 0: запись - РИН (показ), чтение - строчный гасящий импульс
//   21  флаг 1: запись - маркер под этой строкой, чтение - МСВ (линии 8-10)
//   22  флаг 2: запись - 1 короткий звонок, 0 длинный; чтение - не ОКРН (не
//       кадровое гашение)
//   23  флаг 3: запись - служебная строка погашена, чтение - ФЛ7
//   24  флаг 4: РУС/ЛАТ (1 - ЛАТ)
//   27  флаг 7: ФЛ7 - КОИ-7 Н2: знакогенератор получает вместо разряда 7
//       кода разряд 5, и строчные латинские показываются заглавными русскими
//
// Флаг готовности клавиатуры (ТО 6.1.5.2; Alex_K, zx-pk.ru, тема 36791):
// ставится при нажатии и, пока клавиша нажата дольше секунды, - с частотой
// 15 Гц; снимают его запись в порт 15 и отпускание клавиши. Счётчик
// клавиатуры стоит на нажатой клавише, вторая нажатая видна, когда отпущена
// первая. Автоповтор включает прошивка (разряд 2 группы II): при нём она сама
// снимает флаг, взяв код, без него ждёт отпускания - до этого новое нажатие
// не принимается. Нажатие короче 40 мс (щелчок по рисунку, клавиша
// сценария без паузы) держится 40 мс: прошивка опрашивает клавиатуру раз в
// кадр. После отпускания счётчик обходит матрицу (6,4 мс) и только потом
// останавливается на следующей нажатой.
//
// Звонок (ИВВ, ТО 6.1.5.4): 2400 Гц на ~beep; длительность задаёт флаг 2 в
// момент записи в порт 10 - 0,15 с или 0,4 с (замер с машины, тема 20744).
//
// Развёртка - в тактах МПУ (3,08 МГц, точка 15,4 МГц, такт - 5 точек): линия
// 200 тактов, из них 40 - гашение; кадр 28 строк по 11 линий = 61600 тактов,
// 50 Гц. Видимы 25 строк со 2-й, кадровое гашение (ОКРН) - 3 строки. Признак
// маркерной линии - у следующей линии, а не у текущей (как в MAME).
//
// Знакоместо - 2 такта, 80 знаков с 40-го такта линии. Счётчик адреса ЗУ один:
// при показе (РИН, флаг 0) ЗУ само берёт из него знак за знаком и наращивает
// его, поэтому прошивка в каждом строчном гашении загружает адрес начала
// следующей линии заново. Знакоместо рисуется таким, каким его застал луч:
// перед всем, что меняет картинку (флаги 0, 1, 3, 7, адрес и обращения к ЗУ),
// луч догоняется до текущего такта (video_sync()). Так выходит «снег» (Titus,
// zx-pk.ru, тема EmuStudio): чистя строку, прошивка гасит показ, и если работа
// не уложилась в маркерные линии, часть следующей линии тёмная, а после
// включения показа без новой загрузки адреса луч берёт знаки с адреса, где
// прошивка писала.
//
// Линия к ЭВМ - байтовая, как у dl11 с КСМ: ~txd в конце времени символа
// (10 посылок на выбранной скорости), ~rxd - байт от ЭВМ, который приёмник
// берёт сразу (не забранный прошлый затирается, это переполнение).
class IE15IO: public AddressableDevice
{
private:
    Interface i_txd;
    Interface i_rxd;
    Interface i_beep;

    IE15Display * m_display = nullptr;
    IE15Keyboard * m_keyboard = nullptr;
    Memory * m_vram_dev = nullptr;
    uint8_t * m_vram = nullptr;

    // ЗУ
    unsigned int m_ptr = 0;             // адрес ЗУ (счётчик, при показе его ведёт и луч)
    bool m_latch = false;               // первый OTA 00 из пары

    // Флаги
    bool m_video = false;               // 0: РИН
    bool m_cursor = false;              // 1: маркер
    bool m_short_beep = false;          // 2
    bool m_status_off = false;          // 3: служебная строка погашена
    bool m_lat = false;                 // 4: ЛАТ
    bool m_fl7 = false;                 // 7

    // Развёртка
    unsigned int m_pos = 0;             // такт в линии
    unsigned int m_line = 0;            // линия растра
    bool m_line_drawn = false;          // строчное гашение этой линии кончилось
    bool m_marker = false;              // МСВ следующей линии
    bool m_line_on = false;             // идёт видимая линия, знаки собираются в m_cells
    unsigned int m_draw_x = 0;          // знакомест этой линии, которые луч уже прошёл
    unsigned int m_row_start = _FFFF;   // адрес первого из них, _FFFF - не показан
    uint16_t m_cells[80];
    bool m_cells_on = false;            // на линии были знакоместа с показом
    bool m_cells_off = false;           // и без него
    unsigned int m_snow = 0;            // линий, где показ менялся посреди
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

    // Звонок
    uint64_t m_beep_left = 0;           // тактов звучания
    uint64_t m_beep_phase = 0;          // до полупериода, в долях 1/4800 такта
    bool m_beep_level = false;

    // Клавиатура. События приходят из потока окна (и сценария) в очередь, на
    // клавиатуру они ложатся в clock()
    struct KeyEvent {
        std::string token;
        uint8_t code;
        bool dk;
        bool press;
    };
    struct HeldKey {
        std::string token;
        uint8_t code;
        bool dk;
        bool released;                  // отпущена, но ещё держится 40 мс
        uint64_t held;                  // тактов с нажатия
    };
    std::deque<KeyEvent> m_key_events;
    compat_mutex m_key_mutex;
    std::atomic<unsigned> m_events_waiting{0};  // без мьютекса на каждом такте
    std::vector<HeldKey> m_held;        // в порядке нажатия; первая - под счётчиком
    std::atomic<unsigned> m_keys_held{0};       // их число, для поля keys
    bool m_kb_on = false;               // счётчик стоит на первой
    uint64_t m_kb_time = 0;             // тактов, как он на ней остановился
    uint64_t m_kb_next = 0;             // когда следующий строб автоповтора
    uint64_t m_scan_left = 0;           // после отпускания: тактов до новой остановки
    uint32_t m_min_hold_clock = 0;      // частота, для которой посчитан m_min_hold
    uint64_t m_min_hold = 0;            // 40 мс в тактах: меньше клавиша не держится
    uint8_t m_kb_code = 0;
    bool m_kb_dk = false;
    bool m_kb_ready = false;            // флаг готовности (порт 13)
    void key_events();
    void keyboard(unsigned int counter);
    void strobe_first();

    unsigned int m_received = 0;
    unsigned int m_sent = 0;
    unsigned int m_overruns = 0;
    unsigned int m_bells = 0;

    uint64_t char_ticks() const;
    void receive(uint8_t c);
    void begin_line();
    void end_line();
    void render_to(unsigned int x_end);
    void video_sync();
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

    // Клавиша с кодом c нажата / отпущена, из любого потока; dk - клавиша
    // блока курсора. token - какая это клавиша; пустой - нажата и сразу
    // отпущена
    void key_press(const std::string &token, uint8_t c, bool dk);
    void key_release(const std::string &token);

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
