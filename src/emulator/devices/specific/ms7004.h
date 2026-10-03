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
// Прошивки клавиатуры нет, поэтому она сделана на уровне её протокола:
//   - обычная клавиша отдаёт свой код при нажатии и ничего при отпускании;
//   - ВР (Shift, $AE) и СУ (Ctrl, $AF) - клавиши «вниз/вверх»: код при
//     нажатии, а при отпускании $B3 («все отпущены»), если других таких
//     клавиш не держат, иначе свой код ещё раз;
//   - автоповтор хоста (повторное нажатие без отпускания) отдаёт код снова.
// КСМ ей ничего не шлёт: в его ПЗУ нет ни одной записи в её порт. КЦГД
// шлёт команды LK201 (~rxd): на «перезапуск» $FD клавиатура отвечает
// последовательностью включения - номер прошивки $01 и три нуля (её ждёт
// заводской тест КЦГД KC.SAV, ошибка 17), на запрос номера $AB - $01, $00.
// Остальные команды (индикаторы, звонок, режимы клавиш) принимаются и
// ничего не меняют.
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

    // Клавиши «вниз/вверх», какими их держит хост и какими видит терминал
    bool m_shift_host = false;
    bool m_ctrl_host = false;
    bool m_shift_machine = false;
    unsigned int m_forced = 0;          // сколько знаков с пометкой нажато

    std::deque<unsigned int> m_queue;
    compat_mutex m_queue_mutex;
    volatile bool m_queued = false;
    unsigned int m_dropped = 0;
    unsigned int m_codes = 0;
    unsigned int m_last = 0;
    unsigned int m_commands = 0;

    const KeyEntry * entry_of(unsigned int host) const;
    void press(const KeyEntry &e, bool down);
    void set_shift(bool down);
    void modifier_released();
    void enqueue_locked(unsigned int code);
    void command(unsigned int code);

public:
    MS7004(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    // ВР и СУ, какими их видит терминал, и темп линии - состояние машины;
    // очередь кодов - ввод хоста, его снимок не несёт
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_ms7004(InterfaceManager *im, EmulatorConfigDevice *cd);
