// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК MY floppy controller board (КМД 3.057.136): the link between its own processor and the machine

#include "dvk_kmd.h"
#include "emulator/utils.h"

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2
#define CALLBACK_INIT   3

#define CSR_GO          0000001
#define CSR_DONE        0000040
#define CSR_IE          0000100
#define CSR_TR          0000200
#define CSR_ERR         0100000
#define CSR_HOST_RD     (CSR_ERR | CSR_TR | CSR_IE | CSR_DONE)
#define CSR_HOST_WR     (CSR_IE | 077437)
#define CSR_LOCAL_WR    (CSR_ERR | CSR_TR | CSR_DONE)

// Адреса на устройстве (базы диапазонов в конфигурации)
#define BASE_HOST       0000000     // 172140-172143
#define BASE_LOCAL      0000100     // 177100-177103
#define BASE_MODE       0000110     // 177716
#define BASE_DMA        0400000     // 40000-77777

// Регистр режима платы (177716): так он читается в MAME
#define MODE_VALUE      0010001

DVKKMD::DVKKMD(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_local_init(this, im, 1, "local_init", MODE_W)
{
    m_clocked = true;   // снимает импульс INIT платы
    can_read = true;
    can_write = true;
    addresable_size = 0500000;
}

emulator::Result DVKKMD::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    // Память машины, с которой плата обменивается сама
    const std::string mapper = read_confg_value(cd, "host_mapper", false, std::string("mapper"));
    m_host = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name(mapper, false));
    if (m_host == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKKMD|" + std::string(QT_TRANSLATE_NOOP("DVKKMD", "Memory mapper is expected")) + "} " + mapper);

    m_vector = read_confg_value(cd, "vector", false, (unsigned int)0170);
    const unsigned bits = read_confg_value(cd, "address_bits", false, (unsigned int)16);
    m_address_mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1);
    i_virq.change(1);
    i_local_init.change(0);
    return emulator::Result::ok();
}

void DVKKMD::reset(bool cold)
{
    AddressableDevice::reset(cold);
    m_cr = m_go = m_dr = 0;
    m_pending = false;
    m_irq.clear();
    i_virq.change(1);
}

void DVKKMD::clock(MAYBE_UNUSED unsigned int counter)
{
    if (m_pulse) {
        m_pulse = false;
        i_local_init.change(0);
    }
}

void DVKKMD::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT) {
        // INIT магистрали машины: сбросить регистры и подать прошивке IRQ2
        if (new_value & 1) {
            m_cr = m_go = m_dr = 0;
            m_pending = false;
            m_pulse = true;
            i_local_init.change(1);
            update_irq();
        }
        return;
    }
    if (callback_id == CALLBACK_IAKO) {
        const unsigned vector = new_value & 0xFFFF;
        if (vector != 0 && vector == m_vector) m_pending = false;
    }
    update_irq();
}

void DVKKMD::update_irq()
{
    m_irq.offer(VirqLine::chain(m_pending ? m_vector : 0, i_virq_in, i_vector_in));
}

// «Готово» от прошивки: появление при разрешённом прерывании - запрос,
// снятие - отзыв запроса
void DVKKMD::set_done(bool done)
{
    const bool was = (m_cr & CSR_DONE) != 0;
    if (done && !was && (m_cr & CSR_IE)) m_pending = true;
    if (!done && was) m_pending = false;
}

// Адрес в памяти машины для окна платы: разряды 0-13 - смещение в окне,
// 14-21 - младший байт MYDR. Прошивка (подпрограмма 007550) сама сдвигает
// туда разряды 14-15 адреса буфера и 16-21 из MYCSR и пишет этот байт в обе
// половины MYDR - по одной на каждую ВП1-095. Разряды 16-21 доходят до шины
// через перемычки S1-S6, у 16-разрядной машины их нет
uint32_t DVKKMD::host_address(unsigned offset) const
{
    return (((uint32_t)(m_dr & 0377) << 14) | (offset & 037776)) & m_address_mask;
}

unsigned int DVKKMD::get_value_word(unsigned int address)
{
    if (address >= BASE_DMA) {
        m_dma_reads++;
        return m_host->read_word(host_address(address - BASE_DMA)) & 0xFFFF;
    }
    switch (address & ~1u) {
    case BASE_HOST:      return m_cr & CSR_HOST_RD;
    case BASE_HOST + 2:  return m_dr;
    case BASE_LOCAL:     return m_cr;
    case BASE_LOCAL + 2: return m_dr;
    case BASE_MODE:      return MODE_VALUE;
    default:             return 0;
    }
}

unsigned DVKKMD::get_direct(unsigned address)
{
    // Окно в память машины смотрится без счёта обращений
    unsigned w;
    if (address >= BASE_DMA) w = m_host->get_direct(host_address(address - BASE_DMA))
                                  | (m_host->get_direct(host_address(address - BASE_DMA) + 1) << 8);
    else w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

unsigned int DVKKMD::get_value(unsigned int address)
{
    if (address >= BASE_DMA) {
        m_dma_reads++;
        return m_host->read(host_address(address - BASE_DMA) | (address & 1)) & 0xFF;
    }
    const unsigned w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void DVKKMD::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    value &= 0xFFFF;
    if (address >= BASE_DMA) {
        m_dma_writes++;
        m_host->write_word(host_address(address - BASE_DMA), value);
        return;
    }
    switch (address & ~1u) {
    case BASE_HOST:
        // Машина: снятое разрешение отзывает запрос; пуск запоминает команду
        // и снимает «готово»
        if ((value & CSR_IE) == 0) m_pending = false;
        m_cr = (m_cr & ~CSR_HOST_WR) | (value & CSR_HOST_WR);
        if (value & CSR_GO) {
            m_go = m_cr;
            m_cr &= ~CSR_DONE;
            m_commands++;
        }
        update_irq();
        break;
    case BASE_HOST + 2:
        // Машина: запись данных снимает запрос данных
        m_dr = value;
        m_cr &= ~CSR_TR;
        break;
    case BASE_LOCAL:
        // Прошивка: «готово», ошибка, запрос данных
        set_done((value & CSR_DONE) != 0);
        m_cr = (m_cr & ~CSR_LOCAL_WR) | (value & CSR_LOCAL_WR);
        update_irq();
        break;
    case BASE_LOCAL + 2:
        m_dr = value;
        break;
    default:
        break;
    }
}

void DVKKMD::set_value(unsigned int address, unsigned int value, bool force)
{
    if (address >= BASE_DMA) {
        m_dma_writes++;
        m_host->write(host_address(address - BASE_DMA) | (address & 1), value & 0xFF);
        return;
    }
    // Байтовая запись в регистр - половина слова
    const unsigned a = address & ~1u;
    const unsigned w = (a == BASE_HOST || a == BASE_LOCAL) ? m_cr : m_dr;
    const unsigned v = (address & 1) ? ((w & 0x00FF) | ((value & 0xFF) << 8)) : ((w & 0xFF00) | (value & 0xFF));
    // Пуск только младшим байтом: старший байт MYCSR пуск не повторяет
    if ((address & 1) && a == BASE_HOST) {
        m_cr = (m_cr & ~(CSR_HOST_WR & 0xFF00)) | (v & CSR_HOST_WR & 0xFF00);
        return;
    }
    set_value_word(a, v, force);
}

std::vector<DeviceFieldInfo> DVKKMD::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"csr",      "MYCSR", false});
    r.push_back({"dr",       "MYDR", false});
    r.push_back({"command",  "Последняя запущенная команда (разряды 1-4)", false});
    r.push_back({"commands", "Сколько команд запустила машина", false});
    r.push_back({"dma",      "Обращений платы к памяти машины: чтений, записей", false});
    return r;
}

bool DVKKMD::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "csr")      { out.values.push_back(m_cr); return true; }
    if (field == "dr")       { out.values.push_back(m_dr); return true; }
    if (field == "command")  { out.values.push_back((m_go >> 1) & 017); return true; }
    if (field == "commands") { out.values.push_back(m_commands); return true; }
    if (field == "dma")      { out.values.push_back(m_dma_reads); out.values.push_back(m_dma_writes); return true; }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void DVKKMD::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("cr", m_cr);
    w.u("go", m_go);
    w.u("dr", m_dr);
    w.b("pending", m_pending);
    w.b("pulse", m_pulse);
    w.u("offered", m_irq.offered());
    w.u("commands", m_commands);
    w.u("dma_reads", m_dma_reads);
    w.u("dma_writes", m_dma_writes);
}

emulator::Result DVKKMD::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("cr", m_cr);
    r.u("go", m_go);
    r.u("dr", m_dr);
    r.b("pending", m_pending);
    r.b("pulse", m_pulse);
    unsigned offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    r.u("commands", m_commands);
    r.u("dma_reads", m_dma_reads);
    r.u("dma_writes", m_dma_writes);
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_kmd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKKMD(im, cd);
}
