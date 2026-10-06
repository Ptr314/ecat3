// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DEC RX11 floppy controller (ДВК DX, ГМД-70)

#include <cstring>

#include "dvk_dx.h"
#include "emulator/utils.h"

#define RX_GO       0000001
#define RX_UNIT     0000020
#define RX_DONE     0000040
#define RX_IE       0000100
#define RX_TR       0000200
#define RX_INIT     0040000
#define RX_ERROR    0100000

#define RXES_CRC    0000001
#define RXES_ID     0000004     // сброс закончен
#define RXES_DD     0000100     // сектор с меткой удаления
#define RXES_DRDY   0000200     // привод готов

#define FN_FILL     0
#define FN_EMPTY    1
#define FN_WRITE    2
#define FN_READ     3
#define FN_STATUS   5
#define FN_WRITE_DD 6
#define FN_ERROR    7

// Коды ошибок RX11 (регистр ошибок, функция 7)
#define ERR_DRIVE0  0010        // привод 0 не нашёл дорожку 0 при сбросе
#define ERR_TRACK   0040        // дорожка больше 76
#define ERR_SECTOR  0070        // сектор не найден
#define ERR_NODISK  0110        // нет дискеты - то же «не найден»

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2
#define CALLBACK_INIT   3

DVKDX::DVKDX(InterfaceManager *im, EmulatorConfigDevice *cd):
      FDC(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_select(this, im, 1, "select", MODE_W)
    , i_motor_on(this, im, 1, "motor_on", MODE_W)
{
    m_clocked = true;
}

emulator::Result DVKDX::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    std::vector<FDD*> list;
    res = load_drives(1, DVK_DX_DRIVES, list);
    if (!res) return res;
    m_drives_count = list.size();
    for (unsigned int i = 0; i < m_drives_count; i++) m_drives[i] = list[i];

    m_vector = read_confg_value(cd, "vector", false, (unsigned int)0264);

    // Сектор и шаг головки ГМД-70: оборот 166 мс на 26 секторов, около
    // 10 мс на операцию с поиском дорожки
    const unsigned int us = read_confg_value(cd, "op_us", false, (unsigned int)10000);
    const uint64_t clock = (m_system_clock != 0) ? m_system_clock : 4000000;
    m_op_ticks = clock * us / 1000000;
    if (m_op_ticks == 0) m_op_ticks = 1;
    return emulator::Result::ok();
}

void DVKDX::reset(MAYBE_UNUSED bool cold)
{
    m_irq.clear();
    m_ie = false;
    init_controller();
}

// Сброс контроллера: сектор 1 дорожки 1 привода 0 - в буфер, «готово»
void DVKDX::init_controller()
{
    m_phase = PH_IDLE;
    m_func = 0;
    m_unit = 0;
    m_tr = false;
    m_err = false;
    m_pending = false;
    m_error_code = 0;
    m_left = 0;
    memset(m_buffer, 0, sizeof(m_buffer));
    bool deleted = false;
    if (m_drives_count > 0) read_sector(m_drives[0], 1, 1, deleted);
    m_rxes = RXES_ID;
    m_done = true;
    m_db = status();
    i_select.change(0);
    i_motor_on.change(1);
    update_irq();
}

unsigned int DVKDX::status() const
{
    unsigned int v = m_rxes;
    FDD * f = (m_unit < m_drives_count) ? m_drives[m_unit] : nullptr;
    if (f != nullptr && f->get_loaded() != 0) v |= RXES_DRDY;
    return v;
}

bool DVKDX::get_busy()
{
    return m_phase == PH_BUSY;
}

unsigned int DVKDX::get_selected_drive()
{
    return m_unit;
}

void DVKDX::update_irq()
{
    m_irq.offer(VirqLine::chain((m_pending && m_ie) ? m_vector : 0, i_virq_in, i_vector_in));
}

void DVKDX::finish(bool error, unsigned int code)
{
    m_phase = PH_IDLE;
    i_motor_on.change(1);
    m_tr = false;
    m_err = error;
    if (error) m_error_code = code;
    m_done = true;
    m_db = (m_func == FN_ERROR) ? m_error_code : status();
    m_pending = true;
    update_irq();
}

// Диск в приводе - дорожки IBM 3740 FM (режим fm_ibm). Привод с образом
// секторов - конфигурация без этого режима или снимок, сделанный до него, -
// перестраивается в дорожки при первом обращении
FDD * DVKDX::track_drive(FDD * f)
{
    if (f == nullptr || f->get_loaded() == 0) return nullptr;
    if (!f->whole_track()) f->rebuild_ibm_fm();
    return f->whole_track() ? f : nullptr;
}

// Сектор в буфер - по его заголовку на дорожке; deleted - метка удаления F8
bool DVKDX::read_sector(FDD * drive, unsigned int track, unsigned int sector, bool &deleted)
{
    FDD * f = track_drive(drive);
    std::vector<uint8_t> t;
    if (f == nullptr || !f->read_track((int)track, 0, t)) return false;
    int size = 0;
    const int mark = dsk_tools::ibm_fm_find_sector(t.data(), t.size(), (int)track, (int)sector, deleted, size);
    if (mark < 0 || size != DVK_DX_SECTOR_SIZE) return false;
    memcpy(m_buffer, t.data() + mark + 1, DVK_DX_SECTOR_SIZE);
    return true;
}

// Операция с диском, когда дорожка и сектор приняты
void DVKDX::execute()
{
    FDD * drive = (m_unit < m_drives_count) ? m_drives[m_unit] : nullptr;
    FDD * f = track_drive(drive);
    if (f == nullptr) { finish(true, ERR_NODISK); return; }
    if (m_track >= DVK_DX_TRACKS) { finish(true, ERR_TRACK); return; }
    if (m_sector < 1 || m_sector > DVK_DX_SECTORS) { finish(true, ERR_SECTOR); return; }

    m_rxes &= ~RXES_DD;
    if (m_func == FN_READ) {
        bool deleted = false;
        if (!read_sector(f, m_track, m_sector, deleted)) { finish(true, ERR_SECTOR); return; }
        // Сектор с меткой удаления читается, и RXES это показывает
        if (deleted) m_rxes |= RXES_DD;
        m_reads++;
    } else {
        if (f->is_protected()) { finish(true, ERR_SECTOR); return; }
        std::vector<uint8_t> t;
        bool deleted = false;
        int size = 0;
        const int mark = f->read_track((int)m_track, 0, t)
            ? dsk_tools::ibm_fm_find_sector(t.data(), t.size(), (int)m_track, (int)m_sector, deleted, size) : -1;
        if (mark < 0 || size != DVK_DX_SECTOR_SIZE) { finish(true, ERR_SECTOR); return; }
        // Функция 6 пишет метку удаления F8, функция 2 - обычную FB
        dsk_tools::ibm_fm_put_sector(t.data(), t.size(), mark, m_buffer, DVK_DX_SECTOR_SIZE, m_func == FN_WRITE_DD);
        f->write_track((int)m_track, 0, t);
        m_writes++;
    }
    finish(false, 0);
}

void DVKDX::clock(unsigned int counter)
{
    if (m_phase != PH_BUSY) return;
    if (m_left > counter) {
        m_left -= counter;
        return;
    }
    m_left = 0;
    if (m_func == FN_READ || m_func == FN_WRITE || m_func == FN_WRITE_DD)
        execute();
    else
        finish(false, 0);
}

void DVKDX::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT) {
        // INIT снимает и разрешение прерывания
        if (new_value & 1) { m_ie = false; init_controller(); }
        return;
    }
    if (callback_id == CALLBACK_IAKO) {
        if ((new_value & 0xFFFF) == m_vector && m_vector != 0) m_pending = false;
    }
    update_irq();
}

unsigned DVKDX::get_direct(unsigned address)
{
    unsigned int v;
    if ((address & 2) == 0) {
        // Функция и привод только пишутся: загрузчик ждёт готовности по
        // маске 100247, где они тоже есть
        v = (m_done ? RX_DONE : 0) | (m_ie ? RX_IE : 0)
          | (m_tr ? RX_TR : 0) | (m_err ? RX_ERROR : 0);
    } else
        v = (m_phase == PH_EMPTY) ? m_buffer[m_pos] : m_db;
    return (address & 1) ? (v >> 8) & 0xFF : v;
}

unsigned int DVKDX::get_value_word(unsigned int address)
{
    const unsigned int v = get_direct(address & ~1u);
    if ((address & 2) != 0 && m_phase == PH_EMPTY && m_tr) {
        // Байт взят - следующий; на последнем буфер выдан
        if (++m_pos >= DVK_DX_SECTOR_SIZE) {
            m_pos = 0;
            finish(false, 0);
        }
    }
    return v;
}

unsigned int DVKDX::get_value(unsigned int address)
{
    if (address & 1) return get_direct(address) & 0xFF;
    return get_value_word(address) & 0xFF;
}

void DVKDX::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if ((address & 2) == 0) {
        if (value & RX_INIT) {
            init_controller();
            return;
        }
        const bool was_ie = m_ie;
        m_ie = (value & RX_IE) != 0;
        if ((value & RX_GO) && m_phase == PH_IDLE) {
            m_func = (value >> 1) & 7;
            m_unit = (value & RX_UNIT) ? 1 : 0;
            i_select.change(m_unit);
            m_done = false;
            m_err = false;
            m_pending = false;
            m_pos = 0;
            switch (m_func) {
            case FN_FILL:  m_phase = PH_FILL;  m_tr = true; break;
            case FN_EMPTY: m_phase = PH_EMPTY; m_tr = true; break;
            case FN_WRITE: case FN_READ: case FN_WRITE_DD:
                m_phase = PH_SECTOR; m_tr = true; break;
            default:
                // Состояние, ошибки и резервная 4 - без обращения к диску
                m_phase = PH_BUSY;
                m_left = 1;
                break;
            }
        } else if (!was_ie && m_ie && m_done) {
            // Разрешение при готовности тоже даёт прерывание
            m_pending = true;
        }
        update_irq();
        return;
    }

    value &= 0377;
    switch (m_phase) {
    case PH_FILL:
        m_buffer[m_pos] = (uint8_t)value;
        if (++m_pos >= DVK_DX_SECTOR_SIZE) {
            m_pos = 0;
            finish(false, 0);
        }
        break;
    case PH_SECTOR:
        m_sector = value & 037;
        m_phase = PH_TRACK;
        break;
    case PH_TRACK:
        m_track = value & 0177;
        m_tr = false;
        m_phase = PH_BUSY;
        // Головка опущена, пока идёт обмен: по этой линии горит индикатор
        // привода. Номер привода уже выставлен - привод берёт его вместе с ней
        i_motor_on.change(0);
        m_left = m_op_ticks;
        break;
    default:
        break;
    }
}

void DVKDX::set_value(unsigned int address, unsigned int value, bool force)
{
    const unsigned int base = address & ~1u;
    value &= 0xFF;
    if ((base & 2) == 0) {
        const unsigned int cur = m_ie ? RX_IE : 0;
        if (address & 1) set_value_word(base, (cur & 0xFF) | (value << 8), force);
        else set_value_word(base, value, force);
    } else if ((address & 1) == 0)
        set_value_word(base, value, force);
}

std::vector<DeviceFieldInfo> DVKDX::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = FDC::get_device_fields();
    r.push_back({"csr",      "RXCS, как его читает программа",   false});
    r.push_back({"rxes",     "Регистр состояния RXES",           false});
    r.push_back({"position", "Привод, дорожка, сектор",          false});
    r.push_back({"reads",    "Секторов прочитано",               false});
    r.push_back({"writes",   "Секторов записано",                false});
    return r;
}

bool DVKDX::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "csr")    { out.values.push_back(get_direct(0) | (get_direct(1) << 8)); return true; }
    if (field == "rxes")   { out.values.push_back(status()); return true; }
    if (field == "reads")  { out.values.push_back(m_reads);  return true; }
    if (field == "writes") { out.values.push_back(m_writes); return true; }
    if (field == "position") {
        out.values.push_back(m_unit);
        out.values.push_back(m_track);
        out.values.push_back(m_sector);
        return true;
    }
    out.numeric = false;
    return FDC::get_field(field, from, to, out);
}

void DVKDX::save_state(StateWriter &w)
{
    FDC::save_state(w);
    w.n("phase", m_phase);
    w.u("func", m_func);
    w.u("unit", m_unit);
    w.b("done", m_done);
    w.b("ie", m_ie);
    w.b("tr", m_tr);
    w.b("err", m_err);
    w.b("pending", m_pending);
    w.u("db", m_db);
    w.u("rxes", m_rxes);
    w.u("error_code", m_error_code);
    w.u("sector", m_sector);
    w.u("track", m_track);
    w.n("pos", m_pos);
    w.array("buffer", m_buffer, DVK_DX_SECTOR_SIZE);
    w.n64("left", m_left);
    w.u("offered", m_irq.offered());
}

emulator::Result DVKDX::load_state(const StateReader &r)
{
    emulator::Result res = FDC::load_state(r);
    if (!res) return res;
    r.u("phase", m_phase);
    r.u("func", m_func);
    r.u("unit", m_unit);
    r.b("done", m_done);
    r.b("ie", m_ie);
    r.b("tr", m_tr);
    r.b("err", m_err);
    r.b("pending", m_pending);
    r.u("db", m_db);
    r.u("rxes", m_rxes);
    r.u("error_code", m_error_code);
    r.u("sector", m_sector);
    r.u("track", m_track);
    r.u("pos", m_pos);
    r.array("buffer", m_buffer, DVK_DX_SECTOR_SIZE);
    r.n64("left", m_left);
    uint32_t offered = 0;
    if (r.u("offered", offered)) m_irq.set_offered(offered);
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_dx(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKDX(im, cd);
}
