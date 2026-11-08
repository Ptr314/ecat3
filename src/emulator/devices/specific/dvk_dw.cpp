// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК DW hard disk controller (КЗД 3.057.316)

#include "dvk_dw.h"
#include "emulator/utils.h"

#define SI_IE           0000100

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2

DVKDW::DVKDW(InterfaceManager *im, EmulatorConfigDevice *cd):
      DWController(im, cd, 040)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
{
}

// Регистр головки - 3 разряда, сектора на дорожке - до 16
bool DVKDW::geometry_ok() const
{
    return m_heads >= 1 && m_heads <= 8 && m_sectors >= 1 && m_sectors <= 16;
}

// Сектор с номером n лежит в образе на месте блока n - 1 своей дорожки, сектор 0 -
// на месте последнего
bool DVKDW::image_sector(unsigned int id, unsigned int &index) const
{
    if (id >= m_sectors) return false;
    index = (id + m_sectors - 1) % m_sectors;
    return true;
}

emulator::Result DVKDW::load_config(SystemData *sd)
{
    emulator::Result res = DWController::load_config(sd);
    if (!res) return res;
    m_vector = read_confg_value(cd, "vector", false, (unsigned int)0300);
    return load_image();
}

void DVKDW::reset(bool cold)
{
    m_irq.clear();
    DWController::reset(cold);
}

//--------------------------- Шина ------------------------------------------//

unsigned int DVKDW::reg_of(unsigned int address) const
{
    const unsigned int reg = (address >> 1) & 017;
    return (reg <= DW_STATUS) ? reg : (unsigned int)DW_NONE;
}

// Запрос - уровень: разрешение и ЗОА или ЗОБ
void DVKDW::update_irq()
{
    const bool request = m_ie && (m_status & (ST_DONE | ST_DRQ1)) != 0;
    m_irq.offer(VirqLine::chain(request ? m_vector : 0, i_virq_in, i_vector_in));
}

void DVKDW::request_changed(MAYBE_UNUSED unsigned int request, MAYBE_UNUSED bool up)
{
    update_irq();
}

void DVKDW::bus_init()
{
    m_ie = false;
    update_irq();
}

unsigned int DVKDW::status_bus_bits() const
{
    return m_ie ? SI_IE : 0;
}

// Записываются только сброс и разрешение (Alex_K); разрешение при поднятом ЗОА или ЗОБ
// даёт прерывание сразу
void DVKDW::status_written(unsigned int value)
{
    m_ie = (value & SI_IE) != 0;
    update_irq();
}

void DVKDW::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (callback_id == CALLBACK_IAKO) {
        // Подтверждение запроса не снимает. Процессор, приняв прерывание, забывает
        // линию, поэтому оставшийся запрос подаётся заново: обработчик, который снимет
        // ЗОА и ЗОБ обращением к регистрам, его отзовёт, а не снявший получит прерывание
        // снова, как на плате
        if ((new_value & 0xFFFF) == m_vector && m_vector != 0) m_irq.clear();
        update_irq();
        return;
    }
    if (callback_id == CALLBACK_CHAIN) {
        update_irq();
        return;
    }
    DWController::interface_callback(callback_id, new_value, old_value);
}

//--------------------------- Поля и снимок ---------------------------------//

std::vector<DeviceFieldInfo> DVKDW::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = DWController::get_device_fields();
    r.push_back({"si", "Регистр SI (174020) - то же, что status", false});
    return r;
}

bool DVKDW::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    return DWController::get_field(field == "si" ? std::string("status") : field, from, to, out);
}

void DVKDW::save_state(StateWriter &w)
{
    DWController::save_state(w);
    w.u("si", read_reg(DW_STATUS, true));
    w.u("offered", m_irq.offered());
}

emulator::Result DVKDW::load_state(const StateReader &r)
{
    const emulator::Result res = DWController::load_state(r);
    if (!res) return res;

    unsigned int si = 0;
    r.u("si", si);
    m_ie = (si & SI_IE) != 0;
    // Запрос - из разрешения и РСНУ; прежние защёлки «drqa»/«drqb» не нужны
    unsigned int offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);

    // Снимок прежнего вида (до общей прошивки с e85-knmd): РСНУ - только в «si», у
    // буфера два счётчика, разметка - признаком у записи, события - со своими номерами
    unsigned int status = 0;
    if (!r.u("status", status)) {
        m_status = si & (ST_DONE | ST_DRQ1 | ST_BUSY);
        unsigned int count_read = 0, count_write = 0;
        bool format = false;
        r.u("count_read", count_read);
        r.u("count_write", count_write);
        r.b("format", format);
        if (m_op == OP_WRITE && format) m_op = OP_FORMAT;
        // У прежней модели в кольце было два указателя: продолжается тот, что двигался
        const unsigned int count = (m_op == OP_READ || (m_op == OP_NONE && count_read != 0))
                                   ? count_read : count_write;
        set_buffer_position((count > 256) ? 0 : count);
        switch (pending_event()) {
        case 1: adopt_word_event(); break;
        // Сброс или возврат 020: кончаются одинаково
        case 2: set_pending_event(EV_INIT); break;
        // Чтение и запись уже у диска: буфер не участвует
        case 3: set_pending_event(EV_READ); m_op = OP_NONE; break;
        case 4: set_pending_event(format ? EV_FORMAT : EV_WRITE); m_op = OP_NONE; break;
        default: break;
        }
    }
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_dw(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKDW(im, cd);
}
