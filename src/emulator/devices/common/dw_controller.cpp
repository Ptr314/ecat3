// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The DW hard disk controller: the board of the Электроника МС 0585 and its ДВК copy

#include <cstring>

#include "dw_controller.h"
#include "emulator/utils.h"

// Накопитель 5 МБ: столько цилиндров DW.SYS ждёт при поднятом разряде 400 регистра
// состояния, а без него - вдвое больше
#define SMALL_CYLINDERS 153

// Ошибки РОШПК, ТО5 табл. 3 (её страница 13 - в скане docs-external/E85/Discs/new):
//   400    нет маркера данных за найденным идентификатором (чтение)
//   1000   дорожка 0 не найдена за 1100 шагов
//   2000   неисправность: автотест, несуществующая команда, команда при неисправном
//          накопителе
//   10000  сектор не найден за два оборота
//   20000  КЦК идентификатора, 40000 КЦК данных
// Образ не портится и головки дорожку 0 находят, поэтому бывают только 2000 и 10000
#define ERR_FAULT       0002000     // неисправность
#define ERR_ID          0010000     // нет адреса
#define ERR_PRECOMP     0000040     // предкомпенсация со 128 цилиндра
#define ERR_RD          0177400
#define ERR_WR          0000377

#define SEC_RDWR        0177437
#define CYL_RDWR        0001777
#define HEAD_RDWR       0000007

// Команду прошивка различает по разрядам 6-4, остальные - что угодно (Alex_K): 6 и 5 -
// операция, 4 - направление. Запись (1): буфер, если он нужен, заполняется до диска, в
// конце сразу ЗОА. Чтение (0): в конце ЗОБ и буфер, ЗОА - когда его прочтут. Обычные
// коды - 020, 040, 060 и 120, но 000 - тоже возврат, а 100 - тоже разметка
#define CMD_DIR         0020
#define CMD_OP          0140
#define CMD_OP_RESTORE  0000
#define CMD_OP_SECTOR   0040
#define CMD_OP_FORMAT   0100

#define CSR_ERR         0000400
#define CSR_DRQ2        0004000
#define CSR_SEEK_DONE   0010000
#define CSR_READY       0040000
#define CSR_RD          0177400
#define CSR_WR          0000377

#define CALLBACK_INIT   100

// Сброс - автотест прошивки и возврат на дорожку 0. Время не известно; ограничено
// сверху проверкой регистров TESTDW ДВК, которая ждёт снятия «Занят» с ограничением
// (400 мс ещё проходят). Снизу ограничения нет: проверка прерываний TESTDW (сброс,
// пауза, разрешение) получает прерывание и после конца сброса - запрос там уровнем
#define INIT_US         200000
// Сектор, которого нет, ищется два оборота (ТО5, п. 4.5.2)
#define MISS_REVS       2

DWController::DWController(InterfaceManager *im, EmulatorConfigDevice *cd, unsigned int window):
      AddressableDevice(im, cd)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
{
    device_class = "hdd";
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = window;
    reset_ids();
}

void DWController::reset_ids()
{
    for (unsigned int i = 0; i < ID_TRACKS; i++) m_id_head[i] = (uint8_t)(i & 7);
}

// Образ, без начальной установки: из снимка регистры и линии приходят сами
emulator::Result DWController::open_image(const std::string &file_name)
{
    std::string error;
    m_cylinders = 0;
    if (!m_image.open(file_name, error))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Hard disk image file not found")) + "} " + file_name);
    // Образ известного накопителя - его геометрия. Иначе цилиндры из конфигурации или
    // по размеру файла. Образ больше заданной геометрии допустим, хвост не используется;
    // меньше - только там, где он растёт
    unsigned int cylinders = m_cfg_cylinders;
    m_heads = m_cfg_heads;
    m_sectors = m_cfg_sectors;
    known_geometry(m_image.size(), m_heads, cylinders, m_sectors);
    const uint64_t track = (uint64_t)m_heads * m_sectors * HddImage::SECTOR_SIZE;
    const uint64_t image_cylinders = m_image.size() / track;
    m_cylinders = (cylinders != 0) ? cylinders : (unsigned int)image_cylinders;
    if (m_cylinders == 0 || m_cylinders > 1024 || (!m_grows_image && image_cylinders < m_cylinders)) {
        m_image.close();
        m_cylinders = 0;
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unrecognized hard disk geometry")) + "} " + file_name);
    }
    if (m_grows_image) m_image.extend(track * m_cylinders, 0);
    reset_ids();
    return emulator::Result::ok();
}

// И при неудаче: гнездо могло опустеть
emulator::Result DWController::attach(const std::string &file_name)
{
    const emulator::Result res = open_image(file_name);
    init_controller();
    return res;
}

emulator::Result DWController::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    files = read_confg_value(cd, "files", false, std::string(""));
    m_image.set_write_protect(read_confg_value(cd, "write_protect", false, false));
    m_image.set_volatile(read_confg_value(cd, "volatile", false, false));

    m_heads = m_cfg_heads = read_confg_value(cd, "heads", false, (unsigned int)4);
    m_sectors = m_cfg_sectors = read_confg_value(cd, "sectors", false, (unsigned int)16);
    m_cfg_cylinders = read_confg_value(cd, "cylinders", false, (unsigned int)0);
    if (m_cfg_cylinders > 1024 || !geometry_ok())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unsupported hard disk geometry")) + "} " + name);
    const std::string small_jumper = cd->get_parameter("small", false).value;
    m_small = small_jumper.empty() ? -1 : (parse_numeric_value(small_jumper) != 0 ? 1 : 0);

    // Времена в микросекундах: поиск и поворот до сектора, переход головок на
    // цилиндр, передача слова буфера, начальная установка, оборот диска. Кроме
    // поиска сектора, которого нет, ТО5 времён не даёт: seek_time, step_time и word_time
    // подобраны под ДВК, init_time ограничен тестом ДВК сверху, а диск не вращается, и
    // найденный сектор приходит сразу за поиском дорожки
    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));

    m_seek_us = read_confg_value(cd, "seek_time", false, (unsigned int)2000);
    m_step_us = read_confg_value(cd, "step_time", false, (unsigned int)200);
    m_word_us = read_confg_value(cd, "word_time", false, (unsigned int)2);
    m_init_us = read_confg_value(cd, "init_time", false, (unsigned int)INIT_US);
    m_rev_us = read_confg_value(cd, "format_time", false, (unsigned int)16667);
    return emulator::Result::ok();
}

// Образ на мегабайты возят отдельно от машины: его отсутствие не ошибка, гнездо
// пустое, образ вставляют командой load или из меню
emulator::Result DWController::load_image()
{
    const std::string image = read_confg_value(cd, "image", false, std::string(""));
    if (!image.empty()) {
        const std::string file = find_file_location(sd, image);
        if (!file.empty()) {
            const emulator::Result res = open_image(file);
            if (!res) return res;
        }
    }
    init_controller();
    return emulator::Result::ok();
}

void DWController::reset(MAYBE_UNUSED bool cold)
{
    init_controller();
}

void DWController::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT && (new_value & 1)) init_controller();
}

//--------------------------- Запросы обмена --------------------------------//

// РСНУ целиком; о каждом поднявшемся или упавшем запросе узнаёт шина
void DWController::set_status(unsigned int status)
{
    const unsigned int changed = m_status ^ status;
    m_status = status;
    if (changed & ST_DONE) request_changed(REQ_A, (status & ST_DONE) != 0);
    if (changed & ST_DRQ1) request_changed(REQ_B, (status & ST_DRQ1) != 0);
}

void DWController::raise_done()
{
    set_status(m_status | ST_DONE);
}

void DWController::clear_done()
{
    set_status(m_status & ~ST_DONE);
}

void DWController::raise_data()
{
    set_status(m_status | ST_DRQ1);
}

void DWController::clear_data()
{
    set_status(m_status & ~ST_DRQ1);
}

//--------------------------- Ход операции ----------------------------------//

void DWController::schedule(unsigned int event, unsigned int us)
{
    m_event = event;
    m_timeout = us;
    m_acc = 0;
}

void DWController::schedule_word()
{
    m_word_pending = true;
    m_word_timeout = m_word_us;
    m_word_acc = 0;
}

void DWController::adopt_word_event()
{
    m_event = EV_NONE;
    m_word_pending = true;
    m_word_timeout = m_timeout;
    m_word_acc = m_acc;
}

unsigned int DWController::seek_us(unsigned int cylinder) const
{
    const unsigned int d = (cylinder > m_track) ? cylinder - m_track : m_track - cylinder;
    return m_seek_us + d * m_step_us;
}

bool DWController::small_drive() const
{
    return m_small > 0 || (m_small < 0 && m_cylinders != 0 && m_cylinders <= SMALL_CYLINDERS);
}

// Начальная установка - операция: «Занят», очистка регистров, автотест и переход на
// нулевую дорожку, а в конце ЗОА и «Операция завершена» (п. 4.5.5). Прошивка ждёт от
// накопителя готовности и «Установка завершена» в бесконечном цикле: без накопителя
// (образа) «Занят» не снимается, и отвечает только РСНУ (Alex_K)
void DWController::init_controller()
{
    m_err = ERR_PRECOMP;
    m_sector = m_cyl = m_head = 0;
    m_csr = 0;
    m_pointer = 0;
    m_op = OP_NONE;
    m_word_pending = false;
    bus_init();
    set_status(ST_BUSY);
    if (m_image.attached())
        schedule(EV_INIT, m_init_us);
    else
        m_event = EV_NONE;
}

void DWController::clock(unsigned int counter)
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
    event_done(event);
}

void DWController::event_done(unsigned int event)
{
    switch (event) {
    case EV_INIT:
    case EV_RESTORE:
        m_track = 0;
        if (m_image.attached()) m_csr |= CSR_READY | CSR_SEEK_DONE;
        if (event == EV_INIT)
            finish();
        else
            complete(0);
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
    case EV_WRITE_MISS:
        complete(ERR_ID);
        break;
    }
}

// Накопитель видит столько разрядов РАП, сколько нужно на его головки: у накопителя на
// 4 головки поверхности 4-7 совпадают с 0-3, а у RD32 на 6 головок поверхностей 6 и 7
// нет вовсе, и обращение к ним кончается, как за последним цилиндром
unsigned int DWController::physical_head() const
{
    unsigned int mask = 0;
    while (mask + 1 < m_heads) mask = (mask << 1) | 1;
    return m_head & mask;
}

// Начало дорожки в образе. Дальше последнего цилиндра и последней поверхности накопитель
// не идёт
bool DWController::track_offset(uint64_t &offset) const
{
    if (!m_image.attached() || m_cyl >= m_cylinders || physical_head() >= m_heads)
        return false;
    offset = (uint64_t)(m_cyl * m_heads + physical_head()) * m_sectors * HddImage::SECTOR_SIZE;
    return true;
}

// Найдётся ли сектор из регистров: дорожка есть, номер на ней есть, и её
// идентификаторы совпадают с регистрами целиком
bool DWController::sector_found(uint64_t &offset) const
{
    unsigned int index = 0;
    if (!track_offset(offset) || !image_sector(m_sector & 037, index) || index >= m_sectors)
        return false;
    offset += (uint64_t)index * HddImage::SECTOR_SIZE;
    return m_id_head[m_cyl * 8 + physical_head()] == m_head;
}

// Конец операции: снят «Занят», ЗОА и «Операция завершена». m_op остаётся: что сделает
// следующий проход буфера
void DWController::finish()
{
    m_word_pending = false;
    m_csr &= ~CSR_DRQ2;
    m_status &= ~ST_BUSY;
    raise_done();
}

void DWController::fail(unsigned int err)
{
    m_err |= err;
    m_csr |= CSR_ERR;
    finish();
}

// Буфер готов к выдаче программе
void DWController::give_buffer()
{
    m_pointer = 0;
    m_op = OP_READ;
    m_status &= ~ST_BUSY;
    m_csr |= CSR_DRQ2;
    raise_data();
}

// Конец дисковой части команды, с ошибкой или без: у записи (разряд 4) сразу ЗОА, у
// чтения - ЗОБ и буфер, ЗОА - когда его прочтут
void DWController::complete(unsigned int err)
{
    if (err != 0) {
        m_err |= err;
        m_csr |= CSR_ERR;
    }
    if (m_command & CMD_DIR)
        finish();
    else
        give_buffer();
}

// Адрес буфера вернулся к началу: прошивка делает то, что велит последняя команда
// (Alex_K: счётчик один на чтение и запись, и по концу буфера «может выполняться
// последняя операция с диском» - прочитанный после записи буфер записывается снова).
// После сброса и возврата 020 буфер просто идёт по кругу (тест буфера TESTDW пишет и
// читает 256 слов без команды)
void DWController::buffer_end()
{
    switch (m_op) {
    case OP_READ:
        finish();
        break;
    case OP_WRITE:
    case OP_FORMAT:
        // Контроллер уходит на диск; разметка ждёт индекса и идёт оборот
        m_word_pending = false;
        m_status |= ST_BUSY;
        if (m_op == OP_FORMAT)
            schedule(EV_FORMAT, seek_us(m_cyl) + m_rev_us);
        else
            schedule(EV_WRITE, seek_us(m_cyl));
        break;
    default:
        schedule_word();
        break;
    }
}

// Найден сектор или нет, чтение кончается одинаково: ЗОБ и «Запрос данных», буфер
// отдаётся программе, а ЗОА приходит, когда его прочтут. Сектор не нашёлся за два оборота
// (EV_READ_MISS) - в буфере остаётся прежнее, и стоят разряды ошибки. Обработчик DW из
// RT-11 Э-85 на чтение разрешает только ЗОБ и при ошибке опустошает буфер сам (DUMP), а
// DW с V5.4A, определяя размер, ждёт «Запрос данных» опросом: ЗОА без ЗОБ повесили бы обоих
void DWController::read_done()
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
    complete(0);
}

void DWController::write_done()
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
        complete(ERR_FAULT);
        return;
    }
    if (m_sector >> 8) note_unsupported("service zone");
    m_writes++;
    complete(0);
}

// Первые 16 слов буфера - порядок секторов на дорожке, физического порядка в образе нет.
// Размечаются 16 секторов, поля данных и 16 байт служебной зоны - нулями (Alex_K;
// служебная зона не хранится)
void DWController::format_done()
{
    uint64_t offset;
    uint8_t data[HddImage::SECTOR_SIZE];
    m_track = (m_cyl < m_cylinders) ? m_cyl : m_cylinders - 1;
    if (!track_offset(offset)) {
        complete(ERR_ID);
        return;
    }
    memset(data, 0, sizeof(data));
    const unsigned int sectors = (m_sectors < 16) ? m_sectors : 16;
    for (unsigned int s = 0; s < sectors; s++)
        if (!m_image.write(offset + (uint64_t)s * HddImage::SECTOR_SIZE, data)) {
            complete(ERR_FAULT);
            return;
        }
    m_id_head[m_cyl * 8 + physical_head()] = (uint8_t)m_head;
    m_formats++;
    complete(0);
}

void DWController::command(unsigned int cmd)
{
    m_command = cmd;
    m_err &= ~ERR_RD;
    m_csr &= ~(CSR_ERR | CSR_DRQ2);
    m_op = OP_NONE;
    m_word_pending = false;
    m_pointer = 0;
    // Команда занимает контроллер целиком: незаконченная операция больше не кончится.
    // Пока он занят, команду не записать (отказ шины), так что это обмен буфером
    m_event = EV_NONE;
    clear_done();
    clear_data();

    // Команда выполняется, только когда накопитель готов и установка завершена (п. 4.5.1),
    // а без накопителя прошивка не выходит из сброса. Остаётся снимок, где образа уже нет:
    // «неисправность» (табл. 3)
    if (!m_image.attached()) {
        m_csr &= ~(CSR_READY | CSR_SEEK_DONE);
        fail(ERR_FAULT);
        return;
    }
    m_csr |= CSR_READY | CSR_SEEK_DONE;
    const bool write = (cmd & CMD_DIR) != 0;
    switch (cmd & CMD_OP) {
    case CMD_OP_RESTORE:
        m_status |= ST_BUSY;
        schedule(EV_RESTORE, seek_us(0));
        break;
    case CMD_OP_SECTOR:
    case CMD_OP_FORMAT:
        if (write) {
            // Сначала буфер: сектор или порядок секторов дорожки
            m_op = ((cmd & CMD_OP) == CMD_OP_FORMAT) ? OP_FORMAT : OP_WRITE;
            m_csr |= CSR_DRQ2;
            raise_data();
        } else if ((cmd & CMD_OP) == CMD_OP_FORMAT) {
            // Разметка «чтением» (100) - тем, что уже лежит в буфере
            m_status |= ST_BUSY;
            schedule(EV_FORMAT, seek_us(m_cyl) + m_rev_us);
        } else {
            m_status |= ST_BUSY;
            schedule(EV_READ, seek_us(m_cyl));
        }
        break;
    default:
        // Разряды 6 и 5 оба - несуществующая команда (табл. 3)
        fail(ERR_FAULT);
        break;
    }
}

//--------------------------- Регистры --------------------------------------//

// Обращение к РД: ЗОБ снят, адрес буфера - дальше; следующее слово готово через
// word_time, а пройденный до конца буфер - событие прошивки
void DWController::next_word()
{
    m_pointer = (m_pointer + 1) & 255;
    clear_data();
    if (m_pointer == 0)
        buffer_end();
    else
        schedule_word();
}

// Любое обращение к РКС2 снимает ЗОА и ставит адрес буфера в начало: прошивка, разбирая
// регистр, берёт ОЗУ служебной зоны и потом возвращает адрес к началу данных (Alex_K)
void DWController::csr_touched()
{
    m_pointer = 0;
    clear_done();
}

unsigned int DWController::read_reg(unsigned int reg, bool peek)
{
    switch (reg) {
    case DW_ID:
        if (!peek) clear_data();
        return 0401;
    case DW_ERR:
        return m_err & ERR_RD;
    case DW_SECTOR:
        if (!peek) {
            m_pointer = 0;
            clear_done();
        }
        return m_sector & SEC_RDWR;
    case DW_DATA: {
        const unsigned int data = m_buf[m_pointer & 255];
        if (!peek) next_word();
        return data;
    }
    case DW_CYL:
        return m_cyl & CYL_RDWR;
    case DW_HEAD:
        return m_head & HEAD_RDWR;
    case DW_CSR:
        if (!peek) csr_touched();
        return m_csr & CSR_RD;
    case DW_STATUS: {
        unsigned int v = m_status & (ST_BUSY | ST_DRQ1 | ST_DONE);
        if (small_drive()) v |= ST_SMALL;
        return v;
    }
    default:
        return 0;
    }
}

void DWController::write_reg(unsigned int reg, unsigned int value)
{
    value &= 0xFFFF;
    switch (reg) {
    case DW_ID:
        clear_data();
        break;
    case DW_ERR:
        m_err = (m_err & ~ERR_WR) | (value & ERR_WR);
        break;
    case DW_SECTOR:
        m_sector = value & SEC_RDWR;
        m_pointer = 0;
        clear_done();
        break;
    case DW_DATA:
        m_buf[m_pointer & 255] = (uint16_t)value;
        next_word();
        break;
    case DW_CYL:
        m_cyl = value & CYL_RDWR;
        break;
    case DW_HEAD:
        m_head = value & HEAD_RDWR;
        break;
    case DW_CSR:
        command(value & CSR_WR);
        break;
    case DW_STATUS:
        status_written(value);
        if (value & ST_INIT) init_controller();
        break;
    default:
        break;
    }
}

void DWController::trace(unsigned int kind, unsigned int reg, unsigned int value)
{
    // Слова данных вытеснили бы из журнала всё остальное за два сектора
    if (reg == DW_DATA) return;
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

// Пока контроллер занят, ответить на шине может только РСНУ: обращение к остальным - без
// ответа, тайм-аут. Устройство ставит MemoryMapper::no_device само: диспетчер снимает
// флаг перед вызовом
bool DWController::refused(unsigned int address)
{
    if (!busy() || reg_of(address) == DW_STATUS) return false;
    m_refused++;
    if (m_mapper != nullptr) m_mapper->no_device = true;
    return true;
}

unsigned int DWController::get_value_word(unsigned int address)
{
    if (refused(address)) return 0xFFFF;
    const unsigned int reg = reg_of(address);
    const unsigned int v = read_reg(reg, false) & 0xFFFF;
    trace(0, reg, v);
    return v;
}

void DWController::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (refused(address)) return;
    const unsigned int reg = reg_of(address);
    trace(1, reg, value);
    write_reg(reg, value);
}

unsigned int DWController::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

// Байт пишется в свою половину регистра. Старшие байты РКС2, РОШПК и РСНУ только
// читаются, но обращение к РКС2 есть обращение: оно снимает ЗОА и адрес буфера. В РСНУ
// записываемые разряды (сброс, у ДВК и разрешение) - в младшем байте, а разрешение не
// читается: слово из прочитанного младшего байта его бы сняло
void DWController::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (refused(address)) return;
    const unsigned int reg = reg_of(address);
    if (address & 1) {
        if (reg == DW_CSR) csr_touched();
        if (reg == DW_CSR || reg == DW_ERR || reg == DW_STATUS) return;
    }
    const unsigned int old = read_reg(reg, true);
    const unsigned int w = (address & 1) ? ((old & 0x00FF) | ((value & 0xFF) << 8))
                                         : ((old & 0xFF00) | (value & 0xFF));
    trace(2, reg, w);
    write_reg(reg, w);
}

unsigned DWController::get_direct(unsigned address)
{
    const unsigned int w = read_reg(reg_of(address), true);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

//--------------------------- Поля и команды --------------------------------//

ConfigFields DWController::get_config_fields()
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

std::vector<DeviceFieldInfo> DWController::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"attached",  "1, когда образ вставлен",                   false});
    r.push_back({"file",      "Имя образа",                                false});
    r.push_back({"protected", "1, когда запись в образ запрещена",          false});
    r.push_back({"volatile",  "1, когда запись идёт в память, а не в файл", false});
    r.push_back({"geometry",  "Цилиндры, головки и секторы образа",         false});
    r.push_back({"csr",       "Регистр РКС2 (+16)",                        false});
    r.push_back({"err",       "Регистр ошибок РОШПК (+4), с записанной предкомпенсацией", false});
    r.push_back({"status",    "Регистр РСНУ (+20), как его читает программа", false});
    r.push_back({"command",   "Последняя команда",                         false});
    r.push_back({"cylinder",  "Цилиндр в регистре",                        false});
    r.push_back({"head",      "Головка в регистре",                        false});
    r.push_back({"sector",    "Сектор в регистре",                         false});
    r.push_back({"reads",     "Сколько секторов прочитано",                 false});
    r.push_back({"writes",    "Сколько секторов записано",                  false});
    r.push_back({"formats",   "Сколько дорожек размечено",                  false});
    r.push_back({"refused",   "Обращений к регистрам, пока контроллер занят (без ответа)", false});
    r.push_back({"trace",     "Последние обращения к регистрам, кроме РД: вид (0 чтение, 1 запись слова, 2 байта), номер слова регистра, значение", true});
    return r;
}

std::vector<DeviceCommandInfo> DWController::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"load",     "\"file\"", "Вставляет образ винчестера"});
    r.push_back({"eject",    "",         "Вынимает образ"});
    r.push_back({"protect",  "[0|1]",    "Запрещает или разрешает запись в образ"});
    r.push_back({"volatile", "[0|1]",    "Запись в память вместо файла; выключение забывает записанное"});
    return r;
}

bool DWController::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
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
    if (field == "err")       { out.values.push_back(m_err);      return true; }
    if (field == "status")    { out.values.push_back(read_reg(DW_STATUS, true)); return true; }
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

emulator::Result DWController::send_command(const std::string &command, const std::string &parameters)
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

//--------------------------- Снимок ----------------------------------------//

void DWController::save_state(StateWriter &w)
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

// Ключи «status», «pointer», «count», «formats», «refused» и «id_head» были только у e85-knmd:
// снимок dvk-dw прежнего вида их не несёт, и наследник переводит свои
emulator::Result DWController::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;

    // Образ - первым: регистры ниже - уже его. Без начальной установки: линии
    // восстановлены
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
    // Счётчик слов («count» прежних снимков) - сам адрес буфера: конец передачи - его
    // возврат к началу
    r.u("pointer", m_pointer);
    m_pointer &= 255;
    r.u("op", m_op);
    if (m_op > OP_FORMAT) m_op = OP_NONE;
    r.u("event", m_event);
    if (m_event > EV_INIT) m_event = EV_NONE;
    // Снимок до разрядов 6-4 (с ключом «count»): сброс шёл тем же событием, что и
    // возврат, а возврат был только 020 - он кончается так же, как сброс
    unsigned int legacy_count = 0;
    if (m_event == EV_RESTORE && r.u("count", legacy_count)) m_event = EV_INIT;
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
