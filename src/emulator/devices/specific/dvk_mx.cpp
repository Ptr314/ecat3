// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК floppy controller MX (3.057.122)

#include "dvk_mx.h"
#include "emulator/utils.h"

#define MX_DRVSE_L  0000002
#define MX_DRIVE    0000014
#define MX_STEP     0000020
#define MX_DIR      0000040
#define MX_MON      0000100
#define MX_TIMER    0000200
#define MX_ERR      0000400
#define MX_INDEX    0001000
#define MX_WP       0002000
#define MX_TRK0     0004000
#define MX_TOPHEAD  0010000
#define MX_WRITE    0020000
#define MX_GO       0040000
#define MX_TR       0100000

// Разряды, которые программа пишет и потом читает обратно
#define MX_STORED   (MX_WRITE | MX_TOPHEAD | MX_MON | MX_DIR | MX_DRIVE | MX_DRVSE_L)

#define MX_SYNC         0000363
#define MX_LEAD_WORDS   8           // нулевых слов от индекса до синхрослова
#define MX_SECTOR_WORDS (DVK_MX_SECTOR_SIZE / 2)
#define MX_MAX_TRACK    84          // дальше головка не идёт

DVKMX::DVKMX(InterfaceManager *im, EmulatorConfigDevice *cd):
      FDC(im, cd)
    , i_select(this, im, 2, "select", MODE_W)
    , i_side(this, im, 1, "side", MODE_W)
    , i_motor_on(this, im, 1, "motor_on", MODE_W)
{
    m_clocked = true;
}

emulator::Result DVKMX::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    std::vector<FDD*> list;
    res = load_drives(1, DVK_MX_MAX_DRIVES, list);
    if (!res) return res;
    m_drives_count = list.size();

    // Линия стороны общая для всех приводов, как на кабеле
    LinkData ld;
    ld.s.i = &i_side;
    ld.s.shift = 0;
    ld.s.mask = create_mask(1, 0);
    for (unsigned int i = 0; i < m_drives_count; i++) {
        m_drives[i].fdd = list[i];
        m_drives[i].track = 0;
        ld.d.i = im->get_interface_by_name(list[i]->name, "side");
        if (ld.d.i == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{DVKMX|" + std::string(QT_TRANSLATE_NOOP("DVKMX", "Interface not found")) + "} " + list[i]->name + ":side");
        ld.d.shift = 0;
        ld.d.mask = create_mask(1, 0);
        ld.s.i->connect(ld.s, ld.d, false);
    }

    // 300 об/мин, ячейка FM 4 мкс - слово из 16 разрядов идёт 128 мкс
    const uint64_t clock = (m_system_clock != 0) ? m_system_clock : 8000000;
    m_rev_ticks = clock / 5;
    m_word_ticks = clock * 128 / 1000000;
    if (m_word_ticks == 0) m_word_ticks = 1;
    m_index_ticks = clock / 250;        // индексный импульс 4 мс
    m_timer_ticks = clock / 2000;

    update_drive_lines();
    return emulator::Result::ok();
}

void DVKMX::reset(MAYBE_UNUSED bool cold)
{
    m_csr = 0;
    m_rbuf = m_wbuf = 0;
    m_tr = m_err = m_timer_tick = false;
    m_op = OP_IDLE;
    m_step_left = 0;
    m_timer_left = 0;
    update_drive_lines();
}

DVKMX::Drive * DVKMX::current()
{
    if (m_csr & MX_DRVSE_L) return nullptr;
    const unsigned int n = (m_csr & MX_DRIVE) >> 2;
    if (n >= m_drives_count) return nullptr;
    return &m_drives[n];
}

bool DVKMX::selected_loaded()
{
    Drive * d = current();
    return d != nullptr && d->fdd != nullptr && d->fdd->get_loaded() != 0;
}

void DVKMX::update_drive_lines()
{
    i_select.change((m_csr & MX_DRIVE) >> 2);
    // Привод берёт сторону как ~side, по выводу SIDE кабеля
    i_side.change((m_csr & MX_TOPHEAD) ? 0 : 1);
    i_motor_on.change(((m_csr & MX_MON) && current() != nullptr) ? 0 : 1);
}

bool DVKMX::get_busy()
{
    return m_op != OP_IDLE || ((m_csr & MX_MON) != 0 && current() != nullptr);
}

unsigned int DVKMX::get_selected_drive()
{
    return (m_csr & MX_DRIVE) >> 2;
}

// Записанные дорожки относятся к образу, в который их писали: другой образ
// (или вынутый) их забывает
void DVKMX::sync_written(Drive * d)
{
    const unsigned int gen = d->fdd->get_generation();
    if (d->adopt) {
        d->generation = gen;
        d->adopt = false;
    }
    if (d->generation != gen) {
        d->written.clear();
        d->generation = gen;
    }
}

// Дорожка под головкой выбранного привода, словами: записанная программой -
// как она её записала, иначе так, как её записал бы стандартный драйвер MX
void DVKMX::build_track()
{
    const unsigned int words = (unsigned int)(m_rev_ticks / m_word_ticks);
    m_track.assign(words, 0);

    Drive * d = current();
    if (d == nullptr || d->fdd == nullptr || d->fdd->get_loaded() == 0) return;
    FDD * f = d->fdd;
    const int side = (m_csr & MX_TOPHEAD) ? 1 : 0;
    const int track = d->track;
    if (track < 0 || track >= f->get_tracks() || side >= f->get_sides()) return;

    sync_written(d);
    const auto kept = d->written.find((unsigned int)(track * 2 + side));
    if (kept != d->written.end()) {
        const size_t n = (kept->second.size() < m_track.size()) ? kept->second.size() : m_track.size();
        std::copy(kept->second.begin(), kept->second.begin() + n, m_track.begin());
        return;
    }

    if (f->get_sector_size() != DVK_MX_SECTOR_SIZE || f->get_sectors() < DVK_MX_SECTORS) return;

    unsigned int p = MX_LEAD_WORDS;
    m_track[p++] = MX_SYNC;
    m_track[p++] = (uint16_t)track;
    for (int s = 0; s < DVK_MX_SECTORS; s++) {
        uint16_t sum = 0;
        if (f->SeekSector(track, s + 1) < 0) return;
        for (int i = 0; i < MX_SECTOR_WORDS; i++) {
            const uint16_t lo = f->ReadNextByte();
            const uint16_t w = (uint16_t)(lo | (f->ReadNextByte() << 8));
            m_track[p++] = w;
            sum = (uint16_t)(sum + w);
        }
        m_track[p++] = sum;
    }
    for (int i = 0; i < 3; i++) m_track[p++] = (uint16_t)(0101400 | (track * 2 + side));
}

// Записанная дорожка обратно в образ: от синхрослова номер дорожки и 11
// секторов с суммами. Дорожка без синхрослова - стёртая, её сектора остаются
// как были: в плоском образе пустую дорожку не выразить
void DVKMX::store_track()
{
    m_writes++;
    m_written = (unsigned int)m_track.size();
    m_last_write = m_track;
    Drive * d = current();
    if (d == nullptr || d->fdd == nullptr || d->fdd->get_loaded() == 0) return;
    FDD * f = d->fdd;
    if (f->is_protected()) return;
    const int side = (m_csr & MX_TOPHEAD) ? 1 : 0;
    const int track = d->track;
    if (track < 0 || track >= f->get_tracks() || side >= f->get_sides()) return;

    // Дорожка остаётся такой, какой её записали, - и стёртая тоже
    sync_written(d);
    d->written[(unsigned int)(track * 2 + side)] = m_track;

    if (f->get_sector_size() != DVK_MX_SECTOR_SIZE || f->get_sectors() < DVK_MX_SECTORS) return;

    size_t p = 0;
    while (p < m_track.size() && m_track[p] != MX_SYNC) p++;
    p += 2;     // синхрослово и номер дорожки
    if (p + DVK_MX_SECTORS * (MX_SECTOR_WORDS + 1) > m_track.size()) return;

    for (int s = 0; s < DVK_MX_SECTORS; s++) {
        if (f->SeekSector(track, s + 1) < 0) return;
        for (int i = 0; i < MX_SECTOR_WORDS; i++) {
            const uint16_t w = m_track[p++];
            f->WriteNextByte((uint8_t)(w & 0xFF));
            f->WriteNextByte((uint8_t)(w >> 8));
        }
        p++;    // сумма
    }
}

// Слово записи на дорожку: после индекса - поверх её начала
void DVKMX::put_word(uint16_t w)
{
    if (!m_wrapped) {
        m_track.push_back(w);
        return;
    }
    if (m_wrap < m_track.size()) m_track[m_wrap] = w;
    m_wrap++;
}

// Индекс: пуск, ждавший его, начинается, чтение кончается. Запись индекс
// не останавливает (так и в MAME): она идёт, пока программа подаёт слова
// или не снимет разряд записи. TSTMX пишет заполнитель 177777, пока не
// увидит индекс, и только потом бросает - остановленная на индексе запись
// не давала ему готовности, и тест висел
void DVKMX::start_word()
{
    switch (m_op) {
    case OP_WAIT_READ:
        build_track();
        m_reads++;
        m_op = OP_READ;
        // Контроллер молчит до синхрослова, и оно же - первое отданное слово
        m_pos = 0;
        while (m_pos < m_track.size() && m_track[m_pos] != MX_SYNC) m_pos++;
        m_word_left = m_word_ticks * (m_pos + 1);
        break;
    case OP_READ:
        m_op = OP_IDLE;
        break;
    case OP_WAIT_WRITE:
        m_op = OP_WRITE;
        m_track.clear();
        m_wrapped = false;
        m_wrap = 0;
        m_shift = m_wbuf;
        m_tr = true;
        m_word_left = m_word_ticks;
        break;
    case OP_WRITE:
        m_wrapped = true;
        m_wrap = 0;
        break;
    default:
        break;
    }
}

void DVKMX::clock(unsigned int counter)
{
    if (m_step_left > 0) {
        if (m_step_left > counter) m_step_left -= counter;
        else {
            m_step_left = 0;
            m_csr &= ~MX_STEP;
            Drive * d = current();
            if (d != nullptr) {
                m_steps++;
                if (m_step_dir) {
                    if (d->track < MX_MAX_TRACK) d->track++;
                } else {
                    if (d->track > 0) d->track--;
                }
            }
        }
    }

    if (m_timer_left > 0) {
        if (m_timer_left > counter) m_timer_left -= counter;
        else {
            m_timer_tick = true;
            m_timer_left = m_timer_ticks;
        }
    }

    uint64_t left = counter;
    while (left > 0) {
        // Шаг до ближайшего события: индекса или конца слова
        uint64_t step = m_rev_ticks - m_angle;
        const bool word_running = (m_op == OP_READ || m_op == OP_WRITE);
        if (word_running && m_word_left < step) step = m_word_left;
        if (left < step) step = left;

        m_angle += step;
        left -= step;
        if (word_running) m_word_left -= step;

        if (word_running && m_word_left == 0) {
            if (m_op == OP_READ) {
                if (m_pos < m_track.size()) {
                    m_rbuf = m_track[m_pos++];
                    m_tr = true;
                }
                m_word_left = m_word_ticks;
            } else {
                put_word((uint16_t)m_shift);
                if (m_tr) {
                    // Программа не поспела со следующим словом
                    m_err = true;
                    m_underruns++;
                    store_track();
                    m_op = OP_IDLE;
                } else {
                    m_shift = m_wbuf;
                    m_tr = true;
                    m_word_left = m_word_ticks;
                }
            }
        }

        if (m_angle >= m_rev_ticks) {
            m_angle = 0;
            start_word();
        }
    }
}

unsigned int DVKMX::get_value_word(unsigned int address)
{
    if ((address & 2) == 0) {
        const unsigned int v = get_direct(address & ~1u);
        m_timer_tick = false;
        return v;
    }
    m_tr = false;
    return m_rbuf;
}

unsigned DVKMX::get_direct(unsigned address)
{
    if ((address & 2) != 0) {
        const unsigned int v = m_rbuf;
        return (address & 1) ? (v >> 8) : v;
    }
    unsigned int v = m_csr & (MX_STORED | MX_STEP);
    if (m_tr) v |= MX_TR;
    if (m_err) v |= MX_ERR;
    if (m_timer_tick) v |= MX_TIMER;
    Drive * d = current();
    if (d != nullptr) {
        if (d->track == 0) v |= MX_TRK0;
        if (d->fdd != nullptr && d->fdd->get_loaded() != 0) {
            if (d->fdd->is_protected()) v |= MX_WP;
            if (m_angle < m_index_ticks) v |= MX_INDEX;
        }
    }
    return (address & 1) ? (v >> 8) : v;
}

unsigned int DVKMX::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return ((address & 1) ? (w >> 8) : w) & 0xFF;
}

void DVKMX::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if ((address & 2) != 0) {
        m_wbuf = value & 0xFFFF;
        m_tr = false;
        return;
    }

    m_trace[m_trace_count++ % TRACE_SIZE] = value & 0xFFFF;

    // Снятый разряд записи кончает запись: драйвер так и делает после
    // последнего слова (BIC #60241), не дожидаясь индекса, и тут же пускает
    // контрольное чтение. Слово, которое ещё уходило, остаётся на дорожке
    if (m_op == OP_WRITE && (value & MX_WRITE) == 0) {
        put_word((uint16_t)m_shift);
        store_track();
        m_op = OP_IDLE;
        m_tr = false;
    }

    m_csr = value & (MX_STORED | (m_csr & MX_STEP));
    update_drive_lines();

    // Таймер 2 кГц: разряд 7 его включает, и тот же разряд виден взведённым
    m_timer_tick = (value & MX_TIMER) != 0;
    m_timer_left = (value & MX_TIMER) ? m_timer_ticks : 0;

    Drive * d = current();
    if (d == nullptr) return;

    if (value & MX_STEP) {
        // Импульс шага 2 мс, всё это время разряд виден взведённым; головка
        // сдвигается в его конце
        m_step_dir = (value & MX_DIR) != 0;
        m_csr |= MX_STEP;
        m_step_left = (m_system_clock != 0 ? m_system_clock : 8000000) / 500;
    }

    if (value & MX_GO) {
        m_tr = false;
        m_err = false;
        m_op = (value & MX_WRITE) ? OP_WAIT_WRITE : OP_WAIT_READ;
    }
}

void DVKMX::set_value(unsigned int address, unsigned int value, bool force)
{
    // Байтовая запись меняет свою половину регистра: остальное - как было
    const unsigned int base = address & ~1u;
    unsigned int old = ((base & 2) != 0) ? m_wbuf : (m_csr & MX_STORED);
    const unsigned int w = (address & 1) ? ((old & 0x00FF) | ((value & 0xFF) << 8))
                                         : ((old & 0xFF00) | (value & 0xFF));
    set_value_word(base, w, force);
}

std::vector<DeviceFieldInfo> DVKMX::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = FDC::get_device_fields();
    r.push_back({"csr",       "MXCS, как его читает программа",              false});
    r.push_back({"track",     "Дорожка под головкой выбранного привода",     false});
    r.push_back({"op",        "0 - стоит, 1/2 - чтение (ждёт индекса/идёт), 3/4 - запись", false});
    r.push_back({"reads",     "Сколько дорожек прочитано",                   false});
    r.push_back({"writes",    "Сколько дорожек записано",                    false});
    r.push_back({"underruns", "Сколько записей оборвано: слово не поспело",  false});
    r.push_back({"steps",     "Сколько шагов головки",                       false});
    r.push_back({"written",   "Слов в последней записанной дорожке",         false});
    r.push_back({"lastwrite", "Слова последней записанной дорожки (from,to)", false});
    r.push_back({"trace",     "Последние записи в MXCS, старые первыми",     false});
    return r;
}

bool DVKMX::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "csr")       { out.values.push_back(get_direct(0) | (get_direct(1) << 8)); return true; }
    if (field == "op")        { out.values.push_back(m_op);        return true; }
    if (field == "reads")     { out.values.push_back(m_reads);     return true; }
    if (field == "writes")    { out.values.push_back(m_writes);    return true; }
    if (field == "underruns") { out.values.push_back(m_underruns); return true; }
    if (field == "steps")     { out.values.push_back(m_steps);     return true; }
    if (field == "written")   { out.values.push_back(m_written);   return true; }
    if (field == "lastwrite") {
        for (unsigned int i = from; i <= to && i < m_last_write.size(); i++)
            out.values.push_back(m_last_write[i]);
        return true;
    }
    if (field == "trace") {
        const unsigned int n = m_trace_count < TRACE_SIZE ? m_trace_count : TRACE_SIZE;
        for (unsigned int i = m_trace_count - n; i < m_trace_count; i++)
            out.values.push_back(m_trace[i % TRACE_SIZE]);
        return true;
    }
    if (field == "track") {
        Drive * d = current();
        out.values.push_back(d != nullptr ? (unsigned int)d->track : 0);
        return true;
    }
    out.numeric = false;
    return FDC::get_field(field, from, to, out);
}

void DVKMX::save_state(StateWriter &w)
{
    FDC::save_state(w);
    w.u("csr", m_csr);
    w.u("rbuf", m_rbuf);
    w.u("wbuf", m_wbuf);
    w.b("tr", m_tr);
    w.b("err", m_err);
    w.b("timer_tick", m_timer_tick);
    w.n("op", m_op);
    w.n("pos", m_pos);
    w.u("shift", m_shift);
    w.b("wrapped", m_wrapped);
    w.n("wrap", m_wrap);
    w.n64("angle", m_angle);
    w.n64("word_left", m_word_left);
    w.n64("timer_left", m_timer_left);
    w.n64("step_left", m_step_left);
    w.b("step_dir", m_step_dir);
    for (unsigned int i = 0; i < m_drives_count; i++)
        w.n_at("track", i, (unsigned int)m_drives[i].track);
    // Записанные дорожки: их служебных слов в образе нет
    for (unsigned int i = 0; i < m_drives_count; i++) {
        const Drive &d = m_drives[i];
        if (d.written.empty()) continue;
        w.push(("written" + std::to_string(i)).c_str());
        std::vector<uint32_t> keys;
        for (const auto &e : d.written) keys.push_back(e.first);
        w.n("count", (unsigned int)keys.size());
        w.array("keys", keys.data(), keys.size());
        for (const auto &e : d.written) {
            w.n(("words" + std::to_string(e.first)).c_str(), (unsigned int)e.second.size());
            if (!e.second.empty())
                w.array(("track" + std::to_string(e.first)).c_str(), e.second.data(), e.second.size());
        }
        w.pop();
    }
    // Дорожка, которая читается или пишется: записанной ещё нет в образе
    if (m_op == OP_WRITE || m_op == OP_READ) {
        w.n("track_words", (unsigned int)m_track.size());
        if (!m_track.empty()) w.array("track_data", m_track.data(), m_track.size());
    }
}

emulator::Result DVKMX::load_state(const StateReader &r)
{
    emulator::Result res = FDC::load_state(r);
    if (!res) return res;
    r.u("csr", m_csr);
    r.u("rbuf", m_rbuf);
    r.u("wbuf", m_wbuf);
    r.b("tr", m_tr);
    r.b("err", m_err);
    r.b("timer_tick", m_timer_tick);
    r.u("op", m_op);
    r.u("pos", m_pos);
    r.u("shift", m_shift);
    r.b("wrapped", m_wrapped);
    r.u("wrap", m_wrap);
    r.n64("angle", m_angle);
    r.n64("word_left", m_word_left);
    r.n64("timer_left", m_timer_left);
    r.n64("step_left", m_step_left);
    r.b("step_dir", m_step_dir);
    for (unsigned int i = 0; i < m_drives_count; i++) {
        uint32_t t = 0;
        if (r.u_at("track", i, t)) m_drives[i].track = (int)t;
        // Образ привода снимок открывает заново, и номер его другой: дорожки
        // относятся к тому, что окажется в приводе
        Drive &d = m_drives[i];
        d.written.clear();
        d.adopt = true;
        const StateReader sub = r.sub(("written" + std::to_string(i)).c_str());
        uint32_t count = 0;
        if (!sub.u("count", count) || count == 0 || count > 1024) continue;
        std::vector<uint32_t> keys(count, 0);
        if (!sub.array("keys", keys.data(), keys.size())) continue;
        for (uint32_t key : keys) {
            uint32_t words = 0;
            if (!sub.u(("words" + std::to_string(key)).c_str(), words) || words > 65536) continue;
            std::vector<uint16_t> data(words, 0);
            if (words > 0) sub.array(("track" + std::to_string(key)).c_str(), data.data(), data.size());
            d.written[key] = data;
        }
    }
    m_track.clear();
    uint32_t words = 0;
    if (r.u("track_words", words) && words > 0) {
        m_track.assign(words, 0);
        r.array("track_data", m_track.data(), m_track.size());
    }
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_mx(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKMX(im, cd);
}
