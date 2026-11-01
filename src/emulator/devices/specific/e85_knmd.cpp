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
#define ERR_DM          0000400     // нет маркера данных
#define ERR_TRK0        0001000     // не найдена нулевая дорожка
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
#define EV_NONE         0
#define EV_RESTORE      1
#define EV_READ         2
#define EV_WRITE        3
#define EV_FORMAT       4

#define CALLBACK_INIT   1

// Накопитель 5 МБ: разряд 8 РСНУ, позиционирование в режиме «Медленный поиск»
#define SMALL_CYLINDERS 153

E85Knmd::E85Knmd(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irqa(this, im, 1, "irqa", MODE_W)
    , i_irqb(this, im, 1, "irqb", MODE_W)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
{
    device_class = "hdd";
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 0200;
    reset_ids();
}

void E85Knmd::reset_ids()
{
    for (unsigned int i = 0; i < ID_TRACKS; i++) m_id_head[i] = (uint8_t)(i & 7);
}

// Образ, без сброса контроллера: из снимка регистры и линии приходят сами
emulator::Result E85Knmd::open_image(const std::string &file_name)
{
    std::string error;
    m_cylinders = 0;
    if (!m_image.open(file_name, error))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Hard disk image file not found")) + "} " + file_name);
    // Цилиндры - из конфигурации или по размеру файла. Образ может быть короче
    // накопителя: чистый диск - файл хотя бы в один сектор и заданное число цилиндров
    const uint64_t track = (uint64_t)m_heads * m_sectors * HddImage::SECTOR_SIZE;
    m_cylinders = (m_cfg_cylinders != 0) ? m_cfg_cylinders : (unsigned int)(m_image.size() / track);
    if (m_cylinders == 0 || m_cylinders > 1024) {
        m_image.close();
        m_cylinders = 0;
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unrecognized hard disk geometry")) + "} " + file_name);
    }
    m_image.extend(track * m_cylinders, 0);
    reset_ids();
    return emulator::Result::ok();
}

emulator::Result E85Knmd::attach(const std::string &file_name)
{
    const emulator::Result res = open_image(file_name);
    init_controller();
    return res;
}

emulator::Result E85Knmd::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));

    files = read_confg_value(cd, "files", false, std::string(""));
    m_image.set_write_protect(read_confg_value(cd, "write_protect", false, false));
    m_image.set_volatile(read_confg_value(cd, "volatile", false, false));

    m_heads = read_confg_value(cd, "heads", false, (unsigned int)4);
    m_sectors = read_confg_value(cd, "sectors", false, (unsigned int)16);
    m_cfg_cylinders = read_confg_value(cd, "cylinders", false, (unsigned int)0);
    // Головки - степень двойки: лишние разряды РАП накопитель не видит
    if ((m_heads != 2 && m_heads != 4 && m_heads != 8) || m_sectors < 1 || m_sectors > 32 || m_cfg_cylinders > 1024)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unsupported hard disk geometry")) + "} " + name);
    const std::string small_jumper = cd->get_parameter("small", false).value;
    m_small = small_jumper.empty() ? -1 : (parse_numeric_value(small_jumper) != 0 ? 1 : 0);

    // Времена в микросекундах: поиск и поворот до сектора, переход на цилиндр,
    // передача слова буфера, начальная установка, разметка дорожки (оборот)
    m_seek_us = read_confg_value(cd, "seek_time", false, (unsigned int)2000);
    m_step_us = read_confg_value(cd, "step_time", false, (unsigned int)200);
    m_word_us = read_confg_value(cd, "word_time", false, (unsigned int)2);
    m_init_us = read_confg_value(cd, "init_time", false, (unsigned int)50000);
    m_format_us = read_confg_value(cd, "format_time", false, (unsigned int)16667);

    const std::string image = read_confg_value(cd, "image", false, std::string(""));
    if (!image.empty()) {
        const std::string file = find_file_location(sd, image);
        if (!file.empty()) {
            res = attach(file);
            if (!res) return res;
        }
    }
    init_controller();
    return emulator::Result::ok();
}

void E85Knmd::reset(MAYBE_UNUSED bool cold)
{
    init_controller();
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

void E85Knmd::schedule(unsigned int event, unsigned int us)
{
    m_event = event;
    m_timeout = us;
    m_acc = 0;
}

void E85Knmd::schedule_word()
{
    m_word_pending = true;
    m_word_timeout = m_word_us;
    m_word_acc = 0;
}

unsigned int E85Knmd::seek_us(unsigned int cylinder) const
{
    const unsigned int d = (cylinder > m_track) ? cylinder - m_track : m_track - cylinder;
    return m_seek_us + d * m_step_us;
}

void E85Knmd::clock(unsigned int counter)
{
    if (m_system_clock == 0) return;
    if (m_word_pending) {
        m_word_acc += (uint64_t)counter * 1000000ull;
        while (m_word_acc >= m_system_clock && m_word_timeout > 0) {
            m_word_acc -= m_system_clock;
            m_word_timeout--;
        }
        if (m_word_timeout == 0) {
            m_word_pending = false;
            raise_data();
        }
    }
    if (m_event == EV_NONE) return;

    m_acc += (uint64_t)counter * 1000000ull;
    const uint64_t step = m_system_clock;
    while (m_acc >= step && m_timeout > 0) {
        m_acc -= step;
        m_timeout--;
    }
    if (m_timeout > 0) return;

    const unsigned int event = m_event;
    m_event = EV_NONE;
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
    }
}

// У накопителя на 4 головки поверхности 4-7 совпадают с 0-3
unsigned int E85Knmd::physical_head() const
{
    return m_head & (m_heads - 1);
}

// Где лежит сектор. Дальше последнего цилиндра накопитель не идёт
bool E85Knmd::sector_offset(unsigned int sector, uint64_t &offset) const
{
    if (!m_image.attached() || m_cyl >= m_cylinders || sector >= m_sectors)
        return false;
    offset = ((uint64_t)(m_cyl * m_heads + physical_head()) * m_sectors + sector) * HddImage::SECTOR_SIZE;
    return true;
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

void E85Knmd::read_done()
{
    uint64_t offset;
    uint8_t data[HddImage::SECTOR_SIZE];
    m_track = (m_cyl < m_cylinders) ? m_cyl : m_cylinders - 1;
    if (!sector_offset(m_sector & 037, offset) || m_id_head[m_cyl * 8 + physical_head()] != m_head
            || !m_image.read(offset, data)) {
        fail(ERR_ID);
        return;
    }
    for (unsigned int i = 0; i < 256; i++) m_buf[i] = (uint16_t)(data[2 * i] | (data[2 * i + 1] << 8));
    m_reads++;
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
    if (!sector_offset(m_sector & 037, offset) || m_id_head[m_cyl * 8 + physical_head()] != m_head) {
        fail(ERR_ID);
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
        if (m_small > 0 || (m_small < 0 && m_cylinders != 0 && m_cylinders <= SMALL_CYLINDERS)) v |= ST_SMALL;
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
            schedule(format ? EV_FORMAT : EV_WRITE, format ? seek_us(m_cyl) + m_format_us : seek_us(m_cyl));
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

void E85Knmd::trace(unsigned int kind, unsigned int reg, unsigned int value)
{
    // Слова данных вытеснили бы из кольца всё остальное за два сектора
    if (reg == REG_DATA) return;
    // Подряд одинаковые чтения (опрос готовности) - одной записью
    if (kind == 0 && m_trace_next > 0) {
        const TraceEntry &prev = m_trace[(m_trace_next - 1) & 1023];
        if (prev.kind == 0 && prev.reg == reg && prev.value == (value & 0xFFFF)) return;
    }
    TraceEntry &e = m_trace[m_trace_next & 1023];
    e.kind = (uint16_t)kind;
    e.reg = (uint16_t)reg;
    e.value = (uint16_t)value;
    m_trace_next++;
}

static inline unsigned int reg_of(unsigned int address)
{
    return (address & REG_STATUS) ? REG_STATUS : (address & 016);
}

unsigned int E85Knmd::get_value_word(unsigned int address)
{
    if (refused(address)) return 0xFFFF;
    const unsigned int reg = reg_of(address);
    const unsigned int v = read_reg(reg, false) & 0xFFFF;
    trace(0, reg, v);
    return v;
}

void E85Knmd::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (refused(address)) return;
    trace(1, reg_of(address), value);
    write_reg(reg_of(address), value);
}

unsigned int E85Knmd::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

// Байт пишется в свою половину регистра. Старшие байты РКС2 и РОШПК только
// читаются, но обращение к РКС2 есть обращение: оно снимает ЗОА
void E85Knmd::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (refused(address)) return;
    const unsigned int reg = reg_of(address);
    if (address & 1) {
        if (reg == REG_CSR) clear_done();
        if (reg == REG_CSR || reg == REG_ERR) return;
    }
    const unsigned int old = read_reg(reg, true);
    const unsigned int w = (address & 1) ? ((old & 0x00FF) | ((value & 0xFF) << 8))
                                         : ((old & 0xFF00) | (value & 0xFF));
    trace(2, reg, w);
    write_reg(reg, w);
}

unsigned E85Knmd::get_direct(unsigned address)
{
    const unsigned int w = read_reg(reg_of(address), true);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

//--------------------------- Поля и команды --------------------------------//

ConfigFields E85Knmd::get_config_fields()
{
    ConfigField f;
    f.name = "image";
    f.title = QT_TRANSLATE_NOOP("ConfigFields", "Hard disk image");
    f.type = CONFIG_FIELD_FILE;
    f.files = cd->get_parameter("files", false).value;
    f.embeddable = false;

    ConfigField v;
    v.name = "volatile";
    v.title = QT_TRANSLATE_NOOP("ConfigFields", "Hard disk writes");
    v.type = CONFIG_FIELD_CHOICE;
    v.def = "0";
    v.values.push_back({"0", QT_TRANSLATE_NOOP("ConfigFields", "Into the image file")});
    v.values.push_back({"1", QT_TRANSLATE_NOOP("ConfigFields", "Into memory only, the image stays as it was")});
    return {f, v};
}

std::vector<DeviceFieldInfo> E85Knmd::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"attached",  "1, когда образ вставлен",                   false});
    r.push_back({"file",      "Имя образа",                                false});
    r.push_back({"protected", "1, когда запись в образ запрещена",          false});
    r.push_back({"volatile",  "1, когда запись идёт в память, а не в файл", false});
    r.push_back({"geometry",  "Цилиндры, головки и секторы образа",         false});
    r.push_back({"csr",       "Регистр РКС2",                              false});
    r.push_back({"status",    "Регистр РСНУ",                              false});
    r.push_back({"err",       "Регистр РОШПК",                             false});
    r.push_back({"command",   "Последняя команда",                         false});
    r.push_back({"cylinder",  "Цилиндр в регистре",                        false});
    r.push_back({"head",      "Поверхность в регистре",                    false});
    r.push_back({"sector",    "Сектор в регистре",                         false});
    r.push_back({"reads",     "Сколько секторов прочитано",                 false});
    r.push_back({"writes",    "Сколько секторов записано",                  false});
    r.push_back({"formats",   "Сколько дорожек размечено",                  false});
    r.push_back({"refused",   "Обращений к регистрам, пока контроллер занят", false});
    r.push_back({"trace",     "Последние обращения к регистрам: вид (0 чтение, 1 запись слова, 2 байта), смещение, значение", true});
    return r;
}

std::vector<DeviceCommandInfo> E85Knmd::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"load",     "\"file\"", "Вставляет образ винчестера"});
    r.push_back({"eject",    "",         "Вынимает образ"});
    r.push_back({"protect",  "[0|1]",    "Запрещает или разрешает запись в образ"});
    r.push_back({"volatile", "[0|1]",    "Запись в память вместо файла; выключение забывает записанное"});
    return r;
}

bool E85Knmd::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "file") { out.text = m_image.file_name(); return true; }
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
        }
        return true;
    }
    if (field == "geometry") {
        out.text = std::to_string(m_cylinders) + "/" + std::to_string(m_heads) + "/" + std::to_string(m_sectors);
        return true;
    }
    out.numeric = true;
    out.width = 16;
    if (field == "attached")  { out.values.push_back(m_image.attached() ? 1 : 0);     return true; }
    if (field == "protected") { out.values.push_back(m_image.is_protected() ? 1 : 0); return true; }
    if (field == "volatile")  { out.values.push_back(m_image.is_volatile() ? 1 : 0);  return true; }
    if (field == "csr")       { out.values.push_back(m_csr);      return true; }
    if (field == "status")    { out.values.push_back(read_reg(REG_STATUS, true)); return true; }
    if (field == "err")       { out.values.push_back(m_err);      return true; }
    if (field == "command")   { out.values.push_back(m_command);  return true; }
    if (field == "cylinder")  { out.values.push_back(m_cyl);      return true; }
    if (field == "head")      { out.values.push_back(m_head);     return true; }
    if (field == "sector")    { out.values.push_back(m_sector);   return true; }
    out.width = 32;
    if (field == "reads")     { out.values.push_back(m_reads);    return true; }
    if (field == "writes")    { out.values.push_back(m_writes);   return true; }
    if (field == "formats")   { out.values.push_back(m_formats);  return true; }
    if (field == "refused")   { out.values.push_back(m_refused);  return true; }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

emulator::Result E85Knmd::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "load") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Command 'load' expects a file name")) + "}");
        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];
        return attach(file);
    }
    if (command == "eject") {
        m_image.close();
        m_cylinders = 0;
        init_controller();
        return emulator::Result::ok();
    }
    if (command == "protect") {
        m_image.set_write_protect((p.empty() || p[0].empty()) ? !m_image.write_protect()
                                                               : parse_numeric_value(p[0], 10) != 0);
        return emulator::Result::ok();
    }
    if (command == "volatile") {
        m_image.set_volatile((p.empty() || p[0].empty()) ? !m_image.is_volatile()
                                                          : parse_numeric_value(p[0], 10) != 0);
        return emulator::Result::ok();
    }
    return AddressableDevice::send_command(command, parameters);
}

void E85Knmd::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("err", m_err);
    w.u("sector", m_sector);
    w.u("cyl", m_cyl);
    w.u("head", m_head);
    w.u("csr", m_csr);
    w.u("status", m_status);
    w.n("track", m_track);
    w.u("command", m_command);
    w.hex("id_head", m_id_head, sizeof(m_id_head));
    w.array("buf", m_buf, 256);
    w.n("pointer", m_pointer);
    w.n("count", m_count);
    w.n("op", m_op);
    w.n("event", m_event);
    w.n64("timeout", m_timeout);
    w.n64("acc", m_acc);
    w.b("word_pending", m_word_pending);
    w.n64("word_timeout", m_word_timeout);
    w.n64("word_acc", m_word_acc);
    w.n("reads", m_reads);
    w.n("writes", m_writes);
    w.n("formats", m_formats);
    w.n("refused", m_refused);
    w.b("write_protect", m_image.write_protect());
    if (m_image.attached()) {
        w.b("attached", true);
        m_image.save_state(w, "image", name);
    }
}

emulator::Result E85Knmd::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;

    // Образ - первым: регистры ниже - уже его. Без сброса: линии восстановлены
    bool attached = false;
    if (r.b("attached", attached) && attached) {
        std::string file;
        if (r.s("image", file) && !file.empty()) {
            const std::string path = find_file_location(sd, file);
            if (path.empty())
                return emulator::Result::error(emulator::ErrorCode::FileError,
                    "{MachineState|Saved state} " + name + ": file not found: " + file);
            res = open_image(path);
            if (!res) return res;
            // Копия из снимка и его ОЗУ сняты вместе: запись в файл их
            // рассогласовала бы, поэтому восстановленный диск пишет в память
            m_image.set_volatile(true);
        }
    }

    r.u("err", m_err);
    r.u("sector", m_sector);
    r.u("cyl", m_cyl);
    r.u("head", m_head);
    r.u("csr", m_csr);
    r.u("status", m_status);
    r.u("track", m_track);
    r.u("command", m_command);
    r.hex("id_head", m_id_head, sizeof(m_id_head));
    r.array("buf", m_buf, 256);
    r.u("pointer", m_pointer);
    r.u("count", m_count);
    m_pointer &= 255;
    if (m_count > 256) m_count = 0;
    r.u("op", m_op);
    if (m_op > OP_FORMAT) m_op = OP_NONE;
    r.u("event", m_event);
    r.n64("timeout", m_timeout);
    r.n64("acc", m_acc);
    r.b("word_pending", m_word_pending);
    r.n64("word_timeout", m_word_timeout);
    r.n64("word_acc", m_word_acc);
    r.u("reads", m_reads);
    r.u("writes", m_writes);
    r.u("formats", m_formats);
    r.u("refused", m_refused);
    bool wp = false;
    if (r.b("write_protect", wp)) m_image.set_write_protect(wp);
    return emulator::Result::ok();
}

ComputerDevice * create_e85_knmd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Knmd(im, cd);
}
