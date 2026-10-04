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
#define EV_NONE         0
#define EV_WORD         1       // следующее слово готово
#define EV_SEEK         2       // головки на дорожке 0
#define EV_READ         3       // сектор прочитан в буфер
#define EV_WRITE        4       // буфер записан

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2
#define CALLBACK_INIT   3

// Накопитель 5 МБ: столько цилиндров DW.SYS ждёт при поднятом разряде 400
// регистра SI, а без него - вдвое больше
#define SMALL_CYLINDERS 153

DVKDW::DVKDW(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
{
    device_class = "hdd";
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 040;
}

emulator::Result DVKDW::attach(const std::string &file_name)
{
    std::string error;
    if (!m_image.open(file_name, error))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Hard disk image file not found")) + "} " + file_name);
    // Цилиндры - по размеру файла; 4 головки по 16 секторов, как у DW.SYS
    m_cylinders = (unsigned int)(m_image.size() / HddImage::SECTOR_SIZE / (m_heads * m_sectors));
    if (m_cylinders == 0 || m_cylinders > 1024) {
        m_image.close();
        m_cylinders = 0;
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unrecognized hard disk geometry")) + "} " + file_name);
    }
    init_controller();
    return emulator::Result::ok();
}

emulator::Result DVKDW::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    files = read_confg_value(cd, "files", false, std::string(""));
    m_vector = read_confg_value(cd, "vector", false, (unsigned int)0300);
    m_image.set_write_protect(read_confg_value(cd, "write_protect", false, false));
    m_image.set_volatile(read_confg_value(cd, "volatile", false, false));

    // Времена в микросекундах: поиск и поворот до сектора,
    // переход головок на цилиндр, выдача слова из буфера контроллера
    m_seek_us = read_confg_value(cd, "seek_time", false, (unsigned int)2000);
    m_step_us = read_confg_value(cd, "step_time", false, (unsigned int)200);
    m_word_us = read_confg_value(cd, "word_time", false, (unsigned int)2);
    // Сброс - возврат на дорожку 0. Проверка прерываний TESTDW разрешает их
    // после паузы и ждёт прерывания от конца сброса (100 мс - уже мало), а её
    // же проверка регистров ждёт снятия «занят» с ограничением (200 и 400 мс
    // проходят)
    m_init_us = read_confg_value(cd, "init_time", false, (unsigned int)200000);

    // Образ на мегабайты возят отдельно от машины: его отсутствие не ошибка,
    // гнездо пустое, образ вставляют командой load или из меню
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

void DVKDW::reset(MAYBE_UNUSED bool cold)
{
    m_irq.clear();
    init_controller();
}

void DVKDW::init_controller()
{
    m_err = 040;
    m_sector = m_cyl = m_head = 0;
    m_csr = CSR_DONE | CSR_DRDY;
    // Сброс - операция: «занят», а «закончено» поднимется в её конце
    m_si = SI_BUSY;
    if (m_cylinders != 0 && m_cylinders <= SMALL_CYLINDERS) m_si |= SI_SMALL;
    m_drqa = m_drqb = false;
    m_count_read = m_count_write = 0;
    m_format = false;
    m_op = OP_NONE;
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

void DVKDW::schedule(unsigned int event, unsigned int us)
{
    m_event = event;
    m_timeout = us;
    m_acc = 0;
}

unsigned int DVKDW::seek_us(unsigned int cylinder) const
{
    const unsigned int d = (cylinder > m_track) ? cylinder - m_track : m_track - cylinder;
    return m_seek_us + d * m_step_us;
}

void DVKDW::clock(unsigned int counter)
{
    if (m_event == EV_NONE || m_system_clock == 0) return;

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
    case EV_WORD:
        raise_drqb();
        break;
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

// Сектор в регистре - с единицы, 16-й пишется как 0
bool DVKDW::sector_offset(uint64_t &offset) const
{
    const unsigned int sector = (m_sector - 1) & 15;
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
            m_sector = s & 15;
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
            m_event = EV_NONE;
            raise_drqa();
        } else {
            // Без команды буфер читается по кругу
            if (m_count_read >= 256) m_count_read = 0;
            schedule(EV_WORD, m_word_us);
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
            schedule(EV_WORD, m_word_us);
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

void DVKDW::trace(unsigned int kind, unsigned int reg, unsigned int value)
{
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

unsigned int DVKDW::get_value_word(unsigned int address)
{
    const unsigned int reg = (address >> 1) & 017;
    const unsigned int v = read_reg(reg, false) & 0xFFFF;
    trace(0, reg, v);
    return v;
}

void DVKDW::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    trace(1, (address >> 1) & 017, value);
    write_reg((address >> 1) & 017, value);
}

unsigned int DVKDW::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

// Байт пишется в свою половину регистра; старший байт CSR команды не даёт
void DVKDW::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    const unsigned int reg = (address >> 1) & 017;
    if ((address & 1) && reg == REG_CSR) return;
    const unsigned int old = read_reg(reg, true);
    const unsigned int w = (address & 1) ? ((old & 0x00FF) | ((value & 0xFF) << 8))
                                         : ((old & 0xFF00) | (value & 0xFF));
    trace(2, reg, w);
    write_reg(reg, w);
}

unsigned DVKDW::get_direct(unsigned address)
{
    const unsigned int w = read_reg((address >> 1) & 017, true);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

//--------------------------- Поля и команды --------------------------------//

ConfigFields DVKDW::get_config_fields()
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

std::vector<DeviceFieldInfo> DVKDW::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"attached",  "1, когда образ вставлен",                   false});
    r.push_back({"file",      "Имя образа",                                false});
    r.push_back({"protected", "1, когда запись в образ запрещена",          false});
    r.push_back({"volatile",  "1, когда запись идёт в память, а не в файл", false});
    r.push_back({"geometry",  "Цилиндры, головки и секторы образа",         false});
    r.push_back({"csr",       "Регистр 174016",                            false});
    r.push_back({"si",        "Регистр 174020",                            false});
    r.push_back({"err",       "Регистр ошибок 174004",                     false});
    r.push_back({"command",   "Последняя команда",                         false});
    r.push_back({"cylinder",  "Цилиндр в регистре",                        false});
    r.push_back({"head",      "Головка в регистре",                        false});
    r.push_back({"sector",    "Сектор в регистре",                         false});
    r.push_back({"reads",     "Сколько секторов прочитано",                 false});
    r.push_back({"writes",    "Сколько секторов записано",                  false});
    r.push_back({"trace",     "Последние обращения к регистрам: вид (0 чтение, 1 запись слова, 2 байта), номер слова, значение", true});
    return r;
}

std::vector<DeviceCommandInfo> DVKDW::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"load",     "\"file\"", "Вставляет образ винчестера"});
    r.push_back({"eject",    "",         "Вынимает образ"});
    r.push_back({"protect",  "[0|1]",    "Запрещает или разрешает запись в образ"});
    r.push_back({"volatile", "[0|1]",    "Запись в память вместо файла; выключение забывает записанное"});
    return r;
}

bool DVKDW::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "file") { out.text = m_image.file_name(); return true; }
    // Отладка: последние обращения, старые первыми, по три числа на каждое
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
    if (field == "si")        { out.values.push_back(m_si);       return true; }
    if (field == "err")       { out.values.push_back(m_err);      return true; }
    if (field == "command")   { out.values.push_back(m_command);  return true; }
    if (field == "cylinder")  { out.values.push_back(m_cyl);      return true; }
    if (field == "head")      { out.values.push_back(m_head);     return true; }
    if (field == "sector")    { out.values.push_back(m_sector);   return true; }
    out.width = 32;
    if (field == "reads")     { out.values.push_back(m_reads);    return true; }
    if (field == "writes")    { out.values.push_back(m_writes);   return true; }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

emulator::Result DVKDW::send_command(const std::string &command, const std::string &parameters)
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

void DVKDW::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("err", m_err);
    w.u("sector", m_sector);
    w.u("cyl", m_cyl);
    w.u("head", m_head);
    w.u("csr", m_csr);
    w.u("si", m_si);
    w.n("track", m_track);
    w.u("command", m_command);
    w.array("buf", m_buf, 256);
    w.n("count_read", m_count_read);
    w.n("count_write", m_count_write);
    w.b("format", m_format);
    w.n("op", m_op);
    w.b("drqa", m_drqa);
    w.b("drqb", m_drqb);
    w.n("event", m_event);
    w.n64("timeout", m_timeout);
    w.n64("acc", m_acc);
    w.n("reads", m_reads);
    w.n("writes", m_writes);
    w.u("offered", m_irq.offered());
    w.b("write_protect", m_image.write_protect());
    if (m_image.attached()) {
        w.b("attached", true);
        m_image.save_state(w, "image", name);
    }
}

emulator::Result DVKDW::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;

    // Образ - первым: attach() сбрасывает контроллер
    bool attached = false;
    if (r.b("attached", attached) && attached) {
        std::string file;
        if (r.s("image", file) && !file.empty()) {
            const std::string path = find_file_location(sd, file);
            if (path.empty())
                return emulator::Result::error(emulator::ErrorCode::FileError,
                    "{MachineState|Saved state} " + name + ": file not found: " + file);
            res = attach(path);
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
    r.u("si", m_si);
    r.u("track", m_track);
    r.u("command", m_command);
    r.array("buf", m_buf, 256);
    r.u("count_read", m_count_read);
    r.u("count_write", m_count_write);
    if (m_count_read > 256) m_count_read = 0;
    if (m_count_write > 256) m_count_write = 0;
    r.b("format", m_format);
    r.u("op", m_op);
    if (m_op > OP_WRITE) m_op = OP_NONE;
    r.b("drqa", m_drqa);
    r.b("drqb", m_drqb);
    r.u("event", m_event);
    r.n64("timeout", m_timeout);
    r.n64("acc", m_acc);
    r.u("reads", m_reads);
    r.u("writes", m_writes);
    unsigned int offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    bool wp = false;
    if (r.b("write_protect", wp)) m_image.set_write_protect(wp);
    return emulator::Result::ok();
}

ComputerDevice * create_dvk_dw(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DVKDW(im, cd);
}
