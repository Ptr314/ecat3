// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ sound - the speaker line and the tone grid of register 177716

#include "uknc_sound.h"
#include "uknc_timer.h"

#define IN_LINE     0x01        // разряд 7 регистра
#define IN_GRID     0x3E        // разряды 8-12

// Отводы счётчика таймера для разрядов 8-12. При полном двенадцатиразрядном
// счёте с шагом 8 мкс это и есть сетка 62.5 Гц, 250 Гц, 500 Гц, 1 кГц, 8 кГц
// из описания; с другой уставкой в 177712 сетка вся целиком уезжает за ней
static const unsigned int GRID_TAPS[5] = {10, 8, 7, 6, 3};

// Срез фильтра, стоящего на шагах счётчика. Выше слышимой середины диапазона,
// чтобы не трогать ноты и их обертоны, и достаточно низко, чтобы от меандра
// паузы (55 кГц) не осталось ничего: второй порядок даёт там -30 дБ. Тембр
// задаёт фильтр на выходе устройства (параметр lpf), этот - про то, чего
// живой динамик не воспроизводит вовсе
#define GRID_CUTOFF 10000

UKNCSound::UKNCSound(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericSound(im, cd)
    , i_input(this, im, 6, "input", MODE_R, 1)
    , m_timer(nullptr)
    , m_seen_steps(0)
    , m_prev_phase(0.0)
    , m_grid_rate(0.0)
    , m_grid_last(0.0)
{
    m_self_volatile = false;
}

emulator::Result UKNCSound::load_config(SystemData *sd)
{
    emulator::Result res = GenericSound::load_config(sd);
    if (!res) return res;

    //Какому таймеру принадлежит счётчик. Без параметра берётся единственный
    //таймер машины - так открывается состояние, записанное до того, как сетку
    //стало откуда брать: конфигурация внутри снимка своя и переписать её нельзя
    const std::string timer_name = cd->get_parameter("timer", false).value;
    if (!timer_name.empty())
        m_timer = dynamic_cast<UKNCTimer*>(im->dm->get_device_by_name(timer_name, false));
    else
        for (unsigned int i = 0; i < im->dm->device_count && m_timer == nullptr; i++)
            m_timer = dynamic_cast<UKNCTimer*>(im->dm->get_device(i)->get());

    if (m_timer == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCSound|" + std::string(QT_TRANSLATE_NOOP("UKNCSound", "The tone grid needs a timer device")) + "} "
            + name + ": " + timer_name);

    return emulator::Result::ok();
}

void UKNCSound::update_volatile()
{
    const unsigned int in = i_input.value;
    m_self_volatile = (in & IN_LINE) != 0 && (in & IN_GRID) != 0;
}

void UKNCSound::interface_callback(MAYBE_UNUSED unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (((new_value ^ old_value) & (IN_LINE | IN_GRID)) == 0) return;
    update_volatile();
    sound_changed();
}

void UKNCSound::reset(bool cold)
{
    GenericSound::reset(cold);
    update_volatile();
    sound_changed();
}

void UKNCSound::state_restored()
{
    GenericSound::state_restored();
    //Whether the level moves on its own is worked out from the register, and
    //a restored line does not announce itself: Interface::restore() assigns
    //without firing interface_callback(). Left as the reset found it - the
    //register at zero, so nothing moving - the mixer would keep the level it
    //computed once and only look again when something else dirtied it, which
    //during a tone is once a millisecond. The tone came back as a coarse,
    //aliased version of itself until the program next wrote 177716
    update_volatile();
}

unsigned int UKNCSound::grid() const
{
    return (m_timer != nullptr)? m_timer->counter() : 0;
}

bool UKNCSound::level_at(unsigned int counter) const
{
    const unsigned int in = i_input.value;

    // Разряд 7 в нуле глушит всё
    if ((in & IN_LINE) == 0) return false;

    const unsigned int grid = (in & IN_GRID) >> 1;
    if (grid == 0) return true;

    // Включённые частоты собираются «по И»: звучит, пока ни одна из них не
    // подняла свой уровень. Это NOR, а не AND, но перевёрнутая волна звучит
    // так же, и UKNCBTL собирает именно так
    for (unsigned int i = 0; i < 5; i++)
        if ((grid & (1u << i)) && ((counter >> GRID_TAPS[i]) & 1))
            return false;
    return true;
}

bool UKNCSound::level() const
{
    return level_at(grid());
}

void UKNCSound::clock(unsigned int counter)
{
    GenericSound::clock(counter);
    // Где счётчик стоял, когда уровень считали в последний раз: calc_sound_value()
    // зовётся из GenericSound::clock() выше, поэтому отметка ставится после него
    if (m_timer != nullptr)
    {
        m_seen_steps = m_timer->steps();
        m_prev_phase = m_timer->counter_phase_cycles();
    }
}

int16_t UKNCSound::calc_sound_value()
{
    // Уровень за прошедший отрезок, а не в его конце. Устройство тактуется раз
    // в команду периферийного процессора - десяток-другой тактов, - а счётчик,
    // с которого снимается сетка, шагает каждые 12.5 такта. Взять его значение
    // на границе команды значит растянуть один шаг на всю команду, а соседние
    // выбросить: пауза мелодии («уставка 8» - меандр 55 кГц, которого на живой
    // машине не слышно вовсе) превращалась в широкополосную грязь посреди
    // слышимого диапазона, и та же грязь лежала под каждой нотой.
    //
    // Поэтому отрезок собирается по времени: кусок от его начала до первого
    // шага, целые шаги между ними и кусок от последнего шага до конца. Фазу
    // даёт сам таймер, так что делить такты пополам не приходится.
    //
    // И по дороге уровень проходит через фильтр нижних частот, поставленный на
    // шаги счётчика. Это единственное место во всём тракте, где отсчёты идут
    // равномерно: шаг - ровно 12.5 такта, тогда как отрезки приходят длиной в
    // команду, а отсчёты звуковой карты берутся ещё в двадцать раз реже.
    // Поэтому здесь фильтр и работает - до всякого прореживания. Меандр паузы
    // (55 кГц) он давит на тридцать децибел, и то, что раньше сворачивалось
    // внутрь слышимого диапазона грязью, до прореживания просто не доживает;
    // на самих нотах (сотни герц) он не сказывается никак
    if (m_timer != nullptr)
    {
        const uint64_t d = m_timer->steps() - m_seen_steps;
        const double step = m_timer->counter_step_cycles();
        if (d > 0 && d < UKNC_TIMER_STEP_LOG && step > 0.0)
        {
            //Период счёта программа меняет разрядами 1-2 регистра 177710, и
            //вместе с ним меняется частота, на которой стоит фильтр
            const double rate = (double)m_system_clock / step;
            if (rate != m_grid_rate)
            {
                m_grid_rate = rate;
                m_grid_filter.setup((float)rate, (float)GRID_CUTOFF);
            }

            //До первого шага держался уровень, на котором кончился прошлый отрезок
            double w = step - m_prev_phase;
            if (w < 0.0) w = 0.0;
            double sum = m_grid_last * w;
            double total = w;

            const double tail = m_timer->counter_phase_cycles();
            for (unsigned int i = 1; i <= (unsigned int)d; i++)
            {
                const unsigned int v = m_timer->step_value((unsigned int)d - i);
                m_grid_last = m_grid_filter.process(level_at(v) ? 1.0f : -1.0f);
                //Последний шаг длится до конца отрезка, а не целый период
                const double dur = (i < (unsigned int)d) ? step : tail;
                sum += m_grid_last * dur;
                total += dur;
            }

            if (total > 0.0)
            {
                //Фильтр Баттерворта звенит, и на фронте ноты выброс выходит за
                //единицу: без ограничения он завернулся бы в int16 щелчком
                double v = sum / total;
                if (v > 1.0) v = 1.0;
                else if (v < -1.0) v = -1.0;
                return (int16_t)((double)m_amplitude * v);
            }
        }
    }
    //Счётчик с прошлого раза не двинулся - уровень такой, какой есть
    return (int16_t)(level() ? m_amplitude : -m_amplitude);
}

std::vector<DeviceFieldInfo> UKNCSound::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericSound::get_device_fields();
    r.push_back({"control", "Разряды 7-12 регистра 177716, сдвинутые к нулю", false});
    r.push_back({"level",   "Уровень на выходе прямо сейчас, 0 или 1",       false});
    r.push_back({"grid",    "Счётчик таймера, из которого берётся сетка",    false});
    return r;
}

bool UKNCSound::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "control") { out.values.push_back(i_input.value & 077); return true; }
    if (field == "level")   { out.values.push_back(level()? 1 : 0);     return true; }
    if (field == "grid")    { out.width = 16; out.values.push_back(grid()); return true; }
    out.numeric = false;
    return GenericSound::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_sound(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCSound(im, cd);
}
