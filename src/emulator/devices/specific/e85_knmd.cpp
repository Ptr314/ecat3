// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НМД Электроники МС 0585 (плата 3.858.232, ТО5)

#include <cstring>

#include "e85_knmd.h"
#include "emulator/utils.h"

// Регистры - смещение в окне; РСНУ выбран разрядом 4 адреса (ТО5, п. 4.6.1)
#define REG_ID          000
#define REG_ERR         004
#define REG_SECTOR      006
#define REG_DATA        010
#define REG_CYL         012
#define REG_HEAD        014
#define REG_CSR         016
#define REG_STATUS      020

// Страница 13 ТО5 потеряна, разряды 09-13 РОШПК по тексту п. 4.5 и по контроллеру
// ДВК, у которого те же регистры
#define ERR_FAULT       0002000     // неисправность
#define ERR_ID          0010000     // нет адреса
#define ERR_PRECOMP     0000040     // предкомпенсация со 128 цилиндра
#define ERR_RD          0177400
#define ERR_WR          0000377

#define SEC_RDWR        0177437
#define CYL_RDWR        0001777
#define HEAD_RDWR       0000007

#define CMD_RESTORE     0020
#define CMD_READ        0040
#define CMD_WRITE       0060
#define CMD_FORMAT      0120

#define CSR_ERR         0000400
#define CSR_DRQ2        0004000
#define CSR_SEEK_DONE   0010000
#define CSR_READY       0040000
#define CSR_RD          0177400
#define CSR_WR          0000377

#define ST_DONE         0000001
#define ST_INIT         0000010
#define ST_DRQ1         0000200
#define ST_SMALL        0000400
#define ST_BUSY         0100000
#define ST_RD           0100601

// Отложенные события
#define EV_RESTORE      1
#define EV_READ         2
#define EV_WRITE        3
#define EV_FORMAT       4
#define EV_READ_MISS    5               // сектор не нашёлся: конец поиска
#define EV_WRITE_MISS   6

#define CALLBACK_INIT   1

// ТО5 даёт только поиск сектора: два оборота (п. 4.5.2), дальше «Ошибка поиска адреса».
// seek_time, step_time и word_time - значения dvk-dw, начальная установка и оборот
// (format_time) взяты без источника, а диск не вращается: найденный сектор приходит сразу
// за поиском дорожки. Настоящий накопитель медленнее, см. docs/dev/machines.md
#define INIT_US         50000
#define MISS_REVS       2

E85Knmd::E85Knmd(InterfaceManager *im, EmulatorConfigDevice *cd):
      DWController(im, cd, 0200, INIT_US)
    , i_irqa(this, im, 1, "irqa", MODE_W)
    , i_irqb(this, im, 1, "irqb", MODE_W)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
{
    // Образ может быть короче накопителя: чистый диск - файл хотя бы в один сектор
    // и заданное число цилиндров
    m_grows_image = true;
    reset_ids();
}

void E85Knmd::reset_ids()
{
    for (unsigned int i = 0; i < ID_TRACKS; i++) m_id_head[i] = (uint8_t)(i & 7);
}

void E85Knmd::image_opened()
{
    reset_ids();
}

// От 1 до 8 головок: РАП - 3 разряда. Как их видит накопитель - physical_head()
bool E85Knmd::geometry_ok() const
{
    return m_heads >= 1 && m_heads <= 8 && m_sectors >= 1 && m_sectors <= 32;
}

// Накопители, которые узнаются по размеру образа: таблица Xhomer (накопители DEC Pro, по
// 16 и 17 секторов; программы 17-й сектор не трогают), два RD52 других изготовителей из
// таблицы DEVCHR драйвера DW (RT-11 V5.4A-V5.7) и 32 МБ на 8 x 511, куда тоже ставится
// ПРОС. Чистый винчестер - файл другого размера, он дорастает до геометрии из параметров
bool E85Knmd::known_geometry(uint64_t size, unsigned int &heads, unsigned int &cylinders, unsigned int &sectors) const
{
    static const struct { unsigned int heads, cylinders; } drives[] = {
        {4, 153},   // RD50, 5 МБ
        {4, 306},   // RD51 и МС5401, 10 МБ
        {4, 615},   // RD31, 21 МБ
        {5, 697},   // RD52, тип 52C у DW
        {8, 511},   // 32 МБ у ПРОС
        {8, 512},   // RD52, 36 МБ (52Q у DW)
        {7, 645},   // RD52, тип 52A у DW
        {6, 820},   // RD32, 43 МБ
        {8, 1024},  // RD53, 71 МБ
    };
    for (unsigned int s = 16; s <= 17; s++)
        for (unsigned int i = 0; i < sizeof(drives) / sizeof(drives[0]); i++)
            if (size == (uint64_t)drives[i].heads * drives[i].cylinders * s * HddImage::SECTOR_SIZE) {
                heads = drives[i].heads;
                cylinders = drives[i].cylinders;
                sectors = s;
                return true;
            }
    return false;
}

emulator::Result E85Knmd::load_config(SystemData *sd)
{
    emulator::Result res = DWController::load_config(sd);
    if (!res) return res;
    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));
    // Оборот диска: столько идёт разметка дорожки, а сектор, которого нет, ищется два оборота
    m_rev_us = read_confg_value(cd, "format_time", false, (unsigned int)16667);
    return load_image();
}

// Начальная установка - операция: «Занят», очистка регистров, переход на нулевую
// дорожку, а в конце ЗОА и «Операция завершена» (п. 4.5.5)
void E85Knmd::init_controller()
{
    m_err = ERR_PRECOMP;
    m_sector = m_cyl = m_head = 0;
    m_csr = 0;
    m_status = ST_BUSY;
    m_pointer = m_count = 0;
    m_op = OP_NONE;
    m_word_pending = false;
    schedule(EV_RESTORE, m_init_us);
    update_lines();
}

bool E85Knmd::drive_ready() const
{
    return m_image.attached();
}

//--------------------------- Запросы обмена --------------------------------//

void E85Knmd::update_lines()
{
    const unsigned int a = (m_status & ST_DONE) ? 1 : 0;
    if (i_irqa.value != a) i_irqa.change(a);
    const unsigned int b = (m_status & ST_DRQ1) ? 1 : 0;
    if (i_irqb.value != b) i_irqb.change(b);
}

void E85Knmd::raise_done()
{
    m_status |= ST_DONE;
    update_lines();
}

void E85Knmd::clear_done()
{
    m_status &= ~ST_DONE;
    update_lines();
}

void E85Knmd::raise_data()
{
    m_status |= ST_DRQ1;
    update_lines();
}

void E85Knmd::clear_data()
{
    m_status &= ~ST_DRQ1;
    update_lines();
}

void E85Knmd::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT && (new_value & 1)) init_controller();
}

//--------------------------- Ход операции ----------------------------------//

void E85Knmd::word_ready()
{
    raise_data();
}

void E85Knmd::event_done(unsigned int event)
{
    switch (event) {
    case EV_RESTORE:
        m_track = 0;
        m_csr = drive_ready() ? (CSR_READY | CSR_SEEK_DONE) : 0;
        finish();
        break;
    case EV_READ:
        read_done();
        break;
    case EV_WRITE:
        write_done();
        break;
    case EV_FORMAT:
        format_done();
        break;
    case EV_READ_MISS:
        m_err |= ERR_ID;
        m_csr |= CSR_ERR;
        give_buffer();
        break;
    case EV_WRITE_MISS:
        fail(ERR_ID);
        break;
    }
}

// Накопитель видит столько разрядов РАП, сколько нужно на его головки: у накопителя на
// 4 головки поверхности 4-7 совпадают с 0-3, а у RD32 на 6 головок поверхностей 6 и 7
// нет вовсе, и обращение к ним кончается, как за последним цилиндром
unsigned int E85Knmd::physical_head() const
{
    unsigned int mask = 0;
    while (mask + 1 < m_heads) mask = (mask << 1) | 1;
    return m_head & mask;
}

// Где лежит сектор. Дальше последнего цилиндра и последней поверхности накопитель не идёт
bool E85Knmd::sector_offset(unsigned int sector, uint64_t &offset) const
{
    if (!m_image.attached() || m_cyl >= m_cylinders || sector >= m_sectors || physical_head() >= m_heads)
        return false;
    offset = ((uint64_t)(m_cyl * m_heads + physical_head()) * m_sectors + sector) * HddImage::SECTOR_SIZE;
    return true;
}

// Найдётся ли сектор из регистров: дорожка есть, и её идентификаторы совпадают с регистрами
// целиком
bool E85Knmd::sector_found(uint64_t &offset) const
{
    return sector_offset(m_sector & 037, offset) && m_id_head[m_cyl * 8 + physical_head()] == m_head;
}

// Конец операции: снят «Занят», ЗОА и «Операция завершена»
void E85Knmd::finish()
{
    m_op = OP_NONE;
    m_word_pending = false;
    m_csr &= ~CSR_DRQ2;
    m_status &= ~ST_BUSY;
    raise_done();
}

void E85Knmd::fail(unsigned int err)
{
    m_err |= err;
    m_csr |= CSR_ERR;
    finish();
}

// Найден сектор или нет, чтение кончается одинаково: ЗОБ и «Запрос данных», буфер
// отдаётся программе, а ЗОА приходит, когда его прочтут. Сектор не нашёлся за два оборота
// (EV_READ_MISS) - в буфере остаётся прежнее, и стоят разряды ошибки. Обработчик DW из
// RT-11 на чтение разрешает только ЗОБ и при ошибке опустошает буфер сам (DUMP), а DW с
// V5.4A, определяя размер, ждёт «Запрос данных» опросом: ЗОА без ЗОБ повесили бы обоих
void E85Knmd::read_done()
{
    uint64_t offset;
    uint8_t data[HddImage::SECTOR_SIZE];
    m_track = (m_cyl < m_cylinders) ? m_cyl : m_cylinders - 1;
    if (!sector_found(offset) || !m_image.read(offset, data)) {
        schedule(EV_READ_MISS, MISS_REVS * m_rev_us);
        return;
    }
    for (unsigned int i = 0; i < 256; i++) m_buf[i] = (uint16_t)(data[2 * i] | (data[2 * i + 1] << 8));
    m_reads++;
    give_buffer();
}

// Буфер готов к выдаче программе
void E85Knmd::give_buffer()
{
    m_pointer = m_count = 0;
    m_op = OP_READ;
    m_status &= ~ST_BUSY;
    m_csr |= CSR_DRQ2;
    raise_data();
}

void E85Knmd::write_done()
{
    uint64_t offset;
    uint8_t data[HddImage::SECTOR_SIZE];
    m_track = (m_cyl < m_cylinders) ? m_cyl : m_cylinders - 1;
    if (!sector_found(offset)) {
        schedule(EV_WRITE_MISS, MISS_REVS * m_rev_us);
        return;
    }
    for (unsigned int i = 0; i < 256; i++) {
        data[2 * i] = (uint8_t)(m_buf[i] & 0xFF);
        data[2 * i + 1] = (uint8_t)(m_buf[i] >> 8);
    }
    if (!m_image.write(offset, data)) {
        fail(ERR_FAULT);
        return;
    }
    if (m_sector >> 8) note_unsupported("service zone");
    m_writes++;
    finish();
}

// Первые 16 слов буфера - порядок секторов на дорожке: в образе его нет. Чем
// заполняются поля данных, ТО5 не говорит: как и в Xhomer, последним байтом буфера
void E85Knmd::format_done()
{
    uint8_t data[HddImage::SECTOR_SIZE];
    m_track = (m_cyl < m_cylinders) ? m_cyl : m_cylinders - 1;
    memset(data, m_buf[255] >> 8, sizeof(data));
    for (unsigned int s = 0; s < m_sectors; s++) {
        uint64_t offset;
        if (!sector_offset(s, offset)) {
            fail(ERR_ID);
            return;
        }
        if (!m_image.write(offset, data)) {
            fail(ERR_FAULT);
            return;
        }
    }
    m_id_head[m_cyl * 8 + physical_head()] = (uint8_t)m_head;
    m_formats++;
    finish();
}

void E85Knmd::command(unsigned int cmd)
{
    m_command = cmd;
    m_err &= ~ERR_RD;
    m_csr &= ~(CSR_ERR | CSR_DRQ2);
    m_op = OP_NONE;
    m_word_pending = false;
    m_pointer = m_count = 0;
    clear_done();
    clear_data();

    if (!drive_ready() && cmd != 0) {
        fail(ERR_FAULT);
        return;
    }
    switch (cmd) {
    case CMD_RESTORE:
        m_status |= ST_BUSY;
        schedule(EV_RESTORE, seek_us(0));
        break;
    case CMD_READ:
        m_status |= ST_BUSY;
        schedule(EV_READ, seek_us(m_cyl));
        break;
    case CMD_WRITE:
    case CMD_FORMAT:
        m_op = (cmd == CMD_FORMAT) ? OP_FORMAT : OP_WRITE;
        m_csr |= CSR_DRQ2;
        raise_data();
        break;
    default:
        note_unsupported("command " + std::to_string(cmd));
        fail(ERR_FAULT);
        break;
    }
}

//--------------------------- Регистры --------------------------------------//

unsigned int E85Knmd::reg_of(unsigned int address) const
{
    return (address & REG_STATUS) ? REG_STATUS : (address & 016);
}

// Слова данных вытеснили бы из журнала всё остальное за два сектора
bool E85Knmd::trace_skips(unsigned int reg) const
{
    return reg == REG_DATA;
}

// Старшие байты РКС2 и РОШПК только читаются, но обращение к РКС2 есть обращение:
// оно снимает ЗОА
bool E85Knmd::odd_byte_write(unsigned int reg)
{
    if (reg == REG_CSR) clear_done();
    return reg == REG_CSR || reg == REG_ERR;
}

unsigned int E85Knmd::read_reg(unsigned int reg, bool peek)
{
    switch (reg) {
    case REG_ID:
        if (!peek) clear_data();
        return 0401;
    case REG_ERR:
        return m_err & ERR_RD;
    case REG_SECTOR:
        if (!peek) {
            m_pointer = 0;
            clear_done();
        }
        return m_sector & SEC_RDWR;
    case REG_DATA: {
        const unsigned int data = m_buf[m_pointer & 255];
        if (peek) return data;
        m_pointer = (m_pointer + 1) & 255;
        clear_data();
        if (m_op == OP_READ && ++m_count >= 256) {
            // Весь сектор отдан: конец операции
            finish();
        } else {
            schedule_word();
        }
        return data;
    }
    case REG_CYL:
        return m_cyl & CYL_RDWR;
    case REG_HEAD:
        return m_head & HEAD_RDWR;
    case REG_CSR:
        if (!peek) clear_done();
        return m_csr & CSR_RD;
    case REG_STATUS: {
        unsigned int v = m_status & ST_RD;
        if (small_drive()) v |= ST_SMALL;
        return v;
    }
    default:
        return 0;
    }
}

void E85Knmd::write_reg(unsigned int reg, unsigned int value)
{
    value &= 0xFFFF;
    switch (reg) {
    case REG_ID:
        clear_data();
        break;
    case REG_ERR:
        m_err = (m_err & ~ERR_WR) | (value & ERR_WR);
        break;
    case REG_SECTOR:
        m_sector = value & SEC_RDWR;
        m_pointer = 0;
        clear_done();
        break;
    case REG_DATA:
        m_buf[m_pointer & 255] = (uint16_t)value;
        m_pointer = (m_pointer + 1) & 255;
        clear_data();
        if ((m_op == OP_WRITE || m_op == OP_FORMAT) && ++m_count >= 256) {
            // Буфер полон: контроллер уходит на диск
            const bool format = (m_op == OP_FORMAT);
            m_op = OP_NONE;
            m_word_pending = false;
            m_status |= ST_BUSY;
            schedule(format ? EV_FORMAT : EV_WRITE, format ? seek_us(m_cyl) + m_rev_us : seek_us(m_cyl));
        } else {
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
        command(value & CSR_WR);
        break;
    case REG_STATUS:
        if (value & ST_INIT) init_controller();
        break;
    default:
        break;
    }
}

// Пока контроллер занят, ответить на шине может только РСНУ
bool E85Knmd::refused(unsigned int address)
{
    if (!(m_status & ST_BUSY) || (address & REG_STATUS)) return false;
    m_refused++;
    if (m_mapper != nullptr) m_mapper->no_device = true;
    return true;
}

//--------------------------- Поля и снимок ---------------------------------//

std::vector<DeviceFieldInfo> E85Knmd::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = DWController::get_device_fields();
    r.push_back({"status",    "Регистр РСНУ",                              false});
    r.push_back({"formats",   "Сколько дорожек размечено",                  false});
    r.push_back({"refused",   "Обращений к регистрам, пока контроллер занят", false});
    return r;
}

bool E85Knmd::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "status") {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(read_reg(REG_STATUS, true));
        return true;
    }
    if (field == "formats" || field == "refused") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back(field == "formats" ? m_formats : m_refused);
        return true;
    }
    return DWController::get_field(field, from, to, out);
}

void E85Knmd::save_state(StateWriter &w)
{
    DWController::save_state(w);
    w.u("status", m_status);
    w.hex("id_head", m_id_head, sizeof(m_id_head));
    w.n("pointer", m_pointer);
    w.n("count", m_count);
    w.n("op", m_op);
    w.n("formats", m_formats);
    w.n("refused", m_refused);
}

emulator::Result E85Knmd::load_state(const StateReader &r)
{
    const emulator::Result res = DWController::load_state(r);
    if (!res) return res;

    r.u("status", m_status);
    r.hex("id_head", m_id_head, sizeof(m_id_head));
    r.u("pointer", m_pointer);
    r.u("count", m_count);
    m_pointer &= 255;
    if (m_count > 256) m_count = 0;
    r.u("op", m_op);
    if (m_op > OP_FORMAT) m_op = OP_NONE;
    r.u("formats", m_formats);
    r.u("refused", m_refused);
    return emulator::Result::ok();
}

ComputerDevice * create_e85_knmd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Knmd(im, cd);
}
