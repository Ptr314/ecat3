// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: К1801ВП1-033 as a byte parallel interface, printer output (LP11)

#include "k1801vp1_033.h"
#include "emulator/utils.h"

#define REG_CSR         0
#define REG_DATA        1

#define CSR_TR          0000200     // разряд 7 - требование передачи
#define CSR_IE          0000100     // разряд 6 - разрешение прерывания

#define CALLBACK_CHAIN   1
#define CALLBACK_IAKO    2
#define CALLBACK_INIT    3
#define CALLBACK_REQUEST 4

K1801VP1_033::K1801VP1_033(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_data(this, im, 8, "data", MODE_W)
    , i_strobe(this, im, 1, "strobe", MODE_W)
    , i_request(this, im, 1, "request", MODE_R, CALLBACK_REQUEST)
{
    can_read = true;
    can_write = true;
    addresable_size = 4;
}

emulator::Result K1801VP1_033::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_vector = read_confg_value(cd, "vector", false, (unsigned int)0200);
    return emulator::Result::ok();
}

// Запрос принтера. Неподключённый вход висит в 1, как вход ТТЛ на плате без
// принтера: строб тогда не снимается никогда
bool K1801VP1_033::request() const
{
    return i_request.linked == 0 || (i_request.value & 1) != 0;
}

void K1801VP1_033::set_strobe(bool on)
{
    m_strobe = on;
    if ((i_strobe.value & 1) != (on ? 1u : 0u)) i_strobe.change(on ? 1 : 0);
}

// TR и запрос прерывания по нему: встаёт, когда TR появляется при IET,
// снимается с пропажей TR
void K1801VP1_033::update_tr()
{
    const bool tr = request() && !m_strobe;
    if (tr && !m_tr && (m_csr & CSR_IE)) m_pending = true;
    if (!tr) m_pending = false;
    m_tr = tr;
    update_irq();
}

void K1801VP1_033::update_irq()
{
    m_irq.offer(VirqLine::chain(m_pending ? m_vector : 0, i_virq_in, i_vector_in));
}

// INIT снимает разрешение прерывания и запрос. Строб ему не подчиняется:
// он держится, пока принтер не взял байт
void K1801VP1_033::init_registers()
{
    m_csr = 0;
    m_pending = false;
    update_tr();
}

void K1801VP1_033::reset(bool cold)
{
    if (cold) {
        m_data = 0;
        set_strobe(false);
    }
    init_registers();
}

void K1801VP1_033::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_REQUEST) {
        // Принтер снял запрос - байт взят, строб снимается
        if (m_strobe && !request()) set_strobe(false);
        // Запрос вернулся, а строб так и стоит: байт записали неготовому
        // принтеру (занятому или вынутому). Строб - уровень, и принтер,
        // готовый при выданном стробе, берёт байт; модели принтера нужен
        // фронт, поэтому строб выдаётся заново
        else if (m_strobe && (new_value & 1) && !(old_value & 1)) {
            set_strobe(false);
            set_strobe(true);
        }
        update_tr();
        return;
    }
    if (callback_id == CALLBACK_INIT) {
        // Сброс делает первая половина импульса RESET, как у DL11
        if (new_value & 1) init_registers();
        return;
    }
    if (callback_id == CALLBACK_IAKO) {
        const unsigned int vector = new_value & 0xFFFF;
        if (vector == 0) return;
        if (vector == m_vector) m_pending = false;
    }
    update_irq();
}

//--------------------------- Регистры --------------------------------------//

unsigned int K1801VP1_033::get_value_word(unsigned int address)
{
    if (((address >> 1) & 1) == REG_CSR)
        return (m_tr ? CSR_TR : 0) | (m_csr & CSR_IE);
    return 0;                       // РИ только для записи
}

void K1801VP1_033::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (((address >> 1) & 1) == REG_CSR) {
        const bool was = (m_csr & CSR_IE) != 0;
        m_csr = value & CSR_IE;
        const bool now = (m_csr & CSR_IE) != 0;
        if (!now) m_pending = false;
        else if (!was && m_tr) m_pending = true;
        update_irq();
        return;
    }
    // Байт в защёлку и строб. Принтер, взявший байт, снимает запрос прямо из
    // change() строба, и строб снимается там же
    m_data = value & 0xFF;
    i_data.change(m_data);
    set_strobe(true);
    update_tr();
}

unsigned K1801VP1_033::get_direct(unsigned address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

unsigned int K1801VP1_033::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void K1801VP1_033::set_value(unsigned int address, unsigned int value, bool force)
{
    // Всё, что пишет программа, - в младшем байте; запись старшего байта
    // ничего не меняет (так же у DL11)
    if (address & 1) return;
    set_value_word(address, value & 0xFF, force);
}

//--------------------------- Сценарии --------------------------------------//

std::vector<DeviceFieldInfo> K1801VP1_033::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"csr",       "Регистр состояния (РСП)",                               false});
    r.push_back({"data",      "Байт в защёлке",                                        false});
    r.push_back({"strobe",    "1, пока строб выдан, а принтер байт не взял",           false});
    r.push_back({"vector",    "Вектор, предложенный процессору, или 0",                false});
    return r;
}

bool K1801VP1_033::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "csr")       { out.values.push_back(get_value_word(0));     return true; }
    if (field == "data")      { out.values.push_back(m_data);                return true; }
    if (field == "strobe")    { out.values.push_back(m_strobe? 1 : 0);       return true; }
    if (field == "vector")    { out.values.push_back(m_irq.offered());       return true; }

    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

void K1801VP1_033::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("csr", m_csr);
    w.u("data", m_data);
    w.b("strobe", m_strobe);
    w.b("tr", m_tr);
    w.b("pending", m_pending);
    //What is offered on the line, which the restored line itself does not
    //say: without it the acknowledge would never withdraw a pending request
    w.u("offered", m_irq.offered());
}

emulator::Result K1801VP1_033::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("csr", m_csr);
    r.u("data", m_data);
    r.b("strobe", m_strobe);
    r.b("tr", m_tr);
    r.b("pending", m_pending);
    unsigned offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    return emulator::Result::ok();
}

ComputerDevice * create_k1801vp1_033(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new K1801VP1_033(im, cd);
}
