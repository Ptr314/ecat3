// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ IDE hard disk controller, source

#include <cstring>

#include "uknc_hdd.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"

#ifdef _WIN32
#include <cwchar>
#endif

#define SECTOR_SIZE         512

// Регистры накопителя, номер - разряды 1-3 адреса в обратном коде
#define REG_DATA            0
#define REG_ERROR           1
#define REG_SECTOR_COUNT    2
#define REG_SECTOR_NUMBER   3
#define REG_CYLINDER_LSB    4
#define REG_CYLINDER_MSB    5
#define REG_HEAD_NUMBER     6
#define REG_STATUS          7

#define ST_ERROR            0x01
#define ST_INDEX            0x02
#define ST_BUFFER_READY     0x08
#define ST_SEEK_COMPLETE    0x10
#define ST_DRIVE_READY      0x40
#define ST_BUSY             0x80

#define CMD_READ            0x20
#define CMD_READ1           0x21
#define CMD_WRITE           0x30
#define CMD_WRITE1          0x31
#define CMD_SET_CONFIG      0x91
#define CMD_IDENTIFY        0xEC

#define ERR_NONE            0x00
#define ERR_BAD_SECTOR      0x80

// Отложенные события
#define EVT_NONE            0
#define EVT_RESET_DONE      1
#define EVT_READ_DONE       2
#define EVT_WRITE_DONE      3

// Разряд 6 регистра головки выбирает линейную адресацию вместо
// цилиндр-головка-сектор, разряды 4 и ниже - накопитель и головку
#define HEAD_LBA            0x40
#define HEAD_MASK           0x0F

// Сколько накопитель тратит на сектор и на поиск дорожки, мкс. У винчестера
// тех лет оборот 16,7 мс и 63 сектора на дорожке - это 265 мкс на сектор;
// поиск дорожки дольше на порядок. UKNCBTL держит гораздо большие выдержки
// (256 тактов кадра по 4 мкс на сектор), и драйверы с коротким собственным
// ожиданием разряда «буфер готов» на них не укладываются
#define DEFAULT_SECTOR_US   256
#define DEFAULT_SEEK_US     1000

UKNCHDD::UKNCHDD(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    device_class = "hdd";
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 020000;   // верхняя половина окна кассеты, 110000-117777
    memset(m_buffer, 0, sizeof(m_buffer));
}

UKNCHDD::~UKNCHDD()
{
    unload();
}

//--------------------------- Образ винчестера ------------------------------//

// Имя файла в кодировке UTF-8: на Windows его надо перевести в UTF-16, иначе
// путь с кириллицей не откроется. Образ открывается на чтение и запись -
// машина пишет прямо в него, как настоящая в свой диск
static std::FILE * open_image(const std::string &file_name, bool for_write)
{
#ifdef _WIN32
    std::wstring w = dsk_tools::utf8_to_wide(file_name);
    return _wfopen(w.c_str(), for_write? L"r+b" : L"rb");
#else
    return std::fopen(file_name.c_str(), for_write? "r+b" : "rb");
#endif
}

emulator::Result UKNCHDD::load_image(const std::string &file_name)
{
    // Новый образ открывается и проверяется без замка; старый закрывается и
    // заменяется уже под ним. Неудача оставляет гнездо пустым, как и раньше
    unload();

    bool read_only = false;
    std::FILE * f = open_image(file_name, true);
    if (f == nullptr) {
        read_only = true;
        f = open_image(file_name, false);
    }
    if (f == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCHDD|" + std::string(QT_TRANSLATE_NOOP("UKNCHDD", "Hard disk image file not found")) + "} " + file_name);

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    uint8_t first[SECTOR_SIZE];
    if (size <= 0 || (size % SECTOR_SIZE) != 0
        || std::fread(first, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
        std::fclose(f);
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCHDD|" + std::string(QT_TRANSLATE_NOOP("UKNCHDD", "Unrecognized hard disk image")) + "} " + file_name);
    }

    unsigned int sectors = 0, heads = 0;
    bool inverted = false;
    const uint16_t * w = (const uint16_t *)first;
    if ((w[0] == 0x54A9 && w[1] == 0xFFEF && w[2] == 0xFEFF)
        || (w[0] == 0xAB56 && w[1] == 0x0010 && w[2] == 0x0100)) {
        // Разметка HD: число секторов и их общее число на цилиндр лежат
        // словами, обратный код узнаётся по заголовку
        inverted = (w[0] == 0xAB56);
        uint16_t nsec = w[4], ncyl = w[5];
        if (inverted) { nsec = ~nsec; ncyl = ~ncyl; }
        sectors = nsec;
        if (sectors != 0) heads = (ncyl / sectors) & 0xFF;
    } else {
        // Разметка WD и ID: первый байт - секторы, второй - головки.
        // Образ в обратном коде узнаётся по пустому месту в конце сектора
        uint8_t test = 0xFF;
        for (int i = 0x1F0; i <= 0x1FB; i++) test &= first[i];
        inverted = (test == 0xFF);
        sectors = inverted? (uint8_t)~first[0] : first[0];
        heads   = inverted? (uint8_t)~first[1] : first[1];
    }

    unsigned int cylinders = 0;
    if (sectors != 0 && heads != 0)
        cylinders = (unsigned int)(size / SECTOR_SIZE / sectors / heads);

    if (sectors == 0 || heads == 0 || cylinders == 0 || cylinders > 1024) {
        std::fclose(f);
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCHDD|" + std::string(QT_TRANSLATE_NOOP("UKNCHDD", "Unrecognized hard disk geometry")) + "} " + file_name);
    }

    {
        compat_lock_guard lock(m_image_mutex);
        close_image();
        m_file = f;
        m_file_name = file_name;
        m_image_size = (uint64_t)size;
        m_read_only = read_only;
        m_inverted = inverted;
        m_attached = true;
        m_sectors = sectors;
        m_heads = heads;
        m_cylinders = cylinders;
    }

    reset(true);

    return emulator::Result::ok();
}

void UKNCHDD::close_image()
{
    if (m_file != nullptr) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_attached = false;
    m_file_name.clear();
    m_image_size = 0;
    m_overlay.clear();
    m_cylinders = m_heads = m_sectors = 0;
}

void UKNCHDD::unload()
{
    compat_lock_guard lock(m_image_mutex);
    close_image();
}

emulator::Result UKNCHDD::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    files = read_confg_value(cd, "files", false, std::string(""));
    m_write_protect = read_confg_value(cd, "write_protect", false, false);
    m_volatile = read_confg_value(cd, "volatile", false, false);

    // Время в микросекундах: у машины оно своё у каждого накопителя, а
    // драйверы ждут готовности со своими выдержками
    m_sector_us = read_confg_value(cd, "sector_time", false, (unsigned int)DEFAULT_SECTOR_US);
    m_seek_us   = read_confg_value(cd, "seek_time",   false, (unsigned int)DEFAULT_SEEK_US);

    // Образ винчестера - файл на сотни мегабайт, его не кладут рядом с
    // машиной и не возят с эмулятором. Поэтому его отсутствие не ошибка:
    // гнездо просто пустое, а образ вставляют командой load
    const std::string image = read_confg_value(cd, "image", false, std::string(""));
    if (!image.empty()) {
        const std::string file = find_file_location(sd, image);
        if (!file.empty()) {
            emulator::Result img = load_image(file);
            if (!img) return img;
        }
    }

    return emulator::Result::ok();
}

//--------------------------- Обмен с образом -------------------------------//

void UKNCHDD::invert_buffer()
{
    for (unsigned int i = 0; i < SECTOR_SIZE; i++) m_buffer[i] = (uint8_t)~m_buffer[i];
}

bool UKNCHDD::lba_mode() const
{
    return (m_curheadreg & HEAD_LBA) != 0;
}

uint64_t UKNCHDD::sector_offset() const
{
    // Линейная адресация: те же четыре регистра несут одно 28-разрядное
    // число - разряды 0-7 в номере сектора, 8-23 в цилиндре, 24-27 в младшей
    // тетраде регистра головки. Геометрия при этом ни при чём
    if (lba_mode())
        return (uint64_t)(((uint32_t)(m_curheadreg & HEAD_MASK) << 24)
                          | ((m_curcylinder & 0xFFFF) << 8)
                          | (m_cursector & 0xFF)) * SECTOR_SIZE;

    // Цилиндр-головка-сектор: сектора на дорожке нумеруются с единицы
    const uint64_t sector = ((uint64_t)m_curcylinder * m_heads + m_curhead) * m_sectors
                          + (m_cursector > 0? m_cursector - 1 : 0);
    return sector * SECTOR_SIZE;
}

bool UKNCHDD::read_sector()
{
    compat_lock_guard lock(m_image_mutex);
    if (m_file == nullptr) return false;
    const uint64_t offset = sector_offset();
    if (offset > 0x7FFFFFFFull) return false;
    const auto kept = m_overlay.find(offset);
    if (kept != m_overlay.end())
        memcpy(m_buffer, kept->second.data(), SECTOR_SIZE);
    else {
        if (std::fseek(m_file, (long)offset, SEEK_SET) != 0) return false;
        if (std::fread(m_buffer, 1, SECTOR_SIZE, m_file) != SECTOR_SIZE) return false;
    }
    m_sectors_read++;
    return true;
}

bool UKNCHDD::write_sector()
{
    compat_lock_guard lock(m_image_mutex);
    if (m_file == nullptr || m_write_protect) return false;
    const uint64_t offset = sector_offset();
    if (offset + SECTOR_SIZE > m_image_size) return false;
    if (m_volatile) {
        // Файл только на чтение этому не мешает: в него ничего не пишется
        memcpy(m_overlay[offset].data(), m_buffer, SECTOR_SIZE);
        m_sectors_written++;
        return true;
    }
    if (m_read_only || offset > 0x7FFFFFFFull) return false;
    if (std::fseek(m_file, (long)offset, SEEK_SET) != 0) return false;
    if (std::fwrite(m_buffer, 1, SECTOR_SIZE, m_file) != SECTOR_SIZE) return false;
    std::fflush(m_file);
    m_sectors_written++;
    return true;
}

//--------------------------- Ход операции ----------------------------------//

void UKNCHDD::schedule(unsigned int event, unsigned int us)
{
    m_event = event;
    m_timeout = us;
    // The ticks left over from the previous event would make this one come
    // early - by several microseconds after a long time slice
    m_acc = 0;
}

void UKNCHDD::reset(MAYBE_UNUSED bool cold)
{
    m_status = ST_BUSY;
    m_error = ERR_NONE;
    m_command = 0;
    m_bufferoffset = 0;
    m_curcylinder = m_curhead = m_curheadreg = m_cursector = 0;
    m_sectorcount = 0;
    m_acc = 0;
    schedule(EVT_RESET_DONE, 8);
}

void UKNCHDD::clock(unsigned int counter)
{
    if (m_event == EVT_NONE || m_system_clock == 0) return;

    // Такты домена в микросекунды: на 6,25 МГц микросекунда - это 6,25 такта,
    // поэтому копится дробь
    m_acc += (uint64_t)counter * 1000000ull;
    const uint64_t step = m_system_clock;
    while (m_acc >= step && m_timeout > 0) {
        m_acc -= step;
        m_timeout--;
    }
    if (m_timeout > 0) return;

    const unsigned int event = m_event;
    m_event = EVT_NONE;
    switch (event) {
        case EVT_RESET_DONE:
            m_status &= ~ST_BUSY;
            m_status |= ST_DRIVE_READY | ST_SEEK_COMPLETE;
            break;
        case EVT_READ_DONE:
            read_sector_done();
            break;
        case EVT_WRITE_DONE:
            write_sector_done();
            break;
    }
}

void UKNCHDD::read_sector_done()
{
    m_status &= ~(ST_BUSY | ST_ERROR);
    m_status |= ST_BUFFER_READY | ST_SEEK_COMPLETE;

    if (!read_sector()) {
        m_status |= ST_ERROR;
        m_error = ERR_BAD_SECTOR;
        return;
    }

    if (m_sectorcount > 0) m_sectorcount--;
    if (m_sectorcount > 0) next_sector();

    m_error = ERR_NONE;
    m_bufferoffset = 0;
}

void UKNCHDD::write_sector_done()
{
    m_status &= ~(ST_BUSY | ST_ERROR);
    m_status |= ST_BUFFER_READY | ST_SEEK_COMPLETE;

    if (!write_sector()) {
        m_status |= ST_ERROR;
        m_error = ERR_BAD_SECTOR;
        return;
    }

    if (m_sectorcount > 0) m_sectorcount--;
    if (m_sectorcount > 0) next_sector();

    m_error = ERR_NONE;
    m_bufferoffset = 0;
}

void UKNCHDD::next_sector()
{
    // В линейной адресации регистры после передачи показывают следующий
    // адрес тем же числом, разложенным по тем же местам
    if (lba_mode()) {
        const uint32_t lba = (((uint32_t)(m_curheadreg & HEAD_MASK) << 24)
                              | ((m_curcylinder & 0xFFFF) << 8)
                              | (m_cursector & 0xFF)) + 1;
        m_cursector = lba & 0xFF;
        m_curcylinder = (lba >> 8) & 0xFFFF;
        m_curheadreg = (m_curheadreg & ~HEAD_MASK) | ((lba >> 24) & HEAD_MASK);
        m_curhead = m_curheadreg & HEAD_MASK;
        return;
    }

    m_cursector++;
    if (m_cursector > m_sectors) {
        m_cursector = 1;
        m_curhead++;
        if (m_curhead >= m_heads) {
            m_curhead = 0;
            m_curcylinder++;
        }
    }
}

void UKNCHDD::continue_read()
{
    m_bufferoffset = 0;
    m_status &= ~(ST_BUFFER_READY | ST_BUSY);

    if (m_sectorcount > 0) {
        m_status |= ST_BUSY;
        schedule(EVT_READ_DONE, m_sector_us);        // переход на следующий сектор
    }
}

void UKNCHDD::continue_write()
{
    m_bufferoffset = 0;
    m_status &= ~ST_BUFFER_READY;
    m_status |= ST_BUSY;
    schedule(EVT_WRITE_DONE, m_sector_us);
}

void UKNCHDD::handle_command(unsigned int command)
{
    m_command = command;
    switch (command) {
        case CMD_READ:
        case CMD_READ1:
            m_status |= ST_BUSY;
            m_status &= ~ST_BUFFER_READY;
            schedule(EVT_READ_DONE, m_seek_us);      // поиск дорожки
            break;

        case CMD_SET_CONFIG:
            // Программа разметки сама объявляет геометрию накопителя
            if (m_sectorcount != 0) m_sectors = m_sectorcount;
            m_heads = m_curhead + 1;
            break;

        case CMD_WRITE:
        case CMD_WRITE1:
            m_bufferoffset = 0;
            m_status |= ST_BUFFER_READY;
            break;

        case CMD_IDENTIFY:
            identify_drive();
            m_bufferoffset = 0;
            m_sectorcount = 1;
            m_status |= ST_BUFFER_READY | ST_SEEK_COMPLETE | ST_DRIVE_READY;
            m_status &= ~(ST_BUSY | ST_ERROR);
            break;

        default:
            break;
    }
}

// Строка в поле паспорта накопителя: байты в слове переставлены
static void swap_strncpy(uint8_t * dst, const char * src, int words)
{
    int i;
    for (i = 0; src[i] != 0; i++) dst[i ^ 1] = (uint8_t)src[i];
    for (; i < words * 2; i++) dst[i ^ 1] = ' ';
}

void UKNCHDD::identify_drive()
{
    const uint32_t total = (uint32_t)m_cylinders * m_heads * m_sectors;

    memset(m_buffer, 0, SECTOR_SIZE);

    uint16_t * w = (uint16_t *)m_buffer;
    w[0]  = 0x045A;                     // несъёмный накопитель
    w[1]  = (uint16_t)m_cylinders;
    w[3]  = (uint16_t)m_heads;
    w[6]  = (uint16_t)m_sectors;
    swap_strncpy((uint8_t*)(w + 10), "0000000000", 10);     // заводской номер
    swap_strncpy((uint8_t*)(w + 23), "1.0", 4);             // версия прошивки
    swap_strncpy((uint8_t*)(w + 27), "eCat3 Hard Disk", 18);// модель
    w[47] = 0x8001;
    w[49] = 0x2F00;
    w[53] = 1;
    w[54] = (uint16_t)m_cylinders;
    w[55] = (uint16_t)m_heads;
    w[56] = (uint16_t)m_sectors;
    const uint32_t track = (uint32_t)m_heads * m_sectors;
    memcpy(w + 57, &track, 4);
    memcpy(w + 60, &total, 4);
    memcpy(w + 100, &total, 4);

    // Паспорт кладётся в буфер в обратном коде - так же, как его отдал бы
    // накопитель через инвертирующую магистраль. Дальше с ним обращаются как
    // с прочитанным сектором, поэтому машине он достаётся прямым кодом
    // только у образов в обратном коде; так же ведёт себя и UKNCBTL
    invert_buffer();
}

//--------------------------- Регистры накопителя ---------------------------//

unsigned int UKNCHDD::read_port(unsigned int reg, bool peek)
{
    switch (reg) {
        case REG_DATA: {
            if ((m_status & ST_BUFFER_READY) == 0) return 0;
            uint16_t data;
            memcpy(&data, m_buffer + m_bufferoffset, 2);
            if (!m_inverted) data = (uint16_t)~data;
            if (peek) return data;
            m_bufferoffset += 2;
            if (m_bufferoffset >= SECTOR_SIZE) continue_read();
            return data;
        }
        case REG_ERROR:         return 0xFF00 | m_error;
        case REG_SECTOR_COUNT:  return 0xFF00 | (m_sectorcount & 0xFF);
        case REG_SECTOR_NUMBER: return 0xFF00 | (m_cursector & 0xFF);
        case REG_CYLINDER_LSB:  return 0xFF00 | (m_curcylinder & 0xFF);
        case REG_CYLINDER_MSB:  return 0xFF00 | ((m_curcylinder >> 8) & 0xFF);
        case REG_HEAD_NUMBER:   return 0xFF00 | (m_curheadreg & 0xFF);
        default:                return 0xFF00 | m_status;
    }
}

void UKNCHDD::set_port(unsigned int reg, unsigned int value)
{
    switch (reg) {
        case REG_DATA: {
            if ((m_status & ST_BUFFER_READY) == 0) return;
            uint16_t data = (uint16_t)value;
            if (!m_inverted) data = (uint16_t)~data;
            memcpy(m_buffer + m_bufferoffset, &data, 2);
            m_bufferoffset += 2;
            if (m_bufferoffset >= SECTOR_SIZE) continue_write();
            break;
        }
        case REG_ERROR:
            // Запись предкомпенсации накопителю не нужна
            break;
        case REG_SECTOR_COUNT:
            m_sectorcount = (value & 0xFF) == 0? 256 : (value & 0xFF);
            break;
        case REG_SECTOR_NUMBER:
            m_cursector = value & 0xFF;
            break;
        case REG_CYLINDER_LSB:
            m_curcylinder = (m_curcylinder & 0xFF00) | (value & 0xFF);
            break;
        case REG_CYLINDER_MSB:
            m_curcylinder = (m_curcylinder & 0x00FF) | ((value & 0xFF) << 8);
            break;
        case REG_HEAD_NUMBER:
            m_curheadreg = value & 0xFF;
            m_curhead = value & HEAD_MASK;
            break;
        default:
            handle_command(value & 0xFF);
            break;
    }
}

//--------------------------- Обращения с шины ------------------------------//

// Разряды 1-3 адреса выбирают регистр в обратном коде: 110000 - состояние,
// 110016 - данные
static inline unsigned int address_to_reg(unsigned int address)
{
    return (~(address >> 1)) & 7;
}

unsigned int UKNCHDD::get_value_word(unsigned int address)
{
    // Пустое гнездо: на магистрали никто не отвечает, и линии стоят в нуле
    if (!m_attached) return 0;
    // Магистраль 1801 несёт данные в обратном коде, и плата их не переворачивает
    return (~read_port(address_to_reg(address))) & 0xFFFF;
}

// Отладчик и LOG видят слово буфера, не продвигаясь по нему
unsigned UKNCHDD::get_direct(unsigned address)
{
    if (!m_attached) return 0;
    const unsigned int w = (~read_port(address_to_reg(address & ~1u), true)) & 0xFFFF;
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void UKNCHDD::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (!m_attached) return;
    set_port(address_to_reg(address), (~value) & 0xFFFF);
}

unsigned int UKNCHDD::get_value(unsigned int address)
{
    // Байтовое чтение регистра данных забирает целое слово, как и у UKNCBTL:
    // накопитель отдаёт буфер словами, половинки у него нет
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void UKNCHDD::set_value(unsigned int address, unsigned int value, bool force)
{
    // Байт становится словом со своей половины, как и в UKNCBTL: на шине
    // 1801 обмен всё равно идёт словами, а половину выбирает разряд 0 адреса
    const unsigned int w = (address & 1)? ((value & 0xFF) << 8) : (value & 0xFF);
    set_value_word(address & ~1u, w, force);
}

//--------------------------- Поля и команды --------------------------------//

ConfigFields UKNCHDD::get_config_fields()
{
    //A link only: the image is written through as the machine works with it,
    //and a copy unpacked from the configuration would lose every change
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

void UKNCHDD::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);

    w.u("status", m_status, 8);
    w.u("error", m_error, 8);
    w.u("command", m_command, 8);
    w.n("sectorcount", m_sectorcount);
    w.n("cursector", m_cursector);
    w.n("curcylinder", m_curcylinder);
    w.n("curhead", m_curhead);
    w.n("curheadreg", m_curheadreg);
    w.n("bufferoffset", m_bufferoffset);
    w.hex("buffer", m_buffer, sizeof(m_buffer));
    w.n("event", m_event);
    w.n64("timeout", m_timeout);
    w.n64("acc", m_acc);
    w.n("sectors_read", m_sectors_read);
    w.n("sectors_written", m_sectors_written);
    w.b("write_protect", m_write_protect);

    if (!m_attached) return;
    w.b("attached", true);
    w.b("inverted", m_inverted);
    w.b("volatile", m_volatile);
    w.n("cylinders", m_cylinders);
    w.n("heads", m_heads);
    w.n("sectors", m_sectors);

    //The image with the written sectors already merged in. Simpler than
    //carrying a sparse map, and byte for byte what the guest sees - which is
    //the point: the file on disk never had those writes
    compat_lock_guard lock(m_image_mutex);
    if (m_file == nullptr || m_image_size == 0) return;
    std::vector<uint8_t> image(static_cast<size_t>(m_image_size), 0);
    std::fseek(m_file, 0, SEEK_SET);
    if (std::fread(image.data(), 1, image.size(), m_file) != image.size()) return;
    for (std::map<uint64_t, std::array<uint8_t, 512>>::const_iterator it = m_overlay.begin();
         it != m_overlay.end(); ++it)
        if (it->first + 512 <= image.size())
            memcpy(image.data() + it->first, it->second.data(), 512);
    w.blob("image", dsk_tools::get_filename(m_file_name.empty() ? (name + ".img") : m_file_name),
           image.data(), image.size());
}

emulator::Result UKNCHDD::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;

    r.u("status", m_status);
    r.u("error", m_error);
    r.u("command", m_command);
    r.u("sectorcount", m_sectorcount);
    r.u("cursector", m_cursector);
    r.u("curcylinder", m_curcylinder);
    r.u("curhead", m_curhead);
    r.u("curheadreg", m_curheadreg);
    r.u("bufferoffset", m_bufferoffset);
    r.hex("buffer", m_buffer, sizeof(m_buffer));
    r.u("event", m_event);
    r.n64("timeout", m_timeout);
    r.n64("acc", m_acc);
    r.u("sectors_read", m_sectors_read);
    r.u("sectors_written", m_sectors_written);
    r.b("write_protect", m_write_protect);

    bool attached = false;
    if (!r.b("attached", attached) || !attached) return emulator::Result::ok();

    //The state names the copy in its own bundle, and attach_image() opens it
    //the way the configuration would have
    std::string file;
    if (!r.s("image", file) || file.empty()) return emulator::Result::ok();
    const std::string path = find_file_location(sd, file);
    if (path.empty())
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{MachineState|Saved state} " + name + ": file not found: " + file);

    r.b("inverted", m_inverted);
    r.b("volatile", m_volatile);
    load_image(path);
    r.u("cylinders", m_cylinders);
    r.u("heads", m_heads);
    r.u("sectors", m_sectors);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> UKNCHDD::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"attached",  "1, когда образ вставлен",                  false});
    r.push_back({"file",      "Имя образа",                               false});
    r.push_back({"inverted",  "1, когда образ снят в обратном коде",       false});
    r.push_back({"lba",       "1, когда адрес задан линейно, а не CHS",     false});
    r.push_back({"protected", "1, когда запись в образ запрещена",         false});
    r.push_back({"volatile",  "1, когда запись идёт в память, а не в файл", false});
    r.push_back({"geometry",  "Цилиндры, головки и секторы образа",        false});
    r.push_back({"cylinders", "Число цилиндров",                          false});
    r.push_back({"heads",     "Число головок",                            false});
    r.push_back({"sectors",   "Число секторов на дорожке",                 false});
    r.push_back({"status",    "Регистр состояния накопителя",              false});
    r.push_back({"error",     "Регистр ошибки",                           false});
    r.push_back({"command",   "Последняя команда",                        false});
    r.push_back({"cylinder",  "Текущий цилиндр",                          false});
    r.push_back({"head",      "Текущая головка",                          false});
    r.push_back({"sector",    "Текущий сектор",                           false});
    r.push_back({"reads",     "Сколько секторов прочитано",                false});
    r.push_back({"writes",    "Сколько секторов записано",                 false});
    return r;
}

std::vector<DeviceCommandInfo> UKNCHDD::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"load",    "\"file\"", "Вставляет образ винчестера"});
    r.push_back({"eject",   "",         "Вынимает образ"});
    r.push_back({"protect", "[0|1]",    "Запрещает или разрешает запись в образ"});
    r.push_back({"volatile", "[0|1]",   "Запись в память вместо файла; выключение забывает записанное"});
    return r;
}

bool UKNCHDD::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "file") {
        out.text = m_file_name;
        return true;
    }
    if (field == "geometry") {
        out.text = std::to_string(m_cylinders) + "/" + std::to_string(m_heads) + "/" + std::to_string(m_sectors);
        return true;
    }

    out.numeric = true;
    out.width = 16;
    if (field == "attached")  { out.values.push_back(m_attached? 1 : 0);  return true; }
    if (field == "inverted")  { out.values.push_back(m_inverted? 1 : 0);  return true; }
    if (field == "lba")       { out.values.push_back(lba_mode()? 1 : 0);  return true; }
    if (field == "protected") { out.values.push_back((m_read_only || m_write_protect)? 1 : 0); return true; }
    if (field == "volatile")  { out.values.push_back(m_volatile? 1 : 0);  return true; }
    if (field == "cylinders") { out.values.push_back(m_cylinders);        return true; }
    if (field == "heads")     { out.values.push_back(m_heads);            return true; }
    if (field == "sectors")   { out.values.push_back(m_sectors);          return true; }
    if (field == "status")    { out.values.push_back(m_status);           return true; }
    if (field == "error")     { out.values.push_back(m_error);            return true; }
    if (field == "command")   { out.values.push_back(m_command);          return true; }
    if (field == "cylinder")  { out.values.push_back(m_curcylinder);      return true; }
    if (field == "head")      { out.values.push_back(m_curhead);          return true; }
    if (field == "sector")    { out.values.push_back(m_cursector);        return true; }
    out.width = 32;
    if (field == "reads")     { out.values.push_back(m_sectors_read);     return true; }
    if (field == "writes")    { out.values.push_back(m_sectors_written);  return true; }

    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

emulator::Result UKNCHDD::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "load") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{UKNCHDD|" + std::string(QT_TRANSLATE_NOOP("UKNCHDD", "Command 'load' expects a file name")) + "}");
        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];
        return load_image(file);
    }

    if (command == "eject") {
        unload();
        return emulator::Result::ok();
    }

    if (command == "protect") {
        if (p.empty() || p[0].empty())
            m_write_protect = !m_write_protect;
        else
            m_write_protect = (parse_numeric_value(p[0], 10) != 0);
        return emulator::Result::ok();
    }

    if (command == "volatile") {
        if (p.empty() || p[0].empty())
            m_volatile = !m_volatile;
        else
            m_volatile = (parse_numeric_value(p[0], 10) != 0);
        if (!m_volatile) {
            compat_lock_guard lock(m_image_mutex);
            m_overlay.clear();
        }
        return emulator::Result::ok();
    }

    return AddressableDevice::send_command(command, parameters);
}

ComputerDevice * create_uknc_hdd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCHDD(im, cd);
}
