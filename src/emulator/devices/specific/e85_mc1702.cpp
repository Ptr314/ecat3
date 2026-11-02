// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Сопроцессор МС 1702 Электроники МС 0585: плата с К1810ВМ86 в позиции магистрали

#include <cstring>

#include "e85_mc1702.h"
#include "emulator/utils.h"

#define CALLBACK_INIT   1
#define CALLBACK_INTA   2

#define REG_ROM         0
#define REG_STATUS      2
#define REG_ADDRESS     4
#define REG_DATA        6

#define ST_BHE          0x01
#define ST_NO_RESET     0x02
#define ST_NO_VRAM      0x04
#define ST_NO_WAIT      0x08
#define ST_INTR         0x10
#define ST_NO_HALT      0x20
#define ST_NO_WRITE     0x40
#define ST_NO_READ      0x80

// Запись в регистр управления (смещение 4)
#define CTL_RES         0x08
#define CTL_INTR        0x10

// Ожидание ДОЗУ по автомату D44 (таблица 1 ТО) и схеме E3. Шаг автомата 133,3 нс:
// OSC 15 МГц, делённые на 2 в D39.1. MRDC 8288 выдаёт с начала T2, MWTC с начала T3.
// Запрос ловит D43.1, через 8 шагов RDY1 встаёт, и 8284 снимает ожидание по спаду
// CLK: цикл чтения 10 тактов CLK, записи 11. Шаги автомата и такты CLK идут как 3:2,
// фаз две, и при номинальных задержках обе дают одно и то же; запас около 24 нс.
#define DRAM_READ_WAIT  6
#define DRAM_WRITE_WAIT 7

E85Mc1702::E85Mc1702(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irqa(this, im, 1, "irqa", MODE_W)
    , i_irqb(this, im, 1, "irqb", MODE_W)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_res(this, im, 1, "res", MODE_W)
    , i_intr(this, im, 1, "intr", MODE_W)
    , i_nmi(this, im, 1, "nmi", MODE_W)
    , i_vector(this, im, 8, "vector", MODE_W)
    , i_inta(this, im, 1, "inta", MODE_R, CALLBACK_INTA)
    , m_vram(VRAM_SIZE, 0)
{
    can_read = true;
    can_write = true;
    addresable_size = PORT_VRAM + VRAM_SIZE;
    memset(m_queue, 0, sizeof(m_queue));
}

emulator::Result E85Mc1702::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    const std::string cpu = cd->get_parameter("cpu", false).value;
    m_cpu = dynamic_cast<i8086*>(im->dm->get_device_by_name(cpu, false));
    if (m_cpu == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{E85Mc1702|" + std::string(QT_TRANSLATE_NOOP("E85Mc1702", "The processor of the board is expected in 'cpu'")) + "} " + name);
    const std::string rom = cd->get_parameter("test_rom", false).value;
    m_test_rom = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(rom, false));
    if (m_test_rom == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{E85Mc1702|" + std::string(QT_TRANSLATE_NOOP("E85Mc1702", "The test ROM is expected in 'test_rom'")) + "} " + name);
    m_intr_delay = read_confg_value(cd, "intr_delay", false, (unsigned int)0);
    return emulator::Result::ok();
}

void E85Mc1702::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) std::fill(m_vram.begin(), m_vram.end(), 0);
    init_board();
}

// МУСТ: RES держит процессор, пока его не отпустит программа; INTR снят, счётчик
// тестового ПЗУ в начале (ТО, 5.1.2.3)
void E85Mc1702::init_board()
{
    m_rom_counter = 0;
    m_control = CTL_RES;
    m_intr = false;
    m_head = m_count = 0;
    m_reading = m_read_done = false;
    m_address = m_data = 0;
    m_word = false;
    set_res(true);
    if (i_nmi.value != 0) i_nmi.change(0);
    update_lines();
}

void E85Mc1702::set_res(bool on)
{
    m_res = on;
    // Сброс обрывает цикл: незавершённая запись хосту больше не видна
    if (on) {
        m_head = m_count = 0;
        m_reading = m_read_done = false;
    }
    const unsigned int v = on ? 1 : 0;
    if (i_res.value != v) i_res.change(v);
}

void E85Mc1702::update_lines()
{
    const unsigned int a = m_reading ? 1 : 0;
    if (i_irqa.value != a) i_irqa.change(a);
    const unsigned int b = (m_count != 0) ? 1 : 0;
    if (i_irqb.value != b) i_irqb.change(b);
    const unsigned int intr = m_intr ? 1 : 0;
    if (i_intr.value != intr) i_intr.change(intr);
}

void E85Mc1702::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    switch (callback_id) {
    case CALLBACK_INIT:
        if (new_value & 1) init_board();
        break;
    case CALLBACK_INTA:
        // Вектор процессор берёт сам с ~vector, подтверждение снимает INTR
        if ((new_value & 1) && m_intr) {
            m_intr = false;
            m_ints++;
            update_lines();
        }
        break;
    default:
        break;
    }
}

//------------------------- Сторона процессора платы -------------------------//

void E85Mc1702::push_write(uint16_t address, uint16_t data, bool word, bool vram)
{
    if (m_count >= QUEUE) {
        note_unsupported("write queue");
        return;
    }
    Write &w = m_queue[(m_head + m_count) % QUEUE];
    w.address = address;
    w.data = data;
    w.word = word;
    w.vram = vram;
    m_count++;
    if (m_count == 1) {
        m_address = address;
        m_data = data;
        m_word = word;
    }
    update_lines();
}

// Следующий цикл очереди - новый фронт ЗОБ: контроллер прерываний ловит фронты
void E85Mc1702::next_write()
{
    if (m_count == 0) return;
    m_head = (m_head + 1) % QUEUE;
    m_count--;
    update_lines();
    if (m_count != 0) {
        const unsigned int n = m_count;
        const Write &w = m_queue[m_head];
        m_address = w.address;
        m_data = w.data;
        m_word = w.word;
        m_count = 0;
        update_lines();
        m_count = n;
        update_lines();
    }
}

bool E85Mc1702::gate_ready()
{
    return m_count == 0 && !m_reading;
}

bool E85Mc1702::gate_input(uint16_t port, bool word)
{
    if (m_read_done) {
        if (port != m_address || word != m_word) note_unsupported("IN other than the one answered");
        return true;
    }
    if (!m_reading) {
        m_reading = true;
        m_address = port;
        m_word = word;
        m_reads++;
        update_lines();
    }
    return false;
}

uint16_t E85Mc1702::gate_in(uint16_t port, bool word)
{
    if (!m_read_done) note_unsupported("IN without host data");
    m_read_done = false;
    if (word) return m_read_data;
    return (port & 1) ? (uint16_t)(m_read_data >> 8) : (uint16_t)(m_read_data & 0xFF);
}

void E85Mc1702::gate_out(uint16_t port, uint16_t value, bool word)
{
    m_writes++;
    // Байт идёт по своей половине магистрали; вторая половина - тот же байт
    const uint16_t data = word ? value : (uint16_t)((value & 0xFF) * 0x0101);
    push_write(port, data, word, false);
}

// Завершение чтения: хост положил данные на шину процессора
void E85Mc1702::end_read(unsigned int value)
{
    if (!m_reading) return;
    m_read_data = (uint16_t)value;
    m_reading = false;
    m_read_done = true;
    update_lines();
}

unsigned int E85Mc1702::wait_states(MAYBE_UNUSED unsigned int address, bool write, MAYBE_UNUSED unsigned int offset)
{
    return write ? DRAM_WRITE_WAIT : DRAM_READ_WAIT;
}

//--------------------------- Сторона ЦП комплекса ---------------------------//

unsigned int E85Mc1702::status() const
{
    unsigned int v = 0;
    // ВНЕ активен в нуле: слово и нечётный байт
    if (!(m_word || (m_address & 1))) v |= ST_BHE;
    if (!m_res) v |= ST_NO_RESET;
    if (!(m_count != 0 && m_queue[m_head].vram)) v |= ST_NO_VRAM;
    // Процессор стоит, пока хост не ответит на цикл
    if (m_count == 0 && !m_reading) v |= ST_NO_WAIT;
    if (m_intr) v |= ST_INTR;
    if (m_cpu == nullptr || !m_cpu->waiting()) v |= ST_NO_HALT;
    if (m_count == 0) v |= ST_NO_WRITE;
    if (!m_reading) v |= ST_NO_READ;
    return v;
}

unsigned int E85Mc1702::read_reg(unsigned int reg, bool peek)
{
    switch (reg) {
    case REG_ROM: {
        const unsigned int v = m_test_rom->get_value(m_rom_counter & 03777) & 0xFF;
        if (!peek) m_rom_counter = (m_rom_counter + 1) & 03777;
        return v;
    }
    case REG_STATUS:
        return status();
    case REG_ADDRESS:
        return m_address;
    default: {
        // Шина данных между хостом и платой инвертирует: BA1M шлёт #177, а процессор
        // получает FF80h, команду 0; EMUL.TSK делает COM в обе стороны
        const unsigned int v = ~m_data & 0xFFFF;
        if (!peek) next_write();
        return v;
    }
    }
}

void E85Mc1702::write_reg(unsigned int reg, unsigned int value)
{
    value &= 0xFFFF;
    switch (reg) {
    case REG_ROM:
        m_nmis++;
        i_nmi.change(1);
        i_nmi.change(0);
        break;
    case REG_STATUS:
        m_rom_counter = 0;
        break;
    case REG_ADDRESS: {
        const bool was_intr = m_intr;
        m_control = value;
        set_res((value & CTL_RES) != 0);
        m_intr = (value & CTL_INTR) != 0;
        i_vector.change((value >> 8) & 0xFF);
        // Здесь это начало команды F-11, а на плате её последний цикл: см. intr_delay
        if (m_intr && !was_intr) m_cpu->hold(m_intr_delay);
        update_lines();
        break;
    }
    default:
        end_read(~value & 0xFFFF);
        break;
    }
}

void E85Mc1702::trace(unsigned int kind, unsigned int reg, unsigned int value)
{
    if (reg == REG_ROM && kind == 0) return;
    if (kind == 0 && m_trace_next > 0) {
        const TraceEntry &prev = m_trace[(m_trace_next - 1) & 1023];
        if (prev.kind == 0 && prev.reg == reg && prev.value == (value & 0xFFFF)) return;
    }
    TraceEntry &e = m_trace[m_trace_next & 1023];
    e.kind = (uint16_t)kind;
    e.reg = (uint16_t)reg;
    e.value = (uint16_t)value;
    e.address = m_address;
    m_trace_next++;
}

unsigned int E85Mc1702::get_value_word(unsigned int address)
{
    if (address >= (unsigned int)PORT_VRAM) {
        const unsigned int a = (address - PORT_VRAM) & (VRAM_SIZE - 2);
        return m_vram[a] | (m_vram[a + 1] << 8);
    }
    const unsigned int v = read_reg(address & 6, false) & 0xFFFF;
    trace(0, address & 6, v);
    return v;
}

void E85Mc1702::set_value_word(unsigned int address, unsigned int value, bool force)
{
    if (address >= (unsigned int)PORT_VRAM) {
        const unsigned int a = (address - PORT_VRAM) & (VRAM_SIZE - 2);
        m_vram[a] = (uint8_t)value;
        m_vram[a + 1] = (uint8_t)(value >> 8);
        if (force) return;
        m_vram_writes++;
        push_write((uint16_t)(0x8000 + a), (uint16_t)value, true, true);
        return;
    }
    trace(1, address & 6, value);
    write_reg(address & 6, value);
}

unsigned int E85Mc1702::get_value(unsigned int address)
{
    if (address >= (unsigned int)PORT_VRAM)
        return m_vram[(address - PORT_VRAM) & (VRAM_SIZE - 1)];
    const unsigned int w = read_reg(address & 6, false);
    trace(0, address & 6, w);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void E85Mc1702::set_value(unsigned int address, unsigned int value, bool force)
{
    if (address >= (unsigned int)PORT_VRAM) {
        const unsigned int a = (address - PORT_VRAM) & (VRAM_SIZE - 1);
        m_vram[a] = (uint8_t)value;
        if (force) return;
        m_vram_writes++;
        push_write((uint16_t)(0x8000 + a), (uint16_t)((value & 0xFF) * 0x0101), false, true);
        return;
    }
    trace(2, address & 6, value);
    write_reg(address & 6, value & 0xFF);
}

unsigned E85Mc1702::get_direct(unsigned address)
{
    if (address >= (unsigned int)PORT_VRAM)
        return m_vram[(address - PORT_VRAM) & (VRAM_SIZE - 1)];
    const unsigned int w = read_reg(address & 6, true);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

//--------------------------------- Поля -------------------------------------//

std::vector<DeviceFieldInfo> E85Mc1702::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"status",  "Слово состояния",                       false});
    r.push_back({"address", "Адрес цикла",                           false});
    r.push_back({"data",    "Данные цикла",                          false});
    r.push_back({"control", "Последняя запись в управление",          false});
    r.push_back({"queued",  "Записей ждут хоста",                     false});
    r.push_back({"reads",   "Сколько циклов чтения УВВ",              false});
    r.push_back({"writes",  "Сколько циклов записи УВВ",              false});
    r.push_back({"vram_writes", "Сколько записей в видеопамять",      false});
    r.push_back({"nmis",    "Сколько NMI дал хост",                   false});
    r.push_back({"ints",    "Сколько INTR принял процессор",          false});
    r.push_back({"trace",   "Обращения хоста: вид (0 чтение, 1 запись слова, 2 байта), регистр, значение, адрес цикла", true});
    return r;
}

bool E85Mc1702::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "trace") {
        if (to >= 1024) to = 1023;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        out.width = 16;
        for (unsigned int i = from; i <= to; i++) {
            const TraceEntry &e = m_trace[(m_trace_next + i) & 1023];
            out.values.push_back(e.kind);
            out.values.push_back(e.reg);
            out.values.push_back(e.value);
            out.values.push_back(e.address);
        }
        return true;
    }
    out.numeric = true;
    out.width = 16;
    if (field == "status")  { out.values.push_back(status());    return true; }
    if (field == "address") { out.values.push_back(m_address);   return true; }
    if (field == "data")    { out.values.push_back(m_data);      return true; }
    if (field == "control") { out.values.push_back(m_control);   return true; }
    if (field == "queued")  { out.values.push_back(m_count);     return true; }
    out.width = 32;
    if (field == "reads")   { out.values.push_back(m_reads);     return true; }
    if (field == "writes")  { out.values.push_back(m_writes);    return true; }
    if (field == "vram_writes") { out.values.push_back(m_vram_writes); return true; }
    if (field == "nmis")    { out.values.push_back(m_nmis);      return true; }
    if (field == "ints")    { out.values.push_back(m_ints);      return true; }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void E85Mc1702::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.hex("vram", m_vram.data(), m_vram.size());
    w.n("rom_counter", m_rom_counter);
    w.n("control", m_control);
    w.b("res", m_res);
    w.b("intr", m_intr);
    w.b("reading", m_reading);
    w.b("read_done", m_read_done);
    w.u("read_data", m_read_data);
    w.u("address", m_address);
    w.u("data", m_data);
    w.b("word", m_word);
    w.n("head", m_head);
    w.n("count", m_count);
    for (unsigned int i = 0; i < m_count; i++) {
        const Write &q = m_queue[(m_head + i) % QUEUE];
        const unsigned int a[3] = {q.address, q.data, (q.word ? 1u : 0u) | (q.vram ? 2u : 0u)};
        w.array(("queue" + std::to_string(i)).c_str(), a, 3);
    }
    w.n("reads", m_reads);
    w.n("writes", m_writes);
    w.n("vram_writes", m_vram_writes);
    w.n("nmis", m_nmis);
    w.n("ints", m_ints);
}

emulator::Result E85Mc1702::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.hex("vram", m_vram.data(), m_vram.size());
    r.u("rom_counter", m_rom_counter);
    r.u("control", m_control);
    r.b("res", m_res);
    r.b("intr", m_intr);
    r.b("reading", m_reading);
    r.b("read_done", m_read_done);
    unsigned int v = 0;
    if (r.u("read_data", v)) m_read_data = (uint16_t)v;
    if (r.u("address", v)) m_address = (uint16_t)v;
    if (r.u("data", v)) m_data = (uint16_t)v;
    r.b("word", m_word);
    r.u("head", m_head);
    r.u("count", m_count);
    m_head %= QUEUE;
    if (m_count > QUEUE) m_count = 0;
    for (unsigned int i = 0; i < m_count; i++) {
        unsigned int a[3] = {0, 0, 0};
        r.array(("queue" + std::to_string(i)).c_str(), a, 3);
        Write &q = m_queue[(m_head + i) % QUEUE];
        q.address = (uint16_t)a[0];
        q.data = (uint16_t)a[1];
        q.word = (a[2] & 1) != 0;
        q.vram = (a[2] & 2) != 0;
    }
    r.u("reads", m_reads);
    r.u("writes", m_writes);
    r.u("vram_writes", m_vram_writes);
    r.u("nmis", m_nmis);
    r.u("ints", m_ints);
    return emulator::Result::ok();
}

ComputerDevice * create_e85_mc1702(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Mc1702(im, cd);
}
