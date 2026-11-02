// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК DW hard disk controller (КЗД 3.057.316)

#include <cstring>

#include "dvk_dw.h"
#include "emulator/utils.h"

// Регистры - номер слова в окне 174000-174037
#define REG_ID          0
#define REG_ERR         2
#define REG_SECTOR      3
#define REG_DATA        4
#define REG_CYL         5
#define REG_HEAD        6
#define REG_CSR         7
#define REG_SI          8

#define ERR_DM          0000400
#define ERR_TRK0        0001000
#define ERR_FAULT       0002000
#define ERR_AM          0010000
#define ERR_RD          0177400
#define ERR_WR          0000377

#define SEC_RDWR        0177437
#define CYL_RDWR        0001777
#define HEAD_RDWR       0000007

#define CMD_TRK0        0020
#define CMD_READ        0040
#define CMD_WRITE       0060
#define CMD_FORMAT      0120

#define CSR_ERR         0000400
#define CSR_DRQ2        0004000
#define CSR_DONE        0010000
#define CSR_DRDY        0040000
#define CSR_RD          0177400
#define CSR_WR          0000377

#define SI_DONE         0000001
#define SI_INIT         0000010
#define SI_IE           0000100
#define SI_DRQ1         0000200
#define SI_SMALL        0000400
#define SI_BUSY         0100000
#define SI_RD           0100711
#define SI_WR           0000510

// Отложенные события
#define EV_OLD_WORD     1       // только в снимках прежнего вида: слово готово
#define EV_SEEK         2       // головки на дорожке 0
#define EV_READ         3       // сектор прочитан в буфер
#define EV_WRITE        4       // буфер записан

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2
#define CALLBACK_INIT   3

// Сброс - возврат на дорожку 0. Проверка прерываний TESTDW разрешает их после паузы
// и ждёт прерывания от конца сброса (100 мс - уже мало), а её же проверка регистров
// ждёт снятия «занят» с ограничением (200 и 400 мс проходят)
#define INIT_US         200000

DVKDW::DVKDW(InterfaceManager *im, EmulatorConfigDevice *cd):
      DWController(im, cd, 040, INIT_US)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
{
}

// Регистр головки - 3 разряда, сектора - с единицы до 16
bool DVKDW::geometry_ok() const
{
    return m_heads >= 1 && m_heads <= 8 && m_sectors >= 1 && m_sectors <= 16;
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

void DVKDW::init_controller()
{
    m_err = 040;
    m_sector = m_cyl = m_head = 0;
    m_csr = CSR_DONE | CSR_DRDY;
    // Сброс - операция: «занят», а «закончено» поднимется в её конце
    m_si = SI_BUSY;
    if (small_drive()) m_si |= SI_SMALL;
    m_drqa = m_drqb = false;
    m_count_read = m_count_write = 0;
    m_format = false;
    m_op = OP_NONE;
    m_word_pending = false;
    schedule(EV_SEEK, m_init_us);
    update_irq();
}

//--------------------------- Прерывания ------------------------------------//

void DVKDW::update_irq()
{
    m_irq.offer(VirqLine::chain((m_drqa || m_drqb) ? m_vector : 0, i_virq_in, i_vector_in));
}

void DVKDW::raise_drqa()
{
    m_si |= SI_DONE;
    if (m_si & SI_IE) m_drqa = true;
    update_irq();
}

void DVKDW::clear_drqa()
{
    m_si &= ~SI_DONE;
    m_drqa = false;
    update_irq();
}

void DVKDW::raise_drqb()
{
    m_si |= SI_DRQ1;
    if (m_si & SI_IE) m_drqb = true;
    update_irq();
}

void DVKDW::clear_drqb()
{
    m_si &= ~SI_DRQ1;
    m_drqb = false;
    update_irq();
}

void DVKDW::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT) {
        if (new_value & 1) init_controller();
        return;
    }
    if (callback_id == CALLBACK_IAKO) {
        // Подтверждение снимает один запрос: первым - «операция закончена»
        if ((new_value & 0xFFFF) == m_vector && m_vector != 0) {
            if (m_drqa) m_drqa = false;
            else m_drqb = false;
        }
    }
    update_irq();
}

//--------------------------- Ход операции ----------------------------------//

void DVKDW::word_ready()
{
    raise_drqb();
}

void DVKDW::event_done(unsigned int event)
{
    switch (event) {
    case EV_SEEK:
        m_track = 0;
        m_si &= ~SI_BUSY;
        raise_drqa();
        break;
    case EV_READ:
        read_done();
        break;
    case EV_WRITE:
        write_done();
        break;
    }
}

// Сектор в регистре - с единицы, последний (16-й) пишется и как 0
bool DVKDW::sector_offset(uint64_t &offset) const
{
    //Only 0 wraps to the last sector; a number past the geometry is an error,
    //not another sector
    const unsigned int s = m_sector & 037;
    const unsigned int sector = (s == 0) ? m_sectors - 1 : s - 1;
    if (!m_image.attached() || m_cyl >= m_cylinders || m_head >= m_heads || sector >= m_sectors)
        return false;
    offset = ((uint64_t)(m_cyl * m_heads + m_head) * m_sectors + sector) * HddImage::SECTOR_SIZE;
    return true;
}

// Операция не удалась: адресная метка не найдена, конец операции с ошибкой
void DVKDW::fail(unsigned int err)
{
    m_si &= ~SI_BUSY;
    m_csr &= ~CSR_DRQ2;
    m_err |= err;
    m_csr |= CSR_ERR;
    m_op = OP_NONE;
    raise_drqa();
}

void DVKDW::read_done()
{
    uint64_t offset;
    uint8_t data[HddImage::SECTOR_SIZE];
    if (!sector_offset(offset) || !m_image.read(offset, data)) {
        fail(ERR_AM);
        return;
    }
    m_track = m_cyl;
    for (unsigned int i = 0; i < 256; i++) m_buf[i] = (uint16_t)(data[2 * i] | (data[2 * i + 1] << 8));
    m_reads++;
    m_count_read = 0;
    m_si &= ~SI_BUSY;
    m_csr |= CSR_DRQ2;
    raise_drqb();
}

void DVKDW::write_done()
{
    uint8_t data[HddImage::SECTOR_SIZE];
    m_count_write = 0;
    m_track = m_cyl;

    bool ok = true;
    if (m_format) {
        // Разметка: буфер несёт таблицу секторов дорожки, а поля данных всех
        // её секторов заполняются нулями
        memset(data, 0, sizeof(data));
        const unsigned int keep = m_sector;
        for (unsigned int s = 1; s <= m_sectors && ok; s++) {
            uint64_t offset;
            m_sector = s;
            ok = sector_offset(offset) && m_image.write(offset, data);
        }
        m_sector = keep;
    } else {
        for (unsigned int i = 0; i < 256; i++) {
            data[2 * i] = (uint8_t)(m_buf[i] & 0xFF);
            data[2 * i + 1] = (uint8_t)(m_buf[i] >> 8);
        }
        uint64_t offset;
        ok = sector_offset(offset) && m_image.write(offset, data);
    }
    m_format = false;
    if (!ok) {
        fail(ERR_AM);
        return;
    }
    m_writes++;
    m_op = OP_NONE;
    m_si &= ~SI_BUSY;
    m_csr &= ~CSR_DRQ2;
    raise_drqa();
}

void DVKDW::command(unsigned int cmd)
{
    m_command = cmd;
    m_err &= ~ERR_WR;
    m_csr &= ~CSR_ERR;
    m_op = OP_NONE;
    m_word_pending = false;

    switch (cmd) {
    case CMD_TRK0:
        m_si |= SI_BUSY;
        schedule(EV_SEEK, seek_us(0));
        break;
    case CMD_READ:
        m_op = OP_READ;
        m_si |= SI_BUSY;
        m_count_read = 0;
        schedule(EV_READ, seek_us(m_cyl));
        break;
    case CMD_WRITE:
    case CMD_FORMAT:
        m_format = (cmd == CMD_FORMAT);
        m_op = OP_WRITE;
        m_csr |= CSR_DRQ2;
        m_count_write = 0;
        raise_drqb();
        break;
    default:
        // Неизвестная команда: отказ, без конца операции - как в MAME
        note_unsupported("command " + std::to_string(cmd));
        m_err |= ERR_FAULT;
        m_csr |= CSR_ERR;
        break;
    }
}

//--------------------------- Регистры --------------------------------------//

unsigned int DVKDW::reg_of(unsigned int address) const
{
    return (address >> 1) & 017;
}

// Старший байт CSR команды не даёт
bool DVKDW::odd_byte_write(unsigned int reg)
{
    return reg == REG_CSR;
}

unsigned int DVKDW::read_reg(unsigned int reg, bool peek)
{
    switch (reg) {
    case REG_ID:
        if (!peek) clear_drqb();
        return 0401;
    case REG_ERR:
        return m_err & ERR_RD;
    case REG_SECTOR:
        if (!peek) clear_drqa();
        return m_sector & SEC_RDWR;
    case REG_DATA: {
        const unsigned int data = m_buf[m_count_read & 255];
        if (peek) return data;
        m_count_read = (m_count_read + 1) & 511;
        clear_drqb();
        if (m_op == OP_READ && m_count_read >= 256) {
            // Весь сектор отдан: конец операции
            m_count_read = 0;
            m_op = OP_NONE;
            m_csr &= ~CSR_DRQ2;
            m_word_pending = false;
            raise_drqa();
        } else {
            // Без команды буфер читается по кругу
            if (m_count_read >= 256) m_count_read = 0;
            schedule_word();
        }
        return data;
    }
    case REG_CYL:
        return m_cyl & CYL_RDWR;
    case REG_HEAD:
        return m_head & HEAD_RDWR;
    case REG_CSR:
        return m_csr & CSR_RD;
    case REG_SI:
        return m_si & SI_RD;
    default:
        return 0;
    }
}

void DVKDW::write_reg(unsigned int reg, unsigned int value)
{
    value &= 0xFFFF;
    switch (reg) {
    case REG_ID:
        clear_drqb();
        break;
    case REG_ERR:
        m_err = (m_err & ~ERR_WR) | (value & ERR_WR);
        break;
    case REG_SECTOR:
        m_sector = (m_sector & ~SEC_RDWR) | (value & SEC_RDWR);
        clear_drqa();
        break;
    case REG_DATA:
        m_buf[m_count_write & 255] = (uint16_t)value;
        m_count_write++;
        clear_drqb();
        if (m_op == OP_WRITE && m_count_write >= 256) {
            m_si |= SI_BUSY;
            schedule(EV_WRITE, seek_us(m_cyl));
        } else {
            // Без команды буфер пишется по кругу
            if (m_count_write >= 256) m_count_write = 0;
            schedule_word();
        }
        break;
    case REG_CYL:
        m_cyl = value & CYL_RDWR;
        break;
    case REG_HEAD:
        m_head = value & HEAD_RDWR;
        break;
    case REG_CSR:
        m_csr = (m_csr & ~CSR_WR) | (value & CSR_WR);
        clear_drqa();
        command(value & CSR_WR);
        break;
    case REG_SI:
        // Снятое разрешение отзывает запросы. Поставленное при уже поднятом
        // разряде запроса не даёт (в MAME даёт): проверка прерываний TESTDW
        // разрешает их после прошлой операции, не сбросив «закончено», и ждёт
        // прерывания только от следующей
        if ((value & SI_IE) == 0)
            m_drqa = m_drqb = false;
        m_si = (m_si & ~SI_WR) | (value & SI_WR);
        if (value & SI_INIT) init_controller();
        update_irq();
        break;
    default:
        break;
    }
}

//--------------------------- Поля и снимок ---------------------------------//

std::vector<DeviceFieldInfo> DVKDW::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = DWController::get_device_fields();
    r.push_back({"si", "Регистр 174020", false});
    return r;
}

bool DVKDW::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "si") {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_si);
        return true;
    }
    return DWController::get_field(field, from, to, out);
}

void DVKDW::save_state(StateWriter &w)
{
    DWController::save_state(w);
    w.u("si", m_si);
    w.n("count_read", m_count_read);
    w.n("count_write", m_count_write);
    w.b("format", m_format);
    w.n("op", m_op);
    w.b("drqa", m_drqa);
    w.b("drqb", m_drqb);
    w.u("offered", m_irq.offered());
}

emulator::Result DVKDW::load_state(const StateReader &r)
{
    const emulator::Result res = DWController::load_state(r);
    if (!res) return res;

    r.u("si", m_si);
    r.u("count_read", m_count_read);
    r.u("count_write", m_count_write);
    if (m_count_read > 256) m_count_read = 0;
    if (m_count_write > 256) m_count_write = 0;
    r.b("format", m_format);
    r.u("op", m_op);
    if (m_op > OP_WRITE) m_op = OP_NONE;
    r.b("drqa", m_drqa);
    r.b("drqb", m_drqb);
    if (pending_event() == EV_OLD_WORD) adopt_word_event();
    unsigned int offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_dw(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKDW(im, cd);
}
