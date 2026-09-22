// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Юниор ФВ-6506: магнитофон с дистанционным управлением (лента .bt)

#include <cstring>

#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"
#include "unior_tape.h"

// Разряды порта B дополнительного ВВ55 - команды лентопротяжке. Сверено с
// конфигурацией машины в эмуляторе b2m и с тем, как их выдает ПЗУ: команда
// ввода с ленты начинается с $10 (FC48), запись - с $18 (FC43), а заканчивается
// все импульсом $80 (FC37)
#define CTL_REC      0x08
#define CTL_PLAY     0x10
#define CTL_BACK     0x20
#define CTL_FORWARD  0x40
#define CTL_STOP     0x80

//Тишина на ленте: байты идут вплотную, по восемь битовых интервалов, так что
//пропуск двух байт - это уже пауза между записями
#define SILENCE_BITS 16

#define CB_CONTROL  10
#define CB_DATA_IN  11

// Преамбула каждой записи: два байта чередующихся бит на подстройку, затем
// маркер. Синхросимвол $E6 идет уже первым байтом данных
static const uint8_t BT_PREAMBLE[4] = {0xAA, 0xAA, 0x19, 0x00};

UniorTape::UniorTape(InterfaceManager *im, EmulatorConfigDevice *cd):
      TapeRecorder(im, cd)
    , i_control(this, im, 8, "control", MODE_R, CB_CONTROL)
    , i_data_out(this, im, 8, "data_out", MODE_W)
    , i_data_in(this, im, 8, "data_in", MODE_R, CB_DATA_IN)
    , i_ready(this, im, 1, "ready", MODE_W)
{
    m_clocked = true;
}

emulator::Result UniorTape::load_config(SystemData *sd)
{
    emulator::Result res = TapeRecorder::load_config(sd);
    if (!res) return res;

    m_fast_speed = read_confg_value(cd, "fast_speed", false, (unsigned int)15);
    if (m_fast_speed < 1) m_fast_speed = 1;

    m_ticks_per_bit = (baud_rate > 0)?(m_system_clock / baud_rate):1;
    if (m_ticks_per_bit < 1) m_ticks_per_bit = 1;

    //Длина чистой кассеты: полчаса - это сторона С-60
    const unsigned int blank_seconds = read_confg_value(cd, "blank_length", false, (unsigned int)1800);
    m_blank_bits = (uint64_t)blank_seconds * baud_rate;

    m_carrier = false;
    i_ready.change(1);          // Сигнала нет: DSR неактивен

    const std::string image = cd->get_parameter("image", false).value;
    if (!image.empty())
    {
        const std::string file = find_file_location(sd, image);
        if (file.empty())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Tape image file not found")) + "} " + image);
        res = load_file(file, "");
        if (!res) return res;
    }

    return emulator::Result::ok();
}

void UniorTape::reset(bool cold)
{
    TapeRecorder::reset(cold);
    m_transport = T_STOP;
    m_ticks = 0;
    m_write_buf.clear();
    m_carrier = false;
    i_ready.change(1);
    locate(m_pos);
}

// Лента - один поток: пауза, преамбула с данными, снова пауза. Здесь только
// пересчитывается общая длина, положение головки не трогается
void UniorTape::rebuild_timeline()
{
    m_total_bits = 0;
    for (size_t i = 0; i < m_records.size(); i++)
        m_total_bits += m_records[i].gap + (uint64_t)m_records[i].bytes.size() * 8;

    //После последней записи на кассете остается ракорд. Без него лента
    //кончалась бы ровно на последнем байте, а машина этот байт читает уже
    //после того, как он прошел головку: контрольная сумма блока, записанного
    //в самом конце ленты, терялась бы всегда
    m_total_bits += (uint64_t)baud_rate * 2;

    //У чистой кассеты длина своя: она не кончается там, где кончилось
    //записанное, иначе разметка уперлась бы в конец ленты на первом же секторе
    if (m_blank && m_total_bits < m_blank_bits) m_total_bits = m_blank_bits;
}

uint64_t UniorTape::record_start(size_t index) const
{
    uint64_t p = 0;
    for (size_t i = 0; i < index && i < m_records.size(); i++)
        p += m_records[i].gap + (uint64_t)m_records[i].bytes.size() * 8;
    if (index < m_records.size()) p += m_records[index].gap;
    return p;
}

// Где стоит головка после перемотки: в паузе или внутри записи
void UniorTape::locate(uint64_t pos)
{
    uint64_t p = 0;
    for (size_t i = 0; i < m_records.size(); i++)
    {
        const uint64_t gap_end = p + m_records[i].gap;
        const uint64_t rec_end = gap_end + (uint64_t)m_records[i].bytes.size() * 8;
        if (pos < gap_end) { m_rec = i; m_in_gap = true;  m_byte = 0; return; }
        if (pos < rec_end) { m_rec = i; m_in_gap = false; m_byte = (size_t)((pos - gap_end) / 8); return; }
        p = rec_end;
    }
    m_rec = m_records.size();
    m_in_gap = true;
    m_byte = 0;
}

// Образ кассеты в файле .bt: слово заголовка, затем записи
// [пауза:4][длина:4][преамбула:4 + данные:длина]. Одна и та же раскладка
// читается при загрузке, пишется при сохранении и кладется в снимок состояния:
// носитель - это состояние, и кассета, на которую писала машина, не должна
// теряться ни там, ни там
emulator::Result UniorTape::parse_image(const uint8_t * raw, size_t size, const std::string &where)
{
    if (size < 4)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Unable to read tape image")) + "} " + where);

    std::vector<Record> recs;
    size_t off = 4;             // Первое слово файла - заголовок, а не запись
    while (off + 8 <= size)
    {
        const uint32_t gap = (uint32_t)(raw[off] | (raw[off+1] << 8) | (raw[off+2] << 16) | ((uint32_t)raw[off+3] << 24));
        const uint32_t len = (uint32_t)(raw[off+4] | (raw[off+5] << 8) | (raw[off+6] << 16) | ((uint32_t)raw[off+7] << 24));
        const size_t sync = off + 8;
        if (sync + 4 + len > size) break;
        if (memcmp(&raw[sync], BT_PREAMBLE, 4) != 0)
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Not a Unior tape image")) + "} " + where);

        Record r;
        r.gap = gap;
        r.bytes.assign(raw + sync, raw + sync + 4 + len);
        recs.push_back(r);
        off = sync + 4 + len;
    }

    //Файл из одного заголовка - это чистая кассета, ее и размечают с нуля.
    //Все, что длиннее, но записей не дало, - испорченный образ
    if (recs.empty() && size > 8)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Tape image holds no records")) + "} " + where);

    m_blank = recs.empty();

    m_file_header = (uint32_t)(raw[0] | (raw[1] << 8) | (raw[2] << 16) | ((uint32_t)raw[3] << 24));
    m_records.swap(recs);
    rebuild_timeline();

    return emulator::Result::ok();
}

static void put32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

void UniorTape::build_image(std::vector<uint8_t> &out) const
{
    out.clear();
    if (m_records.empty()) return;

    size_t total = 4;
    for (size_t i = 0; i < m_records.size(); i++) total += 8 + m_records[i].bytes.size() + 4;
    out.reserve(total);

    put32(out, m_file_header);
    for (size_t i = 0; i < m_records.size(); i++)
    {
        const Record &r = m_records[i];
        //Запись машины начинается с той же преамбулы, что и запись из файла,
        //но полагаться на это нельзя: без нее образ не загрузится обратно
        const bool has_preamble = r.bytes.size() >= 4 && memcmp(r.bytes.data(), BT_PREAMBLE, 4) == 0;
        put32(out, r.gap);
        put32(out, (uint32_t)(has_preamble?(r.bytes.size() - 4):r.bytes.size()));
        if (!has_preamble) out.insert(out.end(), BT_PREAMBLE, BT_PREAMBLE + 4);
        out.insert(out.end(), r.bytes.begin(), r.bytes.end());
    }
}

emulator::Result UniorTape::load_file(const std::string &file_name, MAYBE_UNUSED const std::string &fmt)
{
    //Файл из одного заголовка - чистая кассета, ее и размечают с нуля
    const long long fsize = dsk_tools::utf8_file_size(file_name);
    if (fsize < 4)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Unable to read tape image")) + "} " + file_name);

    std::vector<uint8_t> raw((size_t)fsize);
    {
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (!file.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Unable to read tape image")) + "} " + file_name);
        file.read(reinterpret_cast<char*>(raw.data()), fsize);
        file.close();
    }

    emulator::Result res = parse_image(raw.data(), raw.size(), file_name);
    if (!res) return res;

    m_dirty = false;
    m_pos = 0;
    m_ticks = 0;
    m_write_buf.clear();
    locate(0);
    set_transport(T_STOP);
    loaded_name = dsk_tools::get_filename(file_name);
    notify_state();

    return emulator::Result::ok();
}

void UniorTape::set_transport(Transport t)
{
    if (m_transport == t) return;

    if (m_transport == T_RECORD && t != T_RECORD) commit_write();

    m_transport = t;
    m_ticks = 0;
    //Перемотка двигает только положение головки: какая запись под ней, никто
    //при этом не считает. Без пересчета лента после отката назад играла бы
    //в пустоту до той записи, на которой ее остановили в прошлый раз - машина
    //ждала бы синхросимвола, которого уже не будет
    locate(m_pos);
    if (t == T_RECORD)
    {
        m_write_buf.clear();
        m_write_start = m_pos;
    }

    //Несущая снимается сразу: в паузе и на стоящей ленте сигнала нет
    if (t != T_RECORD) set_carrier(false);
    else set_carrier(true);

    //Лента стоит - в динамике тишина, а не последний уровень
    m_bit_out = -1;
    m_write_bits = 0;
    if (t == T_STOP) sound_level(0);

    //Окно магнитофона живет на тех же полях, что и у обычной лентопротяжки:
    //признак движения плюс признак записи
    is_recording = (t == T_RECORD);
    motor_on = (t != T_STOP);
    set_tape_mode(tape_mode_of(t));
    notify_state();
}

void UniorTape::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == CB_CONTROL)
    {
        const unsigned int v = new_value & 0xFF;
        //Стоп сильнее всего: ПЗУ гасит лентопротяжку импульсом $80
        if (v & CTL_STOP)           set_transport(T_STOP);
        else if (v & CTL_REC)       set_transport((v & CTL_PLAY)?T_RECORD:T_STOP);
        else if (v & CTL_PLAY)      set_transport(T_PLAY);
        else if (v & CTL_BACK)      set_transport(T_BACK);
        else if (v & CTL_FORWARD)   set_transport(T_FORWARD);
        else                        set_transport(T_STOP);
        return;
    }

    if (callback_id == CB_DATA_IN)
    {
        //Байт от передатчика ВВ51. Пока лентопротяжка в записи, он ложится на
        //ленту; в остальных положениях машина пишет в никуда, как и на живой
        if (m_transport == T_RECORD)
        {
            //Запись ложится туда, где головка была на первом байте, а не там,
            //где включили лентопротяжку: драйвер TCP/M запускает ее и ждет по
            //таймеру почти три секунды, и настоящий блок лежит на ленте именно
            //на этом расстоянии от своего маркера
            if (m_write_buf.empty()) m_write_start = m_pos;
            m_write_last = m_pos;
            m_write_buf.push_back((uint8_t)(new_value & 0xFF));
            //Этот же байт идет в динамик: пишущую ленту слышно, как и читающую
            m_write_byte = (uint8_t)(new_value & 0xFF);
            m_write_bits = 8;
        }
        return;
    }

    TapeRecorder::interface_callback(callback_id, new_value, old_value);
}

// Записанное ложится на ленту одной записью. Головка стирает то, поверх чего
// пишет, поэтому заменяются записи, начало которых она прошла уже в записи.
// Та, что началась раньше, остается: это маркер, который машина только что
// прочитала, а писать начала после него - без маркера блок стал бы не найти.
void UniorTape::commit_write()
{
    if (m_write_buf.size() < 5) { m_write_buf.clear(); return; }

    std::vector<uint64_t> starts(m_records.size(), 0);
    {
        uint64_t p = 0;
        for (size_t i = 0; i < m_records.size(); i++)
        {
            p += m_records[i].gap;
            starts[i] = p;
            p += (uint64_t)m_records[i].bytes.size() * 8;
        }
    }

    const uint64_t ws = m_write_start;
    const uint64_t we = ws + (uint64_t)m_write_buf.size() * 8;
    //Запас на неточность модели лентопротяжки: машина метит в паузу перед
    //блоком, а где именно она окажется, зависит от того, когда ее остановили
    const uint64_t slack = (baud_rate > 16)?(baud_rate / 16):1;

    size_t first = m_records.size();
    size_t last = m_records.size();
    for (size_t i = 0; i < m_records.size(); i++)
    {
        if (starts[i] + slack < ws) continue;
        if (starts[i] > we) break;
        if (first == m_records.size()) first = i;
        last = i + 1;
    }

    if (first == m_records.size())
    {
        //Голова не задела ни одной записи: новая встает между соседями
        first = 0;
        while (first < m_records.size() && starts[first] <= ws) first++;
        last = first;
    }

    const uint64_t prev_end = (first == 0)?0
        :(starts[first - 1] + (uint64_t)m_records[first - 1].bytes.size() * 8);
    const bool has_next = last < m_records.size();
    const uint64_t next_start = has_next?starts[last]:0;

    Record r;
    r.gap = (uint32_t)((ws > prev_end)?(ws - prev_end):0);
    r.bytes = m_write_buf;

    m_records.erase(m_records.begin() + (long)first, m_records.begin() + (long)last);
    m_records.insert(m_records.begin() + (long)first, r);

    if (has_next)
    {
        const uint64_t new_end = ws + (uint64_t)m_records[first].bytes.size() * 8;
        m_records[first + 1].gap = (uint32_t)((next_start > new_end)?(next_start - new_end):(baud_rate / 4));
    }

    m_writes++;
    m_dirty = true;
    m_write_buf.clear();
    rebuild_timeline();
    locate(m_pos);
}

// Чем занята лентопротяжка - в тех понятиях, которыми живет окно магнитофона
unsigned int UniorTape::tape_mode_of(Transport t)
{
    switch (t)
    {
        case T_FORWARD: return TAPE_FORWARD;
        case T_BACK:    return TAPE_BACK;
        case T_STOP:    return TAPE_STOPPED;
        default:        return TAPE_READ;
    }
}

// DSR у ВВ51 - это «есть сигнал с ленты», а не «лентопротяжка крутится». На
// этом держится все чтение: и ПЗУ (FC04 выходит с переносом, когда сигнала
// нет), и загрузчик TCP/M, который читает запись до пропадания несущей, а не
// по счетчику. С DSR по движению ленты загрузчик читал бы кассету насквозь
void UniorTape::set_carrier(bool on)
{
    if (m_carrier == on) return;
    m_carrier = on;
    i_ready.change(on?0:1);
}

void UniorTape::advance_bit()
{
    if (m_transport == T_BACK) {
        if (m_pos > 0) m_pos--;
        return;
    }

    if (m_pos < m_total_bits) m_pos++;

    if (m_transport != T_PLAY) return;

    //Байт отдается приемнику на границе байта внутри записи. Преамбулу ВВ51
    //отбросит сам: он ищет синхросимвол
    if (m_rec >= m_records.size()) { set_carrier(false); return; }
    const Record &r = m_records[m_rec];
    const uint64_t gap_end = record_start(m_rec);
    set_carrier(m_pos > gap_end && m_pos <= gap_end + (uint64_t)r.bytes.size() * 8);
    if (m_pos <= gap_end) return;

    const uint64_t in_rec = m_pos - gap_end;
    if (in_rec % 8 != 0) return;
    const size_t index = (size_t)(in_rec / 8) - 1;
    if (index < r.bytes.size())
    {
        i_data_out.change(r.bytes[index]);
        m_bytes_read++;
        m_byte = index;
        m_in_gap = false;
    }
    if (in_rec >= (uint64_t)r.bytes.size() * 8)
    {
        m_rec++;
        m_in_gap = true;
        m_byte = 0;
    }
}

// Уровень в динамике. Перепады считаются: сценарию нечем услышать ленту, а
// счетчик перепадов - ровно то, что отличает звучащую ленту от молчащей
void UniorTape::sound_level(unsigned int level)
{
    if (level == m_sound_level) return;
    m_sound_level = level;
    m_sound_edges++;
    tape_sound(level);
}

// Бит под головкой - для динамика. Данные машине отдаются байтами, но лента
// записана битами, и слышно должно быть их
int UniorTape::current_bit() const
{
    if (m_transport != T_PLAY || !m_carrier) return -1;
    if (m_rec >= m_records.size()) return -1;
    const Record &r = m_records[m_rec];
    const uint64_t gap_end = record_start(m_rec);
    if (m_pos <= gap_end) return -1;
    const uint64_t in_rec = m_pos - gap_end;
    const size_t index = (size_t)((in_rec - 1) / 8);
    if (index >= r.bytes.size()) return -1;
    return (r.bytes[index] >> ((in_rec - 1) % 8)) & 1;
}

// Бит, который передатчик ВВ51 сейчас кладет на ленту. Запись слышна так же,
// как чтение: байт разбирается младшим разрядом вперед, как его и шлет ВВ51
int UniorTape::next_write_bit()
{
    if (m_write_bits == 0) return -1;
    const int bit = (m_write_byte >> (8 - m_write_bits)) & 1;
    m_write_bits--;
    return bit;
}

void UniorTape::clock(unsigned int counter)
{
    if (m_transport != T_STOP && m_ticks_per_bit > 0)
    {
        //Перемотка идет быстрее воспроизведения во столько же раз, во сколько
        //у настоящего магнитофона
        const unsigned int speed = (m_transport == T_BACK || m_transport == T_FORWARD)?m_fast_speed:1;
        //Шаг - половина битового интервала: сами данные приходят байтами, но
        //в динамик уходит уровень, который меняется в середине каждого бита.
        //Это манчестер, которым лента и записана, и звучит он как у остальных
        //машин: в паузах тихо, на записи - характерный треск
        m_ticks += (uint64_t)counter * speed * 2;
        while (m_ticks >= m_ticks_per_bit)
        {
            m_ticks -= m_ticks_per_bit;
            m_half = !m_half;
            if (!m_half)
            {
                advance_bit();
                m_bit_out = (m_transport == T_RECORD)?next_write_bit():current_bit();
            }
            sound_level((m_bit_out < 0)?0:((unsigned int)m_bit_out ^ (m_half?1u:0u)));
        }
        //Машина перестала слать байты, а лента идет - на ней пошла пауза, и
        //записанное до нее кончилось. Так пишет COPY: лентопротяжку в запись
        //ставит человек, а программа выкладывает блоки один за другим с
        //паузами между ними. Без этого вся кассета стала бы одной записью
        if (m_transport == T_RECORD && !m_write_buf.empty()
            && m_pos > m_write_last + SILENCE_BITS) commit_write();

        if ((m_transport == T_PLAY || m_transport == T_FORWARD) && m_pos >= m_total_bits) set_transport(T_STOP);
        if (m_transport == T_BACK && m_pos == 0) set_transport(T_STOP);
    }
    clock_sound(counter);
}

void UniorTape::play()
{
    if (!m_records.empty()) set_transport(T_PLAY);
}

void UniorTape::stop()
{
    set_transport(T_STOP);
}

void UniorTape::rewind()
{
    set_transport(T_STOP);
    m_pos = 0;
    m_ticks = 0;
    locate(0);
    notify_state();
}

void UniorTape::set_recording(bool recording)
{
    set_transport(recording?T_RECORD:T_STOP);
}

int UniorTape::get_position()
{
    return (baud_rate > 0)?(int)(m_pos / baud_rate):0;
}

int UniorTape::get_total()
{
    return (baud_rate > 0)?(int)(m_total_bits / baud_rate):0;
}

int UniorTape::get_mode()
{
    return tape_mode_of(m_transport);
}

// Для окна это ответ на вопрос «есть ли что сохранять»: у ленты-накопителя
// сохраняют не последнюю запись, а всю кассету, и только если машина на нее
// писала
unsigned UniorTape::get_record_size()
{
    if (!m_dirty) return 0;
    size_t total = 4;
    for (size_t i = 0; i < m_records.size(); i++) total += 8 + m_records[i].bytes.size() + 4;
    return (unsigned)total;
}

void UniorTape::get_save_data(std::vector<uint8_t> &out)
{
    build_image(out);
    if (!out.empty()) m_dirty = false;
}

std::string UniorTape::get_record_name()
{
    return loaded_name.empty()?std::string("tape.bt"):loaded_name;
}

std::vector<uint8_t> * UniorTape::get_record_data()
{
    return &m_write_buf;
}

std::vector<DeviceFieldInfo> UniorTape::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = TapeRecorder::get_device_fields();
    r.push_back({"transport", "Transport state: 0 stop, 1 play, 2 record, 3 back, 4 forward", false});
    r.push_back({"records",   "Records on the tape",                                          false});
    r.push_back({"record",    "Record the head is at, and the byte inside it",                false});
    r.push_back({"bytes",     "Bytes handed to the machine since reset",                      false});
    r.push_back({"writes",    "Records written by the machine",                               false});
    r.push_back({"sound",     "Edges the tape has sent to the speaker",                       false});
    return r;
}

bool UniorTape::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "transport") { out.numeric = true; out.width = 8;  out.values.push_back((unsigned int)m_transport); return true; }
    if (field == "records")   { out.numeric = true; out.width = 16; out.values.push_back((unsigned int)m_records.size()); return true; }
    if (field == "record")
    {
        out.numeric = true; out.width = 16;
        out.values.push_back((unsigned int)m_rec);
        out.values.push_back((unsigned int)m_byte);
        return true;
    }
    if (field == "bytes")     { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_bytes_read); return true; }
    if (field == "writes")    { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_writes); return true; }
    if (field == "sound")     { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_sound_edges); return true; }

    return TapeRecorder::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> UniorTape::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = TapeRecorder::get_device_commands();
    r.push_back({"seek", "Move the head to a record by its number"});
    return r;
}

emulator::Result UniorTape::send_command(const std::string &command, const std::string &parameters)
{
    if (command == "seek")
    {
        std::vector<std::string> p = split_params(parameters);
        if (p.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "Command 'seek' expects a record number")) + "}");
        const unsigned int n = parse_numeric_value(p[0]);
        if (n >= m_records.size())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{UniorTape|" + std::string(QT_TRANSLATE_NOOP("UniorTape", "No such record on the tape")) + "} " + p[0]);
        set_transport(T_STOP);
        m_pos = record_start(n);
        m_ticks = 0;
        locate(m_pos);
        notify_state();
        return emulator::Result::ok();
    }

    return TapeRecorder::send_command(command, parameters);
}

void UniorTape::save_state(StateWriter &w)
{
    TapeRecorder::save_state(w);
    //Кассета едет в снимке целиком: базовый класс сохраняет свой буфер, а у
    //этой ленты данные лежат записями, и записанное машиной есть только здесь
    std::vector<uint8_t> image;
    build_image(image);
    if (!image.empty()) w.blob("tape", name + ".bt", image.data(), image.size());
    w.b("dirty", m_dirty);
    w.n("transport", (unsigned int)m_transport);
    w.n64("pos", m_pos);
    w.n64("ticks", m_ticks);
    w.b("half", m_half);
    w.n64("sound", m_sound_edges);
    w.u("write_byte", m_write_byte, 8);
    w.n("write_bits", m_write_bits);
    w.n64("writes", m_writes);
    w.n64("bytes_read", m_bytes_read);
}

emulator::Result UniorTape::load_state(const StateReader &r)
{
    emulator::Result res = TapeRecorder::load_state(r);
    if (!res) return res;

    std::vector<uint8_t> image;
    if (r.blob("tape", image) && !image.empty())
    {
        res = parse_image(image.data(), image.size(), name);
        if (!res) return res;
    }
    r.b("dirty", m_dirty);

    unsigned int t = 0;
    if (r.u("transport", t)) m_transport = (Transport)t;
    r.n64("pos", m_pos);
    r.n64("ticks", m_ticks);
    r.b("half", m_half);
    r.n64("sound", m_sound_edges);
    r.u("write_byte", m_write_byte);
    r.u("write_bits", m_write_bits);
    r.n64("writes", m_writes);
    r.n64("bytes_read", m_bytes_read);
    locate(m_pos);
    m_carrier = false;
    i_ready.change(1);
    return emulator::Result::ok();
}

ComputerDevice * create_unior_tape(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UniorTape(im, cd);
}
