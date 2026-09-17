// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ keyboard (МС7007)

#pragma once

#include <deque>

#include "emulator/devices/common/keyboard.h"
#include "emulator/thread_compat.h"

// The keyboard of the УК-НЦ is not like the one of the БК: its controller
// hands the machine a SCAN CODE rather than a character, and it does so on
// both edges - a release is bit 7 plus only the low four bits of the number.
// The ROM turns those numbers into characters itself, through five layout
// tables it builds at start-up, so the register keys (ЗАГЛ, СТР, РУС, ЛАТ,
// УПР) are ordinary keys with scan codes of their own and nothing here has to
// know about Rus or case.
//
//   177700  состояние   разряд 6 - разрешение прерывания (запись),
//                       разряд 7 - готовность (чтение)
//   177702  код клавиши разряды 0-6 - номер клавиши при нажатии; при
//                       отпускании разряд 7 и только разряды 0-3 номера.
//                       Чтение снимает готовность
//
// Note the polarity: on the БК bit 6 of the status register MASKS the
// interrupt, here it ENABLES it - the ROM writes `MOV #100,@#177700` with the
// comment «Разрешить прерывания от клавиатуры» and `CLR @#177700` to forbid
// them. The gating itself is done outside, in the configuration.
//
// The device writes the code into a port named by the port-value parameter and
// pulses ~ready, exactly the way map-keyboard does - the flip-flop that holds
// readiness and the register that shows it live in the configuration.
//
// Two layouts, both files of `name: number` lines: `map` names the host keys,
// `keys` the machine's own, the ones drawn on ms7007_keyboard.svg. НР, УПР,
// ГРАФ, АЛФ and ФИКС are keys with numbers of their own, declared `hold:` in
// the native table so that a pointer clicks them on and off: the machine
// expects them held, and a mouse has one contact point. The only mark there is
// lives in the host map: `+shift` / `-shift` after a symbol's number, because
// the host and the МС7007 put symbols on different shift levels.
class UKNCKeyboard: public Keyboard
{
private:
    // Регистр знака. Знак хоста живёт на клавише машины то без НР, то с НР,
    // и Shift хоста тут не указ: `'` набирается на хосте без Shift, а на
    // МС7007 это НР и 7; `:` - наоборот. Такой записи клавиатура сама ставит
    // НР в нужное положение на время нажатия
    enum ShiftMode { SHIFT_ANY, SHIFT_ON, SHIFT_OFF };

    struct KeyEntry {
        unsigned int host;      // код клавиши хоста
        unsigned int scan;      // номер клавиши для машины
        ShiftMode    shift;     // пометка +shift / -shift в раскладке
    };
    std::vector<KeyEntry> m_keys;

    // Клавиши самой машины: имя клавиши МС7007 (оно же имя элемента рисунка)
    // и её номер. Та же таблица, что в m_keys, только слева стоит не клавиша
    // хоста, а клавиша машины
    struct IdEntry {
        std::string  id;
        unsigned int scan;
    };
    std::vector<IdEntry> m_ids;

    AddressableDevice * m_port = nullptr;    // регистр кода, 177702

    // Готовность - уровень, а не импульс: она стоит, пока программа не
    // прочитает регистр кода, и показывается разрядом 7 регистра 177700
    Interface i_ready;
    Interface i_pressed;        // хоть одна клавиша нажата
    Interface i_read;           // строб чтения регистра кода снимает готовность
    Interface i_irq_enable;     // разряд 6 регистра 177700

    // Запрос процессору и цепочка приоритетов, как у таймера: свой запрос
    // вперёд, чужой дальше. Клавиатура стоит к процессору ближе таймера и
    // каналов - её вектор 300 старше их 304 и 320
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;

    bool m_ready = false;
    unsigned int m_offered = 0;
    void update_irq();

    unsigned int m_vector = 0300;
    unsigned int m_codes = 0;   // сколько кодов отдано, для сценариев
    unsigned int m_last = 0;    // последний отданный код

    const KeyEntry * entry_of(unsigned int host) const;

    // НР, каким его держит хост (Shift или клавиша рисунка), и каким его
    // видит машина. Расходятся, пока нажат знак с пометкой регистра
    bool m_shift_host = false;
    bool m_shift_machine = false;
    unsigned int m_forced = 0;          // сколько знаков с пометкой нажато
    void press_key(unsigned int scan, ShiftMode shift, bool press);
    void send(unsigned int code);

    // Коды, ещё не отданные машине. Нажатие окна приходит из потока GUI, а
    // регистр кода, готовность и линия прерывания ПП живут в потоке эмуляции:
    // тронь их отсюда - и запрос прерывания теряется, когда ПП в тот же миг
    // берёт предыдущий или снимает готовность чтением. Поэтому клавиша только
    // встаёт в очередь, а в регистр её кладёт clock(), и не раньше, чем
    // прочитан предыдущий код - код отпускания больше не затирает непрочитанное
    // нажатие. Настоящий МС7007 тоже ждёт машину: у него свой микроконтроллер
    std::deque<unsigned int> m_queue;
    compat_mutex m_queue_mutex;
    volatile bool m_queued = false;     // быстрая проверка без захвата
    unsigned int m_dropped = 0;         // не влезло в очередь
    void enqueue(unsigned int scan, bool press);

    emulator::Result parse_key_table(const std::vector<std::string> &body, const std::string &file) override;
    void send_key_id(const std::string &id, bool press) override;

public:
    UKNCKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    void key_down(unsigned int key) override;
    void key_up(unsigned int key) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_uknc_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
