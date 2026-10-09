// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: КЦГД (ДВК colour graphics display controller): video memory, registers, video output

#pragma once

#include <atomic>
#include <vector>

#include "emulator/core.h"
#include "emulator/devices/common/virq_line.h"
#include "emulator/devices/common/mouse.h"

class KCGDMouse;

// Контроллер цветного графического дисплея КЦГД - графический терминал ДВК,
// замена КСМ. Своя машина на КМ1801ВМ2 (7,7 МГц) с прошивкой 181 или 182,
// своя клавиатура МС7004 и последовательная линия к ДВК; к видеопамяти
// процессор ДВК доступа не имеет. Процессор, ПЗУ и оба ИРПС описываются в
// конфигурации обычными устройствами; здесь - то, что у платы своё.
//
// Видеопамять - 64К слов по 17 разрядов (17 микросхем К565РУ5). Процессору
// КЦГД её младшие 16К слов видны с адреса 0 как ОЗУ (окно 000000-077777),
// остальное - через регистр адреса 160000 и регистр данных 160002 (повторы
// через 4 байта до 163777). 17-й разряд пишется вместе со словом из разряда
// 7 регистра управления: 0 - слово показывается 8 точками по 2 разряда, 1 -
// 4 удвоенными точками по 4 разряда.
//
// КР1801ВП1-033 (167770-167776):
//   167770 - разряд 0 - набор таблицы строк, 1 - чересстрочная развёртка,
//            5, 6 - разрешение прерываний B (таймер, вектор 304) и A (вектор
//            300), 7 - REQA (17-й разряд последнего прочитанного слова,
//            исключающее ИЛИ с разрядом 6 управления), 15 - REQB (разряд Q12
//            счётчика строк развёртки: меняется каждые 16 строк, ~0,5 мс);
//            RESET снимает разряды 0, 1, 5, 6
//   167772 - запись: младший байт - управление (разряды 2-5 - номер регистра
//            палитры, 6 - инверсия REQA, 7 - 17-й разряд записи), старший -
//            значение регистра палитры (8 разрядов, в ЦАП идут шесть, по
//            два на канал); чтение отдаёт управление и регистр по номеру
//   167774 - чтение: мышь в разрядах 8-11 (KCGDMouse), разряд 2 управления
//            выбирает ось
// По MAME (dvk_kcgd.cpp), сверено с прошивкой 181 и нетлистом реплики.
//
// Развёртка - по нетлисту и дампам ПЗУ К556РТ4 (docs-external/DVK/KCGD):
// D33 делит 30,8 МГц на знакоместо в 16 тактов (4 такта процессора, его
// частота - тот же делитель); D57/D58 считают знакоместа строки (Q1-Q7), D61
// и D62 по их номеру и половине знакоместа (U32) дают строб строки (её конец
// - разряд V6, он же счёт строк), гашение, синхронизацию, RAS и окна
// процессора; D59/D60 считают строки (Q8-Q15, загрузка 061), D85 - поле
// (Q16) и полукадр чересстрочной развёртки (Q17); D63 по номеру строки,
// Q7 и Q17 даёт конец поля (V10 - загрузка счётчика), кадровый импульс,
// гашение и синхронизацию кадра. Защёлки D68-D70 берут выходы ПЗУ по
// фронтам D33. Всё это при загрузке прогоняется по полузнакоместам один
// раз (build_scan()), и дальше развёртка - таблица событий поля.
//
// Видеопамять процессор получает в окне: обращение к ней (окно 000000-077777
// и регистр данных 160002) ждёт знакоместа, в конце которого D61 (разряд 3)
// открывает окно, и RPLY приходит в конце следующего за окном (защёлка D70,
// D30, D19). ПЗУ, ВП1-033, ВП1-065 и регистр адреса 160000 отвечают сами
class KCGD: public AddressableDevice, public WaitSource
{
public:
    static const unsigned VRAM_WORDS = 65536;

private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_init;
    // Кадровый импульс на EVNT процессора (вектор 100), уровень вывода
    // защёлки D70: 1 - покой, 0 - импульс
    Interface i_evnt;

    std::vector<uint32_t> m_vram;
    KCGDMouse * m_mouse = nullptr;
    unsigned m_ra = 0;              // регистр адреса
    unsigned m_control = 0;         // младший байт 167772
    unsigned m_out = 0;             // слово, записанное в 167772
    uint8_t m_palette[16] = {};
    unsigned m_csr = 0;             // разряды 0, 1, 5, 6 регистра 167770
    bool m_reqa = false;            // 17-й разряд последнего чтения через 160002
    bool m_line_a = false;          // запрос A: разрешён и REQA
    bool m_line_b = false;          // запрос B: разрешён и REQB
    bool m_reqb = false;
    bool m_pending_a = false;
    bool m_pending_b = false;
    unsigned m_vector_a = 0300;
    unsigned m_vector_b = 0304;

    // Развёртка, построенная build_scan(). Время - в тактах процессора
    struct ScanStep {
        uint32_t t;                 // от начала поля
        uint8_t evnt;               // EVNT с этого момента
        uint8_t reqb;               // Q12 с этого момента
    };
    unsigned m_line_clocks = 1;                 // строка
    std::vector<uint16_t> m_reply;              // по такту строки: от строба до RPLY
    std::vector<ScanStep> m_field[2];           // смены по полю, по Q17
    uint32_t m_field_clocks[2] = {1, 1};
    unsigned m_field_hpos = 0;                  // место в строке, где поле начинается
    unsigned m_evnt_bit = 1;                    // выход D63, взятый на EVNT
    // Где развёртка сейчас
    unsigned m_hpos = 0;                        // такт строки
    uint32_t m_fpos = 0;                        // такт поля
    unsigned m_q17 = 0;                         // полукадр
    unsigned m_step = 0;                        // следующая смена в m_field[m_q17]
    unsigned m_fields = 0;                      // полей с пуска

    emulator::Result build_scan(const std::vector<uint8_t> &d61, const std::vector<uint8_t> &d62,
                                const std::vector<uint8_t> &d63);
    void scan_apply(unsigned evnt, unsigned reqb);
    void scan_seek(bool apply);

    void write_vram(unsigned index, unsigned value, unsigned mask);
    bool reqa() const;
    void update_requests();
    void update_irq();
    void reset_registers();

public:
    KCGD(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    unsigned int wait_states(unsigned int address, bool write, unsigned int offset) override;
    // Полей с пуска: изображение перерисовывается по смене поля
    unsigned fields() const { return m_fields; }

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned get_direct(unsigned address) override;

    // Для развёртки (поток отрисовки читает без блокировки, как все дисплеи)
    uint32_t vram(unsigned index) const { return m_vram[index & (VRAM_WORDS - 1)]; }
    // В ЦАП идут шесть разрядов из восьми, что хранят две КР1802ИР1
    unsigned palette(unsigned index) const { return m_palette[index & 15] & 077; }
    bool page() const { return (m_csr & 1) != 0; }
    bool interlace() const { return (m_csr & 2) != 0; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// Изображение КЦГД: 800 x 480, по 240 строк в поле. Строку растра задаёт
// таблица адресов в видеопамяти (набор 0 - с байта 015574 окна, набор 1 - с
// 005574), по 100 слов на строку. Без чересстрочной развёртки (прошивка 181)
// каждая строка таблицы показывается дважды, и поверхность тогда 800 x 240. Цвет точки - регистр палитры:
// у 2-разрядной точки v - регистр 5*v, у 4-разрядной - её значение.
//
// Регистр палитры - 6 разрядов, по два на выход (U55, U56, U57 разъёма XP4,
// ЦАП на резисторах): U55 - красный, U56 - зелёный, U57 - синий (по словам
// автора реплики). Параметр rgb оставлен для другой разводки кабеля.
//
// Своей развёртки у изображения нет: поле меняется по счётчику KCGD.
class KCGDDisplay: public GenericDisplay
{
private:
    KCGD * m_kcgd = nullptr;
    Memory * m_font_rom = nullptr;
    unsigned m_font_offset = 0;
    unsigned m_font_column = 2;           // первая точка знака в знакоместе
    unsigned m_font_map_offset = 0;
    // Копия шрифта прошивки для screen_text(): берётся из ПЗУ при каждом
    // чтении, потому что прошивку можно сменить переключателем машины
    mutable uint8_t m_font_map[96] = {};          // код 040-0177 -> номер знака шрифта
    unsigned m_font_map_h1_offset = 0;
    mutable uint8_t m_font_map_h1[64] = {};       // кириллица КОИ-7 Н1, коды 0100-0177
    mutable std::vector<uint8_t> m_font;            // 256 знаков по 10 байт
    void load_font() const;
    uint32_t m_colors[64] = {};             // все 64 значения регистра палитры
    unsigned m_channel[3] = {0, 1, 2};      // выход U55/U56/U57 -> R, G, B

    unsigned int m_field = 0;           // поле KCGD, которое уже нарисовано
    unsigned int m_surface_lines = 0;   // строк в поверхности отрисовщика

    unsigned line_address(unsigned y) const;
    unsigned pixel(unsigned a, unsigned x) const;
    std::string screen_text() const;

protected:
    void render_all(bool force_render) override;

public:
    KCGDDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void set_renderer(VideoRenderer &vr) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    //240 lines a field in either mode
    unsigned int get_scan_lines() override { return 240; }
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// Мышь КЦГД (разъём XP5). У каждой оси - реверсивный счётчик К555ИЕ7 (X -
// D84, Y - D14): мышь даёт импульсы «вперёд» и «назад», счётчик их считает.
// Мультиплексор К555КП14 (D15) по линии G1 (разряд 2 управления, номер
// регистра палитры) выдаёт на AD08-AD11 три младших разряда одного счётчика
// и кнопку: 0 - X и кнопка с XP5:9, 1 - Y и кнопка с XP5:10. Выход
// мультиплексора открывает КР1801ВП1-033 - при чтении регистра 167774. Так
// по нетлисту реплики; MAME кладёт то же в младшие разряды, и выбор оси у
// него в коде обратный комментарию.
//
// Прошивки 181 и 182 и KeyGP мышь не читают: её читает программа, которую
// машина загружает в КЦГД. Счётчик трёхразрядный, и между двумя опросами
// он не должен обернуться: шаги хоста выдаются не чаще одного за step_us.
// Направления (вправо и вниз - счёт вверх) взяты как в MAME, кнопка нажата -
// 1; с настоящей мышью не сверены.
class KCGDMouse: public ComputerDevice, public HostMouse
{
private:
    unsigned m_x = 0;
    unsigned m_y = 0;
    unsigned m_period = 1;          // тактов на шаг
    unsigned m_ticks = 0;
    std::atomic<int> m_dx;
    std::atomic<int> m_dy;
    std::atomic<unsigned> m_buttons;

    static void add_pending(std::atomic<int> &axis, int delta);

public:
    KCGDMouse(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    void move(int dx, int dy, int buttons) override;
    bool mouse_plugged() const override { return true; }

    // Что видит КЦГД: 0 - X и кнопка 1, 1 - Y и кнопка 2, 4 разряда
    unsigned value(unsigned axis) const;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_kcgd(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_kcgd_mouse(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_kcgd_display(InterfaceManager *im, EmulatorConfigDevice *cd);
