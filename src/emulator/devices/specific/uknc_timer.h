// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ programmable timer

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/virq_line.h"

// Сколько последних значений счётчика таймер держит для динамика. Команда
// периферийного процессора - это десяток-другой тактов, шаг счёта - 12
// такта, так что в одну команду укладывается пара шагов; тридцати двух хватает
// и на самую длинную команду, и на догон после остановки
#define UKNC_TIMER_STEP_LOG 32

// The programmable timer of the УК-НЦ, registers 177710-177714 on the
// peripheral processor's bus. A 12 bit counter running down at 2, 4, 8 or
// 16 us per step; on zero it raises a flag, optionally interrupts through
// vector 304, and reloads itself from the buffer register - there is no
// one-shot mode, it is always cyclic.
//
// «2 мкс» номинальные: шаг - 12 тактов ПП (6,25 МГц), 1,92 мкс. Тест
// таймера Titus (TSTMR v0.1d) на машине с 1515ХМ1-031 насчитывает 2600
// шагов по «8 мкс» за кадр 19,968 мс и минимальный период 1,920 мкс. Ноль
// счётчик держит целый шаг и с него переходит сразу в N - 1 (уставка 99:
// ... 1, 0, 98, 97 ...), так что период остаётся N шагов. При уставке 1
// N - 1 - снова ноль, и готовность встаёт каждый шаг.
//
//   177710  состояние    чтение и запись, байт
//   177712  буферный     только запись, 12 разрядов (уставка)
//   177714  текущее      только чтение, 12 разрядов
//
// Register 177710:
//
//   разряд 0    пуск (1) или останов (0). Переход 0->1 загружает счётчик
//               из буферного регистра
//   разряды 1-2 период счёта: 2, 4, 8 или 16 мкс
//   разряд 3    ошибка переполнения: счётчик обнулился, когда разряд 7 ещё
//               не был снят. Только чтение, снимается чтением 177710
//   разряд 4    разрешение прерывания по внешнему событию, вектор 310
//   разряд 5    готовность внешнего события. Только чтение, снимается
//               чтением 177714. Пока он взведён, счёт стоит
//   разряд 6    разрешение прерывания при обнулении счётчика, вектор 304
//   разряд 7    готовность при обнулении счётчика. Только чтение,
//               снимается чтением 177714
//
// Прототипная документация машины говорит про разряд 0 обратное - «ЛОГ.0 -
// ПУСК, ЛОГ.1 - СТОП», - но ПЗУ самой машины пишет `CLR @#177710` с
// комментарием «Остановить программируемый таймер» и `MOV #101,@#177710` с
// комментарием «Пуск таймера», так что единица здесь пускает.
class UKNCTimer: public AddressableDevice
{
private:
    unsigned int m_flags = 0;           // 177710
    unsigned int m_reload = 0;          // 177712, 12 разрядов
    unsigned int m_counter = 0;         // сам счётчик, 12 разрядов
    // Регистр текущего значения 177714 - не счётчик, а его копия. Пока взведена
    // готовность обнуления или события, она заморожена на том, что было в
    // момент первой из них, и чтение отдаёт её; иначе читается счётчик
    unsigned int m_latch = 0;
    // Событие пришло: на следующем шаге счётчик грузится из буфера
    bool m_event_load = false;

    unsigned int m_divider = 0;         // предделитель внутри выбранного периода
    unsigned int m_acc = 0;             // такты, накопленные до следующего шага
    unsigned int m_base_cycles = 12;    // базовый шаг в тактах своего домена
    unsigned int m_period_us = 2;       // тот же шаг номинально, мкс (поле period)

    unsigned int m_zeroes = 0;          // сколько раз счётчик обнулялся

    // Векторы прерываний
    unsigned int m_vector_zero = 0304;
    unsigned int m_vector_event = 0310;

    // Запрос прерывания процессору. Линия активна нулём, как всё на
    // магистрали 1801. Вход virq_in даёт цепочку приоритетов: если у таймера
    // своего запроса нет, он пропускает чужой дальше, и на один вход
    // процессора можно навесить несколько источников
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;

    // Внешнее событие: перепад на этой линии взводит разряд 5. У машины это
    // вход магнитофона, и перепады проходят только при единице на
    // event_enable (разряд 2 регистра 177716); неподключённый вход открыт.
    // Счётчик не останавливается, а перезагружается из буфера и считает
    // дальше; регистр 177714 хранит его значение в момент перепада - обработчик
    // ПЗУ читает оттуда длительность полупериода, от перепада до перепада
    Interface i_event;
    Interface i_event_enable;
    unsigned int m_events = 0;          // сколько событий принято

    void base_tick();                   // один шаг базовой частоты
    void update_irq();

    // След последних значений счётчика, кольцом. Динамик снимает сетку частот
    // с этого же счётчика, а тактуется раз в команду - на порядок реже, чем
    // счётчик шагает. Посмотреть, где счётчик стоит сейчас, ему мало: пауза в
    // мелодии УК-НЦ - это 55 кГц, и между двумя взглядами она успевает
    // повториться дважды. Поэтому он спрашивает не точку, а пройденный путь
    unsigned int m_step_log[UKNC_TIMER_STEP_LOG];
    uint64_t m_steps = 0;               // сколько шагов сделано с пуска машины
    void log_step() { m_step_log[m_steps % UKNC_TIMER_STEP_LOG] = m_counter; m_steps++; }

public:
    UKNCTimer(InterfaceManager *im, EmulatorConfigDevice *cd);

    // Текущее значение счётчика. Отводы этого же счётчика идут на динамик:
    // разряды 8-12 регистра 177716 выбирают, какой из них пропустить, так что
    // уставка таймера задаёт и высоту тона. См. UKNCSound
    unsigned int counter() const { return m_counter; }

    // Пройденный путь: steps() монотонно растёт с каждым шагом счёта, а
    // step_value(0) - значение после последнего шага, step_value(1) - перед
    // ним и так далее, не глубже UKNC_TIMER_STEP_LOG
    uint64_t steps() const { return m_steps; }
    unsigned int step_value(unsigned int back) const
        { return m_step_log[(m_steps - 1 - back) % UKNC_TIMER_STEP_LOG]; }

    // Сколько тактов занимает один шаг счёта и сколько их прошло с последнего
    // шага. С этим динамик считает уровень точно по времени, а не по числу
    // шагов: шаг - 12 тактов, команда - десяток-другой, и граница команды
    // почти никогда не совпадает с границей шага
    double counter_step_cycles() const;
    double counter_phase_cycles() const;

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    unsigned int read_register(unsigned int address, bool peek);
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_uknc_timer(InterfaceManager *im, EmulatorConfigDevice *cd);
