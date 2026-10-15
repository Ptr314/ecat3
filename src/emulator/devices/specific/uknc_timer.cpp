// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ programmable timer

#include "uknc_timer.h"
#include "emulator/utils.h"

#define REG_STATE       0       // 177710
#define REG_RELOAD      1       // 177712
#define REG_COUNTER     2       // 177714

#define F_RUN           0001    // разряд 0 - пуск
#define F_DIV_MASK      0006    // разряды 1-2 - период счёта
#define F_OVERFLOW      0010    // разряд 3 - ошибка переполнения
#define F_EVENT_IRQ     0020    // разряд 4 - разрешение прерывания по событию
#define F_EVENT         0040    // разряд 5 - готовность внешнего события
#define F_READY         (F_ZERO | F_EVENT)
#define F_ZERO_IRQ      0100    // разряд 6 - разрешение прерывания по нулю
#define F_ZERO          0200    // разряд 7 - готовность при обнулении

// Разряды, которые принадлежат аппаратуре: программа их не пишет
#define F_READ_ONLY     (F_OVERFLOW | F_EVENT | F_ZERO)

#define COUNTER_MASK    07777   // счётчик двенадцатиразрядный

#define CALLBACK_EVENT  1

// Делитель базового периода: разряды 1-2 дают 2, 4, 8 или 16 мкс
static const unsigned int TIMER_DIVIDERS[4] = {1, 2, 4, 8};

UKNCTimer::UKNCTimer(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_EVENT + 1)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_event(this, im, 1, "event", MODE_R, CALLBACK_EVENT)
    , i_event_enable(this, im, 1, "event_enable", MODE_R)
{
    m_clocked = true;   // clock() переопределён
    can_read = true;
    can_write = true;
    addresable_size = 6;
}

emulator::Result UKNCTimer::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    // Базовый шаг счёта в тактах своего домена: 12 тактов ПП, 1,92 мкс, а
    // разряды 1-2 регистра состояния делят его дальше. period - тот же шаг
    // номинально, в микросекундах, как его называет документация
    m_base_cycles = read_confg_value(cd, "base_cycles", false, (unsigned int)12);
    if (m_base_cycles == 0) m_base_cycles = 12;
    m_period_us = read_confg_value(cd, "period", false, (unsigned int)2);
    if (m_period_us == 0) m_period_us = 2;

    m_vector_zero  = read_confg_value(cd, "zero_vector",  false, (unsigned int)0304);
    m_vector_event = read_confg_value(cd, "event_vector", false, (unsigned int)0310);

    return emulator::Result::ok();
}

void UKNCTimer::reset(MAYBE_UNUSED bool cold)
{
    m_flags = 0;
    m_reload = 0;
    m_counter = 0;
    m_latch = 0;
    m_event_load = false;
    m_divider = 0;
    m_acc = 0;
    m_zeroes = 0;
    m_events = 0;
    m_irq.clear();
    update_irq();
}

//--------------------------- Счёт ------------------------------------------//

void UKNCTimer::base_tick()
{
    if ((m_flags & F_RUN) == 0) return;         // останов

    m_divider++;
    if (m_divider < TIMER_DIVIDERS[(m_flags & F_DIV_MASK) >> 1]) return;
    m_divider = 0;

    // Ноль держится целый шаг, и с него счётчик переходит сразу в N - 1:
    // счёт цикличный, однократного режима у этого таймера нет. Так же и после
    // внешнего события. У ХМ2-001 по реверсу БМК (docs-external/UKNC/BMK_texts,
    // «таймер») шаг - восемь тактов: в такте 0 загрузка из буфера, если
    // счётчик обнулился или было событие, в такте 4 вычитание, в такте 7
    // обнуление и событие - отсюда N - 1 на следующем шаге
    if (m_counter == 0 || m_event_load) {
        m_event_load = false;
        m_counter = (m_reload - 1) & COUNTER_MASK;
        log_step();
        return;
    }
    m_counter = (m_counter - 1) & COUNTER_MASK;
    log_step();
    if (m_counter != 0) return;

    // Обнуление: если прежняя готовность ещё не снята, это переполнение.
    // Регистр текущего значения замирает на нуле, если его не заморозило
    // событие раньше
    if (m_flags & F_ZERO) m_flags |= F_OVERFLOW;
    if ((m_flags & F_READY) == 0) m_latch = 0;
    m_flags |= F_ZERO;
    m_zeroes++;
    update_irq();
}

double UKNCTimer::counter_step_cycles() const
{
    return (double)m_base_cycles * TIMER_DIVIDERS[(m_flags & F_DIV_MASK) >> 1];
}

double UKNCTimer::counter_phase_cycles() const
{
    //Предделитель уже отсчитал m_divider базовых шагов, а внутри текущего
    //накопилось m_acc тактов
    return (double)m_divider * m_base_cycles + m_acc;
}

void UKNCTimer::clock(unsigned int counter)
{
    m_acc += counter;
    while (m_acc >= m_base_cycles) {
        m_acc -= m_base_cycles;
        base_tick();
    }
}

void UKNCTimer::interface_callback(unsigned int callback_id, MAYBE_UNUSED unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_EVENT) {
        // Перепад на линии внешнего события взводит готовность и замораживает
        // регистр текущего значения на том, что насчитал счётчик, а сам
        // счётчик на следующем шаге грузится из буфера и считает дальше
        // (ХМ2-001 по реверсу БМК). ПЗУ читает из 177714 время между
        // перепадами (130600 `MOV @#177714,R5`), и отсчёт его идёт от
        // перепада, а не от чтения: задержка обработчика в замер не попадает.
        // Обратный вызов приходит и без смены уровня - от мультиплексора,
        // у которого сменился невыбранный вход, - поэтому смотрится разряд
        if (((new_value ^ old_value) & 1) == 0) return;
        if ((i_event_enable.value & 1) == 0) return;
        if ((m_flags & F_READY) == 0) m_latch = m_counter & COUNTER_MASK;
        m_event_load = true;
        m_flags |= F_EVENT;
        m_events++;
        update_irq();
    } else {
        // Чужой запрос по цепочке - пересчитать, что предложено процессору
        update_irq();
    }
}

//--------------------------- Прерывания ------------------------------------//

void UKNCTimer::update_irq()
{
    // Свой запрос вперёд чужого: таймер стоит на магистрали ближе к процессору.
    // Внутри - внешнее событие (310) старше обнуления (304): так у ХМ2-001 по
    // реверсу БМК (docs-external/UKNC/BMK_texts, «векторные прерывания VIRQ»)
    unsigned int own = 0;
    if ((m_flags & F_EVENT) && (m_flags & F_EVENT_IRQ))      own = m_vector_event;
    else if ((m_flags & F_ZERO) && (m_flags & F_ZERO_IRQ))   own = m_vector_zero;
    m_irq.offer(VirqLine::chain(own, i_virq_in, i_vector_in));
}

//--------------------------- Обращения с шины ------------------------------//

unsigned int UKNCTimer::get_value_word(unsigned int address)
{
    return read_register(address, false);
}

// Отладчик и LOG видят регистры, не снимая флагов и не перезагружая счётчик
unsigned UKNCTimer::get_direct(unsigned address)
{
    const unsigned int w = read_register(address & ~1u, true);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

unsigned int UKNCTimer::read_register(unsigned int address, bool peek)
{
    switch (address >> 1) {
    case REG_STATE: {
        const unsigned int v = m_flags;
        // Чтение регистра состояния снимает ошибку переполнения
        if (!peek) m_flags &= ~F_OVERFLOW;
        return v;
    }
    case REG_COUNTER: {
        // Регистр текущего значения - не сам счётчик: на шину МПИ выходят
        // только три регистра, а счётчик за ними. Обнулившись, счётчик
        // считает дальше с N - 1, а регистр держит ноль, пока его не прочтут
        // и не снимут готовность. Так программа узнаёт, что отмеренное время
        // вышло: тесты скорости команд ПП Titus читают после очереди команд
        // ноль, когда таймер переполнился, и время команды, когда нет
        // После внешнего события регистр держит то, что было на перепаде
        const unsigned int v = (m_flags & F_READY)? m_latch : (m_counter & COUNTER_MASK);
        if (peek) return v;
        // Чтение снимает обе готовности, и регистр снова идёт за счётчиком.
        // Счётчик не трогается: после нуля тест таймера Titus видит на машине
        // N - 1, после события он уже перезагружен
        if (m_flags & F_READY) {
            m_flags &= ~F_READY;
            update_irq();
        }
        return v;
    }
    default:
        // Буферный регистр только для записи. На машине чтение 177712 не
        // получает ответа, и это ловится таймаутом магистрали - в карте
        // памяти диапазон объявлен mode = w, strict = 1
        return _FFFF;
    }
}

void UKNCTimer::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    switch (address >> 1) {
    case REG_STATE: {
        // Переход останов -> пуск загружает счётчик из буферного регистра
        if ((value & F_RUN) && (m_flags & F_RUN) == 0) {
            m_counter = m_reload & COUNTER_MASK;
            m_event_load = false;
        }

        // Разряды готовностей и переполнения принадлежат аппаратуре
        m_flags = (m_flags & F_READ_ONLY) | (value & ~F_READ_ONLY & 0377);
        m_divider = 0;
        update_irq();
        break;
    }
    case REG_RELOAD:
        m_reload = value & COUNTER_MASK;
        // Пока таймер стоит, буфер виден и в счётчике: ПЗУ чистит буфер и
        // проверяет, что обнулился счётчик
        if ((m_flags & F_RUN) == 0) m_counter = m_reload;
        break;
    default:
        // Запись в регистр текущего значения машина игнорирует
        break;
    }
}

unsigned int UKNCTimer::get_value(unsigned int address)
{
    // Регистр состояния объявлен байтовым, и ПЗУ читает его через TSTB
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void UKNCTimer::set_value(unsigned int address, unsigned int value, bool force)
{
    if (address & 1) {
        // Старший байт регистра состояния не несёт ничего
        if ((address >> 1) != REG_STATE)
            set_value_word(address & ~1u, (value & 0xFF) << 8, force);
        return;
    }
    // CLRB @#177710 останавливает таймер, поэтому байтовая запись в младшую
    // половину проходит как обычная
    set_value_word(address & ~1u, value & 0xFF, force);
}

//--------------------------- Поля для сценариев ----------------------------//

void UKNCTimer::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("flags", m_flags);
    w.u("reload", m_reload);
    w.u("counter", m_counter);
    w.u("latch", m_latch);
    w.b("event_load", m_event_load);
    w.n("divider", m_divider);
    // Такты внутри шага. Прежний ключ acc считал их в миллионных долях
    // такта и при чтении пропускается: там другой масштаб
    w.n("phase", m_acc);
    w.n("zeroes", m_zeroes);
    w.n("events", m_events);
    //What is being offered on the shared request line, so that a request
    //already made is not made a second time
    w.u("offered", m_irq.offered());
}

emulator::Result UKNCTimer::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("flags", m_flags);
    r.u("reload", m_reload);
    r.u("counter", m_counter);
    // Снимок, где счётчик ещё стоял на событии: регистр - это он сам
    if (!r.u("latch", m_latch)) m_latch = (m_flags & F_ZERO)? 0 : m_counter;
    m_event_load = false;
    r.b("event_load", m_event_load);
    r.u("divider", m_divider);
    r.u("phase", m_acc);
    if (m_acc >= m_base_cycles) m_acc = 0;
    r.u("zeroes", m_zeroes);
    r.u("events", m_events);
    uint32_t offered = 0;
    if (r.u("offered", offered)) m_irq.set_offered(offered);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> UKNCTimer::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"state",   "Регистр состояния 177710",                  false});
    r.push_back({"reload",  "Буферный регистр 177712, уставка",          false});
    r.push_back({"counter", "Текущее значение счётчика 177714",          false});
    r.push_back({"period",  "Период счёта в микросекундах",              false});
    r.push_back({"running", "1, когда таймер считает",                   false});
    r.push_back({"zeroes",  "Сколько раз счётчик обнулялся с пуска",     false});
    r.push_back({"events",  "Сколько внешних событий принято с пуска",   false});
    r.push_back({"vector",  "Вектор, предложенный процессору, или 0",    false});
    return r;
}

bool UKNCTimer::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "state")   { out.values.push_back(m_flags);   return true; }
    if (field == "reload")  { out.values.push_back(m_reload);  return true; }
    if (field == "counter") { out.values.push_back(m_counter); return true; }
    if (field == "zeroes")  { out.values.push_back(m_zeroes);  return true; }
    if (field == "events")  { out.values.push_back(m_events);  return true; }
    if (field == "vector")  { out.values.push_back(m_irq.offered()); return true; }
    if (field == "running") { out.values.push_back((m_flags & F_RUN)? 1 : 0); return true; }
    if (field == "period")  {
        out.values.push_back(m_period_us * TIMER_DIVIDERS[(m_flags & F_DIV_MASK) >> 1]);
        return true;
    }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_timer(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCTimer(im, cd);
}
