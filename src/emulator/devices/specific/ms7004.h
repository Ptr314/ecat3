// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: МС7004 keyboard (a DEC LK201 workalike) on a serial line

#pragma once

#include <deque>

#include "emulator/devices/common/keyboard.h"
#include "emulator/thread_compat.h"

// Клавиатура МС7004 - копия DEC LK201 с лишними клавишами для кириллицы. У
// неё свой микроконтроллер (КР1816ВЕ35), и терминалу она отдаёт не символы, а
// КОДЫ КЛАВИШ LK201 по последовательной линии на 4800 бод. Символ из кода
// делает терминал: у КСМ это таблица 0AFD + ~код в его ПЗУ.
//
// Поведение снято с её прошивки (docs-external/DVK/MC7004_keyboard_original.rom,
// разбор - ms7004.lst), а не с описания LK201, от которого она отличается:
//   - обычная клавиша отдаёт свой код при нажатии и ничего при отпускании;
//     держать можно три такие клавиши, четвёртая не замечается (017D);
//   - ВР ($AE, правый ВР тоже) и СУ ($AF) отдают код при нажатии, а при
//     отпускании КАЖДОЙ из них - $B3 (0231), даже если другая ещё нажата. У
//     КСМ «$B3» снимает сначала СУ и только потом ВР (0587), поэтому ВР,
//     отпущенный под СУ, оставляет у терминала нажатым ВР вместо СУ - так на
//     настоящей машине, до отпускания СУ;
//   - автоповтор делает сама клавиатура: последняя нажатая клавиша, если её
//     держат, через repeat_delay даёт $B4, потом $B4 каждые repeat_period
//     (029B). Терминал повторяет свой последний символ (КСМ - 057A, если
//     автоповтор не выключен в его установках; КЦГД - так же). Не повторяются
//     ВК ($BD), верхний ряд (коды до $80) и коды $80-$8F. Повтор хоста окно
//     отбрасывает (isAutoRepeat);
//   - команды терминала (~rxd): $FD - перезапуск, ответ $01 и три нуля (его
//     ждёт заводской тест КЦГД KC.SAV, ошибка 17), автоповтор снова включён;
//     $AB - $01, $00; $89 - ответ $B7, и до $8B клавиатура молчит (коды
//     пропадают); $D3 - ответ $BA; $E1 и $D9 выключают автоповтор, $E3
//     включает. Звонок и щелчок принимаются и ничего не меняют;
//   - индикаторы (0421-066E): $13 и байт зажигает, $11 и байт гасит. Байт
//     1000abcd: d - ОЖИД (порт P2.4), c - КОМПОЗ (P2.5), b - ФКС (P2.6), a -
//     СТОП КАДР (P2.7); 1001xxxx - ЛАТ (P1.5), и он наоборот: $13 снимает
//     разряд, $11 ставит. $89 зажигает ОЖИД, $8B гасит; при включении и по
//     $FD все гаснут. Лампа ЛАТ горит при поставленном P1.5.
// Времена автоповтора посчитаны по тактам прошивки (46 машинных циклов - 150
// мкс по её комментарию): проход матрицы ~33 мс, задержка 11 проходов, между
// повторами 7710 циклов ожидания с передачей и щелчком. Не воспроизведено:
// пока идёт автоповтор, прошивка не опрашивает других клавиш.
//
// Цифровой блок хоста - цифровой блок МС7004 со своими кодами (92h-A0h, ВВОД
// 95h): клавиатура берёт клавиши с EmuKey::Keypad (keypad_keys()), а клавиша
// блока, которой нет в раскладке, - то же, что в основном поле.
//
// Раскладка - файл map, строки `клавиша хоста: код [+shift|-shift]`, как у
// УК-НЦ: латинские буквы отданы клавишам с той же латинской надписью, в
// русском регистре получается ЙЦУКЕН самой МС7004 (use_remap = 0). Пометка
// `+shift` - знак набирается с ВР, `-shift` - без него, хотя хост держит
// Shift: на время нажатия ВР ставится как надо, потом возвращается.
//
// Код уходит на линию (~data, байт целиком) не чаще, чем раз в interval
// тактов - столько идёт символ на 4800 бод. Очередь пополняет поток окна,
// разбирает поток эмуляции, как у клавиатуры УК-НЦ.
class MS7004: public Keyboard
{
private:
    enum ShiftMode { SHIFT_ANY, SHIFT_ON, SHIFT_OFF };

    struct KeyEntry {
        unsigned int host;
        unsigned int code;
        ShiftMode    shift;
    };
    std::vector<KeyEntry> m_keys;

    Interface i_data;
    Interface i_rxd;                    // команды от терминала, байт целиком

    unsigned int m_interval = 3000;
    unsigned int m_wait = 0;            // тактов до следующего кода

    // Клавиши «вниз/вверх»: какими их держит хост и какими их отметила
    // клавиатура (её байт 36H)
    bool m_shift_host = false;
    bool m_ctrl_host = false;
    bool m_shift_machine = false;
    bool m_ctrl_machine = false;
    unsigned int m_forced = 0;          // сколько знаков с пометкой нажато

    // Обычные клавиши, которые держат: клавиша хоста и её код (не больше
    // трёх, последняя нажатая - в конце)
    std::vector<std::pair<unsigned int, unsigned int> > m_held;
    // Автоповтор последней нажатой
    unsigned int m_repeat_delay = 0;    // такты до первого $B4
    unsigned int m_repeat_period = 0;   // такты между $B4
    unsigned int m_repeat_left = 0;     // такты до следующего $B4
    volatile bool m_repeat_armed = false;
    bool m_repeating = false;
    // Режимы, заданные терминалом
    bool m_repeat_on = true;
    bool m_inhibit = false;
    bool m_second = false;              // команда ждёт второго байта
    unsigned int m_led_command = 0;     // $11 или $13, ждущая своего байта

    // Индикаторы: разряды 0-3 - ОЖИД, КОМПОЗ, ФКС, СТОП КАДР (P2.4-P2.7),
    // разряд 4 - P1.5 (ЛАТ)
    volatile unsigned int m_leds = 0;

    // Клавиши рисунка (keys): имя и код. Нажатая с рисунка клавиша держится
    // под «кодом хоста» KEY_ID_BASE + номер, чтобы не спутаться с хостом
    std::vector<std::pair<std::string, unsigned int> > m_id_codes;
    std::string id_of_code(unsigned int code) const;

    std::deque<unsigned int> m_queue;
    compat_mutex m_queue_mutex;
    volatile bool m_queued = false;
    unsigned int m_dropped = 0;
    unsigned int m_codes = 0;
    unsigned int m_last = 0;
    unsigned int m_commands = 0;
    std::deque<unsigned int> m_command_log;     // последние 16 байтов от терминала
    unsigned int m_repeats = 0;

    const KeyEntry * entry_of(unsigned int host) const;
    void press(const KeyEntry &e, bool down);
    void set_shift(bool down, bool forced);
    void arm_repeat_locked();
    void send_locked(unsigned int code);
    void enqueue_locked(unsigned int code);
    void command(unsigned int code);
    void led_byte(unsigned int command, unsigned int value);

protected:
    emulator::Result parse_key_table(const std::vector<std::string> &body, const std::string &file) override;
    void send_key_id(const std::string &id, bool press) override;

public:
    MS7004(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;
    bool keypad_keys() const override { return true; }
    std::vector<Indicator> indicators() const override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    // ВР и СУ, какими их отметила клавиатура, режимы терминала и темп линии -
    // состояние машины; очередь кодов и нажатые клавиши - ввод хоста, его
    // снимок не несёт
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_ms7004(InterfaceManager *im, EmulatorConfigDevice *cd);
