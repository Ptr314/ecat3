// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the AZ disk controller

#include <algorithm>
#include <map>
#include <climits>
#include <cstdio>
#include <cstring>

#include "azbk_disk.h"
#include "azbk.h"
#include "azbk_vga.h"
#include "emulator/devices/common/hdd_unit.h"
#include "emulator/host_dir.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2

static const unsigned int R_CSR   = 0177220;
static const unsigned int R_DR    = 0177222;
static const unsigned int R_BOOT1 = 0177224;
static const unsigned int R_BOOT2 = 0177226;
static const unsigned int VECTOR  = 0174;

static const unsigned int CSR_IE   = 0100;
static const unsigned int CSR_DONE = 0200;
static const unsigned int CSR_BIG  = 040000;
static const unsigned int CSR_ERR  = 0100000;

// Слова ошибки прошивки (DR вне потока)
static const uint16_t E_UNIT      = 0x8600;     // нет такого привода, или в строке не "Dnn"
static const uint16_t E_EMPTY     = 0x8C00;     // привод пуст
static const uint16_t E_BLOCK     = 0x8D00;     // блок за концом образа
static const uint16_t E_NODIR     = 0x8B00;     // 013 без открытого оглавления
static const uint16_t E_NO016     = 0x8100;     // 003 без потока 016
static const uint16_t E_DIR       = 0x8200;     // 003, 013: | код FatFs
static const uint16_t E_NOT_D     = 0x8500;     // 004, 014: строка не с 'D'
static const uint16_t E_NO_EQ     = 0x8700;     // 004: нет '='
static const uint16_t E_LONG      = 0x8800;     // 004: путь от 384 знаков
static const uint16_t E_OPEN      = 0x8900;     // 004: привод занят
static const uint16_t E_STAT      = 0x8A00;     // 004: файла нет
static const uint16_t E_FOPEN     = 0x8B00;     // 004: | код FatFs открытия
static const uint16_t E_RANGE     = 0xB600;     // 004, 014: привод от 32
static const uint16_t E_NOT_OPEN  = 0xBC00;     // 014: привод пуст

// Время от начала эпохи до даты по григорианскому календарю, дни
static int64_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int &y, int &m, int &d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400) + (m <= 2 ? 1 : 0);
}

// Знак строки монтирования, как его берет прошивка: малые буквы - большими,
// \ - /
static char path_char(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return (c == '\\') ? '/' : c;
}

// MAC платы, когда AZ.INI его не задает (прошивка 00019)
static const uint8_t DEFAULT_MAC[6] = { 0x00, 0x0E, 0x35, 0x22, 0x0B, 0x01 };

AZBKDisk::AZBKDisk(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd),
    i_virq(this, im, 1, "virq", MODE_W),
    i_vector(this, im, 16, "vector", MODE_W),
    i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN),
    i_vector_in(this, im, 16, "vector_in", MODE_R, CALLBACK_CHAIN),
    m_irq(i_virq, i_vector),
    i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO),
    i_dclo(this, im, 1, "dclo", MODE_W)
{
    addresable_size = 0200000;
    can_read = true;
    can_write = true;
    m_clocked = true;
    i_virq.change(1);
    i_virq_in.value = 1;
    i_dclo.change(1);
    memcpy(m_mac, DEFAULT_MAC, sizeof(m_mac));
}

emulator::Result AZBKDisk::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    const std::string bad = "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name;

    m_board = dynamic_cast<AZBK*>(im->dm->get_device_by_name(cd->get_parameter("board").value));
    if (m_board == nullptr) return emulator::Result::error(emulator::ErrorCode::ConfigError, bad);

    const std::vector<std::string> units = split_string(cd->get_parameter("units", false).value, '|', true);
    if (units.size() > UNITS) return emulator::Result::error(emulator::ErrorCode::ConfigError, bad);
    for (unsigned int i = 0; i < units.size(); i++) {
        const std::string n = str_trim(units[i]);
        if (n.empty()) continue;
        m_units[i] = dynamic_cast<HddUnit*>(im->dm->get_device_by_name(n));
        if (m_units[i] == nullptr) return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (" + n + ")");
    }

    m_stm_version = read_confg_value(cd, "stm_version", true, 0u) & 0377;
    m_block_us = read_confg_value(cd, "block_us", false, 650u);
    m_command_us = read_confg_value(cd, "command_us", false, 20u);
    m_restart_ms = read_confg_value(cd, "restart_ms", false, 100u);

    int y = 2026, mo = 1, d = 1, h = 0, mi = 0, s = 0;
    const std::string start = str_trim(cd->get_parameter("rtc_start", false).value);
    if (!start.empty() && std::sscanf(start.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3)
        return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (rtc_start)");
    m_rtc_start = (uint64_t)(days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s);

    const std::string eeprom = str_trim(cd->get_parameter("eeprom", false).value);
    if (!eeprom.empty()) {
        const std::string file = find_file_location(sd, eeprom);
        const std::string data = file.empty() ? std::string() : dsk_tools::utf8_read_file(file);
        if (data.size() < EEPROM_WORDS * 2)
            return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (eeprom)");
        for (unsigned int i = 0; i < EEPROM_WORDS; i++)
            m_eeprom[i] = (uint16_t)((uint8_t)data[i * 2] | ((uint8_t)data[i * 2 + 1] << 8));
        m_eeprom_valid = true;
    }

    // Гнездо карты microSD: оно сообщает, когда карту сменили
    const std::string sd_name = str_trim(cd->get_parameter("card", false).value);
    if (!sd_name.empty()) {
        m_sd = dynamic_cast<AZBKSdCard*>(im->dm->get_device_by_name(sd_name, false));
        if (m_sd == nullptr) return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (card)");
        m_sd->set_listener(this);
    }

    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));
    return emulator::Result::ok();
}

// Сброс самого контроллера, как после перезапуска микроконтроллера: команды и
// автоматы регистров
void AZBKDisk::reset_controller()
{
    m_done = true;
    m_err = false;
    m_big = false;
    m_ie = false;
    m_network = false;
    m_busy = false;
    m_busy_us = 0;
    m_us_acc = 0;
    m_unit = 0;
    m_block = 0;
    m_block_low = false;
    m_errword = 0;
    m_last_out = 0;
    m_buf = BUF_IOBUF;
    m_buf_len = 0;
    clear_regs();
    m_irq_pending = false;
    m_dir_open = false;
    m_dir_valid = false;
    m_dir.clear();
    m_file = CardFile();
    set_length(0);
    update_irq();
}

// Память AZ, которую трогает старт STM32
void AZBKDisk::fill_words(uint32_t word, uint32_t count, uint16_t value)
{
    uint8_t * mem = m_board->memory();
    const uint64_t size = m_board->memory_size();
    if (mem == nullptr || ((uint64_t)word + count) * 2 > size) return;
    uint8_t * p = mem + (uint64_t)word * 2;
    for (uint32_t i = 0; i < count; i++) {
        p[i * 2] = (uint8_t)value;
        p[i * 2 + 1] = (uint8_t)(value >> 8);
    }
    const uint64_t first = (uint64_t)word * 2 / AZBK::PAGE_BYTES;
    const uint64_t last = ((uint64_t)word + count) * 2 / AZBK::PAGE_BYTES;
    for (uint64_t pg = first; pg < last; pg++) m_board->touch_page((unsigned int)pg);
}

// Старт микроконтроллера (включение и 037), как у прошивки 00019: тест памяти AZ
// - 64 К слов 0x1111 (страницы 01000-01037) и 64 К нулей (01100-01137), нули в
// теневых копиях экранных страниц БК 5 и 6 (страницы AZ 004-007 и 034-037);
// остальную память AZ STM32 не трогает. Затем каталоги 0:/TEMP и 0:/SCREENS
// на карте и диски из AZ.INI
void AZBKDisk::stm_start()
{
    fill_words(0x100000, 0x10000, 0x1111);
    fill_words(0x120000, 0x10000, 0);
    fill_words(0x2000, 0x2000, 0);
    fill_words(0xE000, 0x2000, 0);
    memset(m_iobuf, 0, sizeof(m_iobuf));
    memset(m_cmos, 0, sizeof(m_cmos));
    memset(m_timestamp, 0, sizeof(m_timestamp));
    memset(m_simple_in, 0, sizeof(m_simple_in));
    memset(m_ipdata, 0, sizeof(m_ipdata));
    memset(m_dirrec, 0, sizeof(m_dirrec));
    memset(m_sizecard, 0, sizeof(m_sizecard));
    m_status = 0;
    if (card().present()) {
        card().make_dir("TEMP");
        card().make_dir("SCREENS");
    }
    apply_az_ini();
}

void AZBKDisk::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        m_clocks = 0;
        m_rtc_shift = 0;
        m_restart_left = 0;
        i_dclo.change(1);
        m_commands = m_refused = 0;
        m_trace.clear();
    }
    m_irq.clear();
    reset_controller();
    if (cold) stm_start();
}

//------------------------------------------------------------- время ---

uint64_t AZBKDisk::now_seconds() const
{
    const uint64_t elapsed = (m_system_clock != 0) ? m_clocks / m_system_clock : 0;
    return (uint64_t)((int64_t)(m_rtc_start + elapsed) + m_rtc_shift);
}

// Буфер timestamp (вики MAXIOL): дата RT-11, время RT-11 в тиках 50 и 60 Гц,
// дата и время FAT, затем год, месяц, день, день недели, часы, минуты, секунды
void AZBKDisk::fill_timestamp(uint64_t seconds)
{
    const int64_t days = (int64_t)(seconds / 86400);
    const unsigned int sod = (unsigned int)(seconds % 86400);
    int y, mo, d;
    civil_from_days(days, y, mo, d);
    const unsigned int h = sod / 3600, mi = (sod / 60) % 60, s = sod % 60;
    const int age = y - 1972;
    const unsigned int rt11 = (age >= 0 && age < 128)
        ? (unsigned int)((age & 037) | (d << 5) | (mo << 10) | ((age >> 5) << 14)) : 0;
    const uint32_t t50 = sod * 50u, t60 = sod * 60u;
    m_timestamp[0] = (uint16_t)rt11;
    m_timestamp[1] = (uint16_t)(t50 >> 16);
    m_timestamp[2] = (uint16_t)t50;
    m_timestamp[3] = (uint16_t)(t60 >> 16);
    m_timestamp[4] = (uint16_t)t60;
    m_timestamp[5] = (uint16_t)(y >= 1980 ? (((y - 1980) << 9) | (mo << 5) | d) : 0);
    m_timestamp[6] = (uint16_t)((h << 11) | (mi << 5) | (s / 2));
    m_timestamp[7] = (uint16_t)y;
    m_timestamp[8] = (uint16_t)mo;
    m_timestamp[9] = (uint16_t)d;
    // День недели: 1 - понедельник, как во входном буфере SimpleIN (и у
    // часов STM32); 1970-01-01 - четверг
    m_timestamp[10] = (uint16_t)(((days % 7 + 7 + 3) % 7) + 1);
    m_timestamp[11] = (uint16_t)h;
    m_timestamp[12] = (uint16_t)mi;
    m_timestamp[13] = (uint16_t)s;
}

void AZBKDisk::clock(unsigned int counter)
{
    m_clocks += counter;
    if (m_restart_left > 0) {
        if (m_restart_left > counter) m_restart_left -= counter;
        else {
            // Плата отпускает БК: процессор начинает с адреса из 177716
            m_restart_left = 0;
            i_dclo.change(1);
        }
    }
    if (!m_busy || m_system_clock == 0) return;
    m_us_acc += (uint64_t)counter * 1000000ull;
    const uint64_t us = m_us_acc / m_system_clock;
    m_us_acc -= us * m_system_clock;
    if (us >= m_busy_us) {
        m_busy_us = 0;
        finish_long();
    } else
        m_busy_us -= us;
}

unsigned int AZBKDisk::clocks_to_event() const
{
    uint64_t c = UINT_MAX;
    if (m_restart_left > 0) c = std::min<uint64_t>(c, m_restart_left);
    if (m_busy && m_system_clock != 0) {
        const uint64_t need = m_busy_us * m_system_clock;
        const uint64_t clocks = (need > m_us_acc) ? (need - m_us_acc + 999999) / 1000000 : 1;
        c = std::min<uint64_t>(c, std::max<uint64_t>(clocks, 1));
    }
    return (unsigned int)c;
}

//------------------------------------------------------- прерывание ---

// Клавиатура БК ближе к процессору: на схеме БК0011М IAKO процессора (ППР1
// разъема МПИ) идет на IAKI К1801ВП1-014, а ее IAKO - на ППР2, после
// клавиатуры. Плата в разъеме, которая берет ППР1, отвечала бы на одно
// подтверждение вместе с клавиатурой, поэтому ее запрос обслуживается раньше
// своего (какой контакт берет AZBK - вопрос Б.11)
void AZBKDisk::update_irq()
{
    const unsigned int ahead = ((i_virq_in.value & 1) == 0) ? (i_vector_in.value & 0xFFFF) : 0;
    m_irq.offer(ahead != 0 ? ahead : (m_irq_pending ? VECTOR : 0));
}

void AZBKDisk::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    (void)old_value;
    if (callback_id == CALLBACK_IAKO) {
        if ((new_value & 0xFFFF) == VECTOR) m_irq_pending = false;
        update_irq();
    } else if (callback_id == CALLBACK_CHAIN) {
        update_irq();
    }
}

//----------------------------------------------------------- команды ---

HddUnit * AZBKDisk::unit() const
{
    if (m_unit >= UNITS) return nullptr;
    HddUnit * u = m_units[m_unit];
    return (u != nullptr && u->attached()) ? u : nullptr;
}

// Выбранный привод смонтирован; иначе слово ошибки 0x8C00 и ошибка
bool AZBKDisk::mounted()
{
    if (unit() != nullptr) return true;
    m_errword = E_EMPTY;
    m_err = true;
    return false;
}

unsigned int AZBKDisk::csr() const
{
    if (!m_done) return 0;
    return CSR_DONE | (m_err ? CSR_ERR : 0) | (m_big ? CSR_BIG : 0);
}

// Оба автомата DR - в покой, регистры загрузки - сначала (прошивка делает это
// сбросом и большинством «длительных» команд)
void AZBKDisk::clear_regs()
{
    m_out_mode = OUT_IDLE;
    m_out.clear();
    m_out_pos = 0;
    m_in = nullptr;
    m_in_target = IN_NONE;
    m_in_size = 0;
    m_in_pos = 0;
    m_boot1 = 0;
    m_boot2 = 0;
}

void AZBKDisk::stream_out(const uint16_t * data, unsigned int count, unsigned int mode)
{
    m_out.assign(data, data + count);
    m_out_pos = 0;
    m_out_mode = mode;
}

void AZBKDisk::stream_in(unsigned int target, uint16_t * data, unsigned int count)
{
    m_in = data;
    m_in_size = count;
    m_in_pos = 0;
    m_in_target = target;
}

// Оставшиеся слова потока записи - нули (003 и 006 так дополняют строку и блок)
void AZBKDisk::zero_rest_of_input()
{
    if (m_in == nullptr) return;
    for (unsigned int i = m_in_pos; i < m_in_size; i++) m_in[i] = 0;
    m_in_pos = std::max(m_in_pos, m_in_size);
}

// Таблица назначений (011), как в памяти прошивки: 32 записи по 198 слов -
// размер в блоках (2 слова), дата и время FAT файла, флаги (1 - назначен,
// 4 - открыт) и атрибуты, имя 386 байт
void AZBKDisk::build_table(std::vector<uint16_t> &out) const
{
    out.assign(UNITS * ENTRY_WORDS, 0);
    for (unsigned int i = 0; i < UNITS; i++) {
        const HddUnit * u = m_units[i];
        if (u == nullptr || !u->attached()) continue;
        uint16_t * e = &out[i * ENTRY_WORDS];
        const uint64_t blocks = const_cast<HddUnit*>(u)->blocks();
        e[0] = (uint16_t)blocks;
        e[1] = (uint16_t)(blocks >> 16);
        e[4] = 5 | (040 << 8);
        const std::string &file = const_cast<HddUnit*>(u)->hdd_image().file_name();
        // Смонтированный с карты - той строкой, какой его монтировали, пока в
        // приводе тот же образ (из снимка состояния он приходит копией под тем
        // же именем)
        const std::string n = (!m_mount[i].empty()
                               && str_tolower(dsk_tools::get_filename(m_mount_file[i])) == str_tolower(dsk_tools::get_filename(file)))
                            ? m_mount[i] : "0:/" + dsk_tools::get_filename(file);
        uint8_t * name = reinterpret_cast<uint8_t*>(e + 5);
        for (size_t k = 0; k < n.size() && k + 1 < NAME_BYTES; k++) name[k] = (uint8_t)n[k];
    }
}

// 015: буфер последней выбравшей его команды. Длина больше буфера (023 после
// 011) - за ним нули: прошивка читала бы дальше свою память
void AZBKDisk::stream_buffer()
{
    std::vector<uint16_t> data(m_buf_len, 0);
    std::vector<uint16_t> table;
    const uint16_t * src = nullptr;
    unsigned int have = 0;
    switch (m_buf) {
    case BUF_IOBUF:  src = m_iobuf; have = 256; break;
    case BUF_TABLE:  build_table(table); src = table.data(); have = (unsigned int)table.size(); break;
    case BUF_DIRREC: src = m_dirrec; have = DIRREC_WORDS; break;
    case BUF_CMOS:   src = m_cmos; have = 256; break;
    case BUF_SIMPLE: src = m_simple_in; have = 7; break;
    case BUF_LENGTH: src = m_length; have = 2; break;
    default: break;
    }
    for (unsigned int i = 0; i < m_buf_len && i < have; i++) data[i] = src[i];
    stream_out(data.data(), m_buf_len, OUT_STREAM);
}

void AZBKDisk::start_long(unsigned int cmd, uint64_t us)
{
    m_busy_cmd = cmd;
    m_busy = true;
    m_busy_us = std::max<uint64_t>(us, 1);
    m_us_acc = 0;
    m_done = false;
}

// Готово: «длительная» команда дает прерывание, если оно разрешено
void AZBKDisk::finish()
{
    m_done = true;
    if (m_ie) {
        m_irq_pending = true;
        update_irq();
    }
}

void AZBKDisk::command(unsigned int value)
{
    const unsigned int cmd = value & 077;
    m_ie = (value & CSR_IE) != 0;

    // Занят: принимается только 030 (разрешение прерывания); сброс прерывает
    // обслуживание сети (на 10-20 мкс), но не обмен с картой
    if (!m_done) {
        if (cmd == 030) return;
        if (cmd == 0 && m_network) {
            m_network = false;
            clear_regs();
            m_err = false;
            m_big = false;
            m_busy_cmd = 0;
            m_busy = true;
            m_busy_us = 15;
            m_us_acc = 0;
        }
        return;
    }

    m_commands++;
    if (m_trace.size() >= 64) m_trace.erase(m_trace.begin());
    m_trace.push_back((uint16_t)(value & 0177));
    m_command = cmd;
    m_err = false;
    m_big = false;

    HddUnit * u;
    uint16_t tmp[3];
    switch (cmd) {
    case 000:   // сброс: оглавление, файл и буферы остаются
        clear_regs();
        break;
    case 001:   // выбор устройства, мгновенно; привод выбран, даже пустой
        m_unit = m_arg;
        if (m_unit >= UNITS) {
            m_errword = E_UNIT;
            m_err = true;
        } else if (unit() == nullptr) {
            m_errword = E_EMPTY;
            m_err = true;
        }
        break;
    case 002:   // младшие биты номера блока, старшие очищаются; блок за концом
                // образа - ошибка (номер, равный размеру, проходит)
        if (!mounted()) break;
        m_block = m_arg & 0177777;
        if (m_block > unit()->blocks()) {
            m_errword = E_BLOCK;
            m_err = true;
        } else
            m_block_low = true;
        break;
    case 012:   // старшие биты, только после 002
        if (!mounted()) break;
        if (!m_block_low) {
            m_err = true;
            break;
        }
        m_block |= (uint32_t)(m_arg & 0177777) << 16;
        if (m_block > unit()->blocks()) {
            m_errword = E_BLOCK;
            m_err = true;
        } else
            m_block_low = false;
        break;
    case 005:
    case 006:
        if (!mounted()) break;
        m_block_low = false;
        if (cmd == 005) {
            m_buf = BUF_IOBUF;
            m_buf_len = 256;
        }
        start_long(cmd, m_block_us);
        return;
    case 007:   // размер: от 65536 блоков - 177777 и BIG
    case 017: { // размер, два слова
        u = unit();
        const uint64_t b = (u != nullptr) ? u->blocks() : 0;
        if (cmd == 007) {
            tmp[0] = (uint16_t)std::min<uint64_t>(b, 0177777);
            m_big = b >= 0x10000;
            stream_out(tmp, 1, OUT_SEQ);
        } else {
            tmp[0] = (uint16_t)b;
            tmp[1] = (uint16_t)(b >> 16);
            stream_out(tmp, 2, OUT_SEQ);
        }
        break;
    }
    case 010:   // с IE - обслуживание сети до сброса, без прерывания
        clear_regs();
        if (m_ie) {
            m_network = true;
            m_done = false;
            m_busy = false;
        }
        return;
    case 011:   // таблица назначений - ее и отдаст 015
        m_buf = BUF_TABLE;
        m_buf_len = UNITS * ENTRY_WORDS;
        break;
    case 015:   // буфер через DR
        stream_buffer();
        break;
    case 016:   // прием блока в буфер
        stream_in(IN_IOBUF, m_iobuf, 256);
        m_buf = BUF_IOBUF;
        break;
    case 020:   // расширенная диагностика - итог старта: два слова
        tmp[0] = tmp[1] = 0;
        stream_out(tmp, 2, OUT_SEQ);
        break;
    case 022:   // EEPROM через DR: слово состояния и 255 слов
        stream_out(m_cmos, 256, OUT_STREAM);
        break;
    case 023:   // в буфер с начала: память для 024, имя или данные файла
        stream_in(IN_CMOS, m_cmos, 256);
        m_buf = BUF_CMOS;
        break;
    case 027:   // версия: прошивка STM32 и плата (4 - AZБК), старший диск, дальше нули
        tmp[0] = (uint16_t)((m_stm_version << 8) | 4);
        tmp[1] = 037;
        tmp[2] = 0;
        stream_out(tmp, 3, OUT_STICKY);
        break;
    case 030:
        break;
    case 032:
        stream_out(m_timestamp, 14, OUT_STREAM);
        break;
    case 033:
        stream_in(IN_SIMPLE, m_simple_in, 7);
        m_buf = BUF_SIMPLE;
        break;
    case 037:   // перезапуск платы, БК и самого контроллера
        m_board->restart();
        reset_controller();
        stm_start();
        m_restart_left = (uint64_t)m_restart_ms * m_system_clock / 1000;
        if (m_restart_left == 0) m_restart_left = 1;
        i_dclo.change(0);
        return;
    case 041:
        stream_out(m_ipdata, 12, OUT_STREAM);
        break;
    case 021: case 024: case 025: case 026: case 031: case 034: case 035: case 036:
    case 040: case 042: case 043:
        clear_regs();
        start_long(cmd, m_command_us);
        return;
    case 003: case 004: case 013: case 014: case 044: case 047:
    case 050: case 051: case 052: case 053: case 054: case 055: case 056: case 057:
        if (card_command(cmd)) return;
        break;
    default:    // 045, 046
        note_unsupported("command " + oct_str(cmd, 3));
        m_err = true;
        break;
    }
}

// Конец «длительной» команды
void AZBKDisk::finish_long()
{
    const unsigned int cmd = m_busy_cmd;
    m_busy = false;
    if (cmd == 0) {             // сброс после обслуживания сети
        m_done = true;
        return;
    }
    HddUnit * u = unit();
    uint8_t block[512];
    switch (cmd) {
    case 005:
        // Блок, равный размеру образа, прошивка читает за концом файла: ничего
        if (u == nullptr || m_block >= u->blocks()) break;
        if (!u->read_block(m_block, block)) {
            // Прошивка на ошибке карты останавливается (Fatal Error #6, #7)
            m_err = true;
            break;
        }
        for (unsigned int i = 0; i < 256; i++) m_iobuf[i] = (uint16_t)(block[i * 2] | (block[i * 2 + 1] << 8));
        break;
    case 006:
        // Без потока 016 прошивка останавливается (Fatal Error #8). Неполный
        // буфер дополняется нулями. Блок, равный размеру, на плате дописывает
        // файл; здесь образ не растет
        if (m_in_target == IN_NONE) {
            m_err = true;
        } else {
            zero_rest_of_input();
            if (u != nullptr && m_block < u->blocks()) {
                for (unsigned int i = 0; i < 256; i++) {
                    block[i * 2] = (uint8_t)m_iobuf[i];
                    block[i * 2 + 1] = (uint8_t)(m_iobuf[i] >> 8);
                }
                if (u->is_protected() || !u->write_block(m_block, block)) m_err = true;
            }
        }
        clear_regs();
        break;
    case 021:   // буфер - нули, слово состояния: 0 - прочитано, 1 - память не записана
        memset(m_cmos, 0, sizeof(m_cmos));
        m_cmos[0] = m_eeprom_valid ? 0 : 1;
        if (m_eeprom_valid) memcpy(m_cmos + 1, m_eeprom, sizeof(m_eeprom));
        break;
    case 024:   // в память - первые 255 слов буфера
        memcpy(m_eeprom, m_cmos, sizeof(m_eeprom));
        m_eeprom_valid = true;
        break;
    case 025:   // Hall of Fame: сети нет - ответ сервера об ошибке связи
    case 026:
        put_cmos_string("{\"RESULT\":\"ERROR\",\"DESCRIPTION\":\"CONNECTION_ERROR\"}");
        break;
    case 031:
        fill_timestamp(now_seconds());
        break;
    case 034: {
        const int y = 2000 + (m_simple_in[0] % 100);
        const uint64_t t = (uint64_t)(days_from_civil(y, m_simple_in[1], m_simple_in[2]) * 86400
                         + m_simple_in[4] * 3600 + m_simple_in[5] * 60 + m_simple_in[6]);
        const uint64_t elapsed = (m_system_clock != 0) ? m_clocks / m_system_clock : 0;
        m_rtc_shift = (int64_t)t - (int64_t)(m_rtc_start + elapsed);
        break;
    }
    case 040:   // адреса стека TCP/IP: DHCP без сети не ответил - нули
        memset(m_ipdata, 0, sizeof(m_ipdata));
        break;
    case 043:   // MAC - в первые три слова буфера адресов
        m_ipdata[0] = (uint16_t)(m_mac[0] | (m_mac[1] << 8));
        m_ipdata[1] = (uint16_t)(m_mac[2] | (m_mac[3] << 8));
        m_ipdata[2] = (uint16_t)(m_mac[4] | (m_mac[5] << 8));
        break;
    case 042:   // часы стека TCP/IP: без NTP на плате они идут от 1970 года
                // с ее старта; здесь - нули
        memset(m_timestamp, 0, sizeof(m_timestamp));
        break;
    case 003: open_dir();         break;
    case 013: read_dir();         break;
    case 004: mount();            break;
    case 014: unmount();          break;
    case 050: open_read();        break;
    case 052: read_file_block();  break;
    case 053: open_write();       break;
    case 055: write_file_block(); break;
    case 047: read_into_memory(); break;
    case 044: screenshot();       break;
    case 056: {
        unsigned int total = 0, free = 0;
        card().sizes(total, free);
        m_sizecard[0] = (uint16_t)total;
        m_sizecard[1] = (uint16_t)free;
        break;
    }
    default:
        break;
    }
    finish();
}

//------------------------------------------------------------ карта microSD ---

// Строка в буфере слов: байты по порядку, до нуля
std::string AZBKDisk::buffer_string(const uint16_t * buffer, unsigned int max_bytes) const
{
    std::string s;
    for (unsigned int i = 0; i < max_bytes; i++) {
        const char c = (char)((i & 1) ? (buffer[i / 2] >> 8) : (buffer[i / 2] & 0xFF));
        if (c == 0) break;
        s += c;
    }
    return s;
}

// Строка в буфер файловых команд, с нулем; остальное - нули
void AZBKDisk::put_cmos_string(const std::string &s)
{
    memset(m_cmos, 0, sizeof(m_cmos));
    for (size_t i = 0; i < s.size() && i < sizeof(m_cmos) - 1; i++)
        m_cmos[i / 2] |= (uint16_t)((uint8_t)s[i] << ((i & 1) ? 8 : 0));
}

void AZBKDisk::fat_now(uint16_t &date, uint16_t &time) const
{
    const uint64_t s = now_seconds();
    int y, mo, d;
    civil_from_days((int64_t)(s / 86400), y, mo, d);
    const unsigned int sod = (unsigned int)(s % 86400);
    date = (uint16_t)((y >= 1980 && y < 2108) ? (((y - 1980) << 9) | (mo << 5) | d) : 0);
    time = (uint16_t)(((sod / 3600) << 11) | (((sod / 60) % 60) << 5) | ((sod % 60) / 2));
}

// Разделители подряд - один (FatFs их так и понимает): "//" в значении
// снимка состояния читалось бы как начало комментария
static std::string collapse_separators(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        const bool sep = (s[i] == '/' || s[i] == '\\');
        if (sep && !out.empty() && (out[out.size() - 1] == '/' || out[out.size() - 1] == '\\')) continue;
        out += s[i];
    }
    return out;
}

// Команда карты. true - пошла «длительная», ее окончание - finish_long()
bool AZBKDisk::card_command(unsigned int cmd)
{
    if (!card().present()) {
        m_err = true;
        return false;
    }
    switch (cmd) {
    case 013:       // оглавление не открыто - ошибка сразу
        if (!m_dir_open) {
            m_errword = E_NODIR;
            m_err = true;
            return false;
        }
        m_buf = BUF_DIRREC;
        m_buf_len = DIRREC_WORDS;
        start_long(cmd, m_command_us);
        return true;
    case 051:       // длина файла: на чтение (050), 0 (053, ошибка открытия) или 054
        stream_out(m_length, 2, OUT_STREAM);
        return false;
    case 054:       // длина файла на запись: два слова через DR
        stream_in(IN_LENGTH, m_length, 2);
        m_buf = BUF_LENGTH;
        return false;
    case 057:
        stream_out(m_sizecard, 2, OUT_STREAM);
        return false;
    case 003:
    case 004:
    case 014:       // эти автоматы DR не сбрасывают
        start_long(cmd, m_command_us);
        return true;
    default:
        break;
    }
    clear_regs();
    switch (cmd) {
    case 052:
    case 055:
        start_long(cmd, m_block_us);
        return true;
    case 047: {     // весь файл: по блоку карты на 512 байт
        const uint64_t blocks = m_file.open ? ((uint64_t)length() + 511) / 512 : 0;
        start_long(cmd, m_command_us + blocks * m_block_us);
        return true;
    }
    case 044: {
        std::vector<Region> regions;
        screen_plan(regions);
        uint64_t words = 0;
        for (size_t i = 0; i < regions.size(); i++) words += regions[i].words;
        start_long(cmd, m_command_us + (words * 2 + 1024 + 511) / 512 * m_block_us);
        return true;
    }
    default:        // 050, 053, 056
        start_long(cmd, m_command_us);
        return true;
    }
}

// 003: путь оглавления - строка в буфере блока (016). Нужен поток 016 (или
// другой поток записи): остаток потока - нули. Итог - в слове ошибки, разряда
// 15 нет. Неудача не закрывает прежнее оглавление, но 013 по нему - ошибка
void AZBKDisk::open_dir()
{
    if (m_in_target == IN_NONE) {
        m_errword = E_NO016;
        return;
    }
    zero_rest_of_input();
    m_dir.clear();
    m_dir_pos = 0;
    std::string path;
    int result = AzSdCard::parse(buffer_string(m_iobuf, 512), path);
    if (result == AzSdCard::FR_OK) result = card().list(path, m_dir);
    if (result != AzSdCard::FR_OK) {
        m_dir.clear();
        m_dir_valid = false;
        m_errword = (uint16_t)(E_DIR | result);
        return;
    }
    m_dir_path = path;
    m_dir_open = true;
    m_dir_valid = true;
    m_errword = 0;
}

// 013: запись оглавления (FILINFO FatFs без длинных имен), 12 слов для 015 -
// размер (2 слова), дата и время FAT, атрибуты, имя "ИМЯ.ТИП" с нулем (13
// байт). За последней - та же запись с нулем в первом байте имени, как у
// f_readdir. "." и ".." FatFs платы не отдает
void AZBKDisk::read_dir()
{
    m_errword = 0;
    if (!m_dir_valid) {
        m_errword = (uint16_t)(E_DIR | 9);      // FR_INVALID_OBJECT
        return;
    }
    if (m_dir_pos >= m_dir.size()) {
        m_dirrec[4] &= 0x00FF;
        return;
    }
    const AzSdCard::Entry &e = m_dir[m_dir_pos++];
    uint8_t rec[DIRREC_WORDS * 2] = {};
    rec[0] = (uint8_t)e.size;
    rec[1] = (uint8_t)(e.size >> 8);
    rec[2] = (uint8_t)(e.size >> 16);
    rec[3] = (uint8_t)(e.size >> 24);
    rec[4] = (uint8_t)e.date;
    rec[5] = (uint8_t)(e.date >> 8);
    rec[6] = (uint8_t)e.time;
    rec[7] = (uint8_t)(e.time >> 8);
    rec[8] = e.dir ? 020 : 040;         // каталог; архивный файл
    for (size_t k = 0; k < e.name.size() && k < 12; k++) rec[9 + k] = (uint8_t)e.name[k];
    for (unsigned int w = 0; w < DIRREC_WORDS; w++) m_dirrec[w] = (uint16_t)(rec[w * 2] | (rec[w * 2 + 1] << 8));
}

// Байт буфера блока
static unsigned int iobuf_byte(const uint16_t * b, unsigned int i)
{
    return (i & 1) ? (b[i / 2] >> 8) & 0xFF : b[i / 2] & 0xFF;
}

// 004: строка "Dnn=путь" в буфере блока, как ее разбирает прошивка: ровно две
// цифры (первая 0-3), путь до 384 знаков, путь карты (с "0") - большими
// буквами. Итог - в слове ошибки, разряда 15 нет. Сетевые пространства (A, N,
// P, R, S) и другие диски плата пробует без ошибки; без сети привод остается
// пустым. Монтирование в AZ.INI не пишется
void AZBKDisk::mount()
{
    const unsigned int c0 = (unsigned int)path_char((char)iobuf_byte(m_iobuf, 0));
    if (c0 != 'D') {
        m_errword = E_NOT_D;
        return;
    }
    const unsigned int d1 = iobuf_byte(m_iobuf, 1), d2 = iobuf_byte(m_iobuf, 2);
    if (d1 < '0' || d1 > '3' || d2 < '0' || d2 > '9') {
        m_errword = E_UNIT;
        return;
    }
    const unsigned int n = (d1 - '0') * 10 + (d2 - '0');
    // 32 прошивка пропускает и пишет за своей таблицей
    if (n >= UNITS) {
        m_errword = E_RANGE;
        return;
    }
    if (m_units[n] != nullptr && m_units[n]->attached()) {
        m_errword = E_OPEN;
        return;
    }
    if (iobuf_byte(m_iobuf, 3) != '=') {
        m_errword = E_NO_EQ;
        return;
    }
    const bool local = iobuf_byte(m_iobuf, 4) == '0';
    std::string path;
    bool ended = false;
    for (unsigned int k = 0; k < 384; k++) {
        const char c = (char)iobuf_byte(m_iobuf, 4 + k);
        if (c == 0) {
            ended = true;
            break;
        }
        path += local ? path_char(c) : c;
    }
    if (!ended) {
        m_errword = E_LONG;
        return;
    }
    m_errword = 0;
    if (!local) return;
    // Привода, которого у этой машины нет, на плате нет вовсе
    if (m_units[n] == nullptr) {
        m_errword = E_RANGE;
        return;
    }
    std::string p, host;
    const std::vector<uint8_t> * data = nullptr;
    if (AzSdCard::parse(path, p) != AzSdCard::FR_OK || p.empty() || !card().mount_source(p, host, data)) {
        m_errword = E_STAT;
        return;
    }
    if (!mount_unit(n, path)) m_errword = (uint16_t)(E_FOPEN | AzSdCard::FR_DENIED);
}

// Образ на приводе n поставлен с карты (004 или AZ.INI) и с тех пор его не
// сменили кнопкой окна или сценарием
bool AZBKDisk::card_mounted(unsigned int n) const
{
    if (n >= UNITS || m_mount_path[n].empty() || m_units[n] == nullptr || !m_units[n]->attached()) return false;
    const std::string file = dsk_tools::get_filename(const_cast<HddUnit*>(m_units[n])->hdd_image().file_name());
    return str_tolower(m_mount_file[n]) == str_tolower(file);
}

// Монтирует на свободный привод n файл карты по строке монтирования ("0:/..."):
// файл компьютера (запись в него при записи в каталог) или файл памяти карты
bool AZBKDisk::mount_unit(unsigned int n, const std::string &text)
{
    const std::string given = collapse_separators(str_trim(text));
    std::string path, host;
    const std::vector<uint8_t> * data = nullptr;
    if (n >= UNITS || m_units[n] == nullptr || m_units[n]->attached()
        || AzSdCard::parse(given, path) != AzSdCard::FR_OK || path.empty()
        || !card().mount_source(path, host, data))
        return false;
    const bool ok = (data != nullptr) ? m_units[n]->attach_data("0:/" + path, *data)
                                      : m_units[n]->attach_file(host, card().writes_host());
    if (!ok) return false;
    m_mount[n] = given;
    m_mount_file[n] = dsk_tools::get_filename(m_units[n]->hdd_image().file_name());
    m_mount_path[n] = path;
    m_mount_writes[n] = m_units[n]->writes();
    return true;
}

// Снимает образ с привода n. Без записи в каталог записанное машиной на образ,
// смонтированный с карты, остается на карте - в ее памяти, как файл (до 32 МБ)
void AZBKDisk::drop_unit(unsigned int n)
{
    if (n >= UNITS || m_units[n] == nullptr) return;
    HddUnit * u = m_units[n];
    // Образ, открытый без записи в каталог (как ни переключали ее с тех пор):
    // записанное машиной - в память карты, иначе оно пропало бы
    if (card_mounted(n) && u->hdd_image().is_volatile() && u->writes() != m_mount_writes[n]) {
        std::vector<uint8_t> image;
        uint16_t date, time;
        fat_now(date, time);
        if (u->hdd_image().contents(image)) {
            // Образ округлен до сектора; длина файла - его
            uint32_t size = 0;
            if (card().file_size(m_mount_path[n], size) == AzSdCard::FR_OK && size <= image.size()) image.resize(size);
            card().store(m_mount_path[n], image, date, time);
        }
    }
    u->detach();
    m_mount[n].clear();
    m_mount_file[n].clear();
    m_mount_path[n].clear();
}

// AZ.INI карты, как его читает прошивка: пробелы и \r выбрасываются, строка
// кончается \n (последняя без него не читается), комментарий - строка с ";".
// Раздел [NETWORK]: MAC=000E35220B01. Раздел [DISKS]: "Dn=путь" - D, цифры
// (номер меньше 32), "=", путь до 384 знаков; путь карты (с "0") - большими
// буквами. Привод, который AZ.INI называет, получает этот файл; уже стоящий
// там по той же строке остается (записанное в него не теряется).
// Смонтированный с карты и не названный - снимается. Образ конфигурации или
// окна на приводе, которого AZ.INI не называет, остается: настоящей плате его
// взять неоткуда.
//
// Где плата останавливается с экраном Fatal Error, здесь машина работает
// дальше: строка вне раздела (#12), не по форме (#13), с путем длиннее (#14)
// пропускается, а привод, файла которого нет на карте (#15), остается пустым.
// Привода, которого у машины нет (D4 и дальше), тоже нет; разделы [ROM] и
// [LOGO] (ПЗУ слотов и логотип - у конфигурации), [BOOT] и [ALTBOOT] не
// используются
void AZBKDisk::apply_az_ini()
{
    memcpy(m_mac, DEFAULT_MAC, sizeof(m_mac));
    AzSdCard &c = card();
    uint32_t size = 0;
    if (!c.present() || c.file_size("AZ.INI", size) != AzSdCard::FR_OK || size == 0 || size > 65536) return;
    std::vector<uint8_t> raw(size);
    if (!c.read("AZ.INI", 0, raw.data(), size)) return;
    std::map<unsigned int, std::string> disks;
    std::string section, line;
    for (size_t pos = 0; pos < raw.size(); pos++) {
        const char ch = (char)raw[pos];
        if (ch == ' ' || ch == '\r') continue;
        if (ch != '\n') {
            if (line.size() < 512) line += ch;
            continue;
        }
        const std::string l = line;
        line.clear();
        if (l.empty() || l[0] == ';') continue;
        if (l[0] == '[') {
            const size_t close = l.find(']');
            section = str_toupper(l.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            continue;
        }
        if (section == "NETWORK") {
            if (str_toupper(l.substr(0, 4)) == "MAC=" && l.size() >= 16) {
                uint8_t mac[6];
                bool ok = true;
                for (unsigned int i = 0; i < 6 && ok; i++) {
                    unsigned int v = 0;
                    for (unsigned int k = 0; k < 2; k++) {
                        const char h = l[4 + i * 2 + k];
                        const int dgt = (h >= '0' && h <= '9') ? h - '0' : (h >= 'A' && h <= 'F') ? h - 'A' + 10
                                      : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : -1;
                        if (dgt < 0) ok = false;
                        v = v * 16 + (unsigned int)(dgt < 0 ? 0 : dgt);
                    }
                    mac[i] = (uint8_t)v;
                }
                if (ok) memcpy(m_mac, mac, sizeof(m_mac));
            }
            continue;
        }
        if (section != "DISKS" || path_char(l[0]) != 'D') continue;
        size_t i = 1;
        unsigned int n = 0;
        while (i < l.size() && l[i] >= '0' && l[i] <= '9') {
            n = std::min(n * 10 + (unsigned int)(l[i] - '0'), 1000u);
            i++;
        }
        if (i == 1 || n >= UNITS || i >= l.size() || l[i] != '=') continue;
        std::string path = l.substr(i + 1);
        if (path.size() >= 384) continue;
        if (!path.empty() && path[0] == '0')
            for (size_t k = 0; k < path.size(); k++) path[k] = path_char(path[k]);
        disks[n] = collapse_separators(path);
    }
    for (unsigned int n = 0; n < UNITS; n++) {
        if (m_units[n] == nullptr) continue;
        std::map<unsigned int, std::string>::const_iterator it = disks.find(n);
        if (it == disks.end()) {
            if (card_mounted(n)) drop_unit(n);
            continue;
        }
        std::string want;
        if (AzSdCard::parse(it->second, want) != AzSdCard::FR_OK || want.empty()) continue;
        if (card_mounted(n) && m_mount_path[n] == want) {
            m_mount[n] = it->second;
            continue;
        }
        if (m_units[n]->attached()) drop_unit(n);
        mount_unit(n, it->second);
    }
}

// 014: привод - строкой "Dnn" в буфере блока (ровно две цифры, как у 004),
// номер в DR прошивка не берет. Итог - в слове ошибки, разряда 15 нет; ошибки
// разбора строки копятся (прошивка складывает их с итогом старта). Без записи на
// диск компьютера записанное машиной на смонтированном с карты образе остается
// на карте - в ее памяти, как файл (до 32 МБ)
void AZBKDisk::unmount()
{
    if (path_char((char)iobuf_byte(m_iobuf, 0)) != 'D') {
        m_status |= E_NOT_D;
        m_errword = m_status;
        return;
    }
    const unsigned int d1 = iobuf_byte(m_iobuf, 1), d2 = iobuf_byte(m_iobuf, 2);
    if (d1 < '0' || d1 > '3' || d2 < '0' || d2 > '9') {
        m_status |= E_UNIT;
        m_errword = m_status;
        return;
    }
    const unsigned int n = (d1 - '0') * 10 + (d2 - '0');
    if (n >= UNITS) {
        m_errword = E_RANGE;
        return;
    }
    if (m_units[n] == nullptr || !m_units[n]->attached()) {
        m_errword = E_NOT_OPEN;
        return;
    }
    drop_unit(n);
    m_errword = 0;
}

// Сколько байт файла 050 есть с pos: как f_read, у конца файла меньше. Длина -
// взятая при открытии: каталоги пути не листаются заново на каждый блок
uint32_t AZBKDisk::read_file(uint32_t pos, uint8_t * buffer, uint32_t count)
{
    if (!m_file.open || m_file.write || pos >= m_file.size) return 0;
    const uint32_t n = std::min(count, m_file.size - pos);
    return card().read(m_file.path, pos, buffer, n) ? n : 0;
}

// 050: имя файла - строка в буфере 023, до 256 байт. Длина файла - для 051; у
// неудачного открытия она 0 (прошивка считает 1 << 31 + код как 1 << (31 +
// код), а не код с разрядом 31, как в вики)
void AZBKDisk::open_read()
{
    m_file = CardFile();
    set_length(0);
    std::string path;
    uint32_t size = 0;
    int result = AzSdCard::parse(buffer_string(m_cmos, 256), path);
    if (result == AzSdCard::FR_OK) result = path.empty() ? AzSdCard::FR_INVALID_NAME : card().file_size(path, size);
    if (result != AzSdCard::FR_OK) return;
    m_file.open = true;
    m_file.path = path;
    m_file.size = size;
    set_length(size);
}

// 052: до 512 байт файла с текущего места в буфер; меньше 512 до длины - буфер
// сначала очищается. На длине файл закрывается
void AZBKDisk::read_file_block()
{
    const uint32_t remaining = length() - m_file.pos;
    const uint32_t want = std::min<uint32_t>(remaining, 512);
    if (want < 512) memset(m_cmos, 0, sizeof(m_cmos));
    uint8_t block[512];
    const uint32_t got = read_file(m_file.pos, block, want);
    for (uint32_t i = 0; i < got; i++) {
        if (i & 1) m_cmos[i / 2] = (uint16_t)((m_cmos[i / 2] & 0x00FF) | (block[i] << 8));
        else m_cmos[i / 2] = (uint16_t)((m_cmos[i / 2] & 0xFF00) | block[i]);
    }
    m_file.pos += got;
    if (m_file.pos >= length()) m_file.open = false;
}

// 053: файл создается (или обрезается) сразу, длина - 0 до 054
void AZBKDisk::open_write()
{
    m_file = CardFile();
    set_length(0);
    std::string path;
    int result = AzSdCard::parse(buffer_string(m_cmos, 256), path);
    if (result == AzSdCard::FR_OK) {
        uint16_t date, time;
        fat_now(date, time);
        result = path.empty() ? AzSdCard::FR_INVALID_NAME : card().create(path, date, time);
    }
    if (result != AzSdCard::FR_OK) return;
    m_file.open = true;
    m_file.write = true;
    m_file.path = path;
}

// 055: до 512 байт буфера в файл, сколько осталось до длины 054; на ней файл
// закрывается
void AZBKDisk::write_file_block()
{
    const uint32_t remaining = length() - m_file.pos;
    const uint32_t n = std::min<uint32_t>(remaining, 512);
    uint32_t put = 0;
    if (m_file.open && m_file.write && n > 0) {
        uint8_t block[512];
        for (unsigned int i = 0; i < 256; i++) {
            block[i * 2] = (uint8_t)m_cmos[i];
            block[i * 2 + 1] = (uint8_t)(m_cmos[i] >> 8);
        }
        if (card().append(m_file.path, block, n)) put = n;
    }
    m_file.pos += put;
    if (m_file.pos >= length()) m_file.open = false;
}

// 047: файл 050 в память AZ. Словный адрес - (слово 0 + слово 1 буфера) << 16,
// как его считает прошивка (вики: младшее и старшее слово; совпадает, пока
// младшее - 0), от 0x100000 до 0x800000; длина - из 050, конец - не дальше
// 0x800000. Состояние - в слове 0 буфера: 0 - прочитан, 1 - адрес, 2 - длина,
// 3 - ошибка чтения; остальной буфер - последний прочитанный блок. Хвост файла
// (длина не кратна 512) прошивка берет не из того буфера: в память идут нули
void AZBKDisk::read_into_memory()
{
    const uint32_t word = ((uint32_t)m_cmos[0] + m_cmos[1]) << 16;
    if ((int32_t)word < 0x100000 || (int32_t)word > 0x800000) {
        m_cmos[0] = 1;
        return;
    }
    const uint32_t len = length();
    if (len == 0 || (uint64_t)len + word > 0x800000) {
        m_cmos[0] = 2;
        return;
    }
    uint8_t * mem = m_board->memory();
    uint64_t byte = (uint64_t)word * 2;
    const uint64_t first = byte;
    uint32_t left = len;
    bool failed = false;
    // Файл читается с карты одним разом, дальше - по блокам, как у прошивки
    const uint32_t blocks = (len + 511) / 512;
    std::vector<uint8_t> data((size_t)blocks * 512, 0);
    const uint32_t have = read_file(m_file.pos, data.data(), blocks * 512);
    uint32_t taken = 0;
    while (left > 0 && !failed) {
        const uint32_t part = std::min<uint32_t>(left, 512);
        if (!m_file.open || m_file.write) {
            failed = true;
            break;
        }
        const uint32_t got = (have > taken) ? std::min<uint32_t>(512, have - taken) : 0;
        const uint8_t * block = data.data() + taken;
        taken += got;
        m_file.pos += got;
        for (uint32_t i = 0; i < got; i++) {
            if (i & 1) m_cmos[i / 2] = (uint16_t)((m_cmos[i / 2] & 0x00FF) | (block[i] << 8));
            else m_cmos[i / 2] = (uint16_t)((m_cmos[i / 2] & 0xFF00) | block[i]);
        }
        // Целый блок идет из буфера (у конца файла - с прежним хвостом буфера)
        const uint32_t bytes = (part == 512) ? 512 : (part & ~1u);
        if (byte + bytes > m_board->memory_size()) break;
        if (part == 512) {
            for (unsigned int i = 0; i < 256; i++) {
                mem[byte + i * 2] = (uint8_t)m_cmos[i];
                mem[byte + i * 2 + 1] = (uint8_t)(m_cmos[i] >> 8);
            }
        } else
            memset(mem + byte, 0, bytes);
        byte += bytes;
        left -= part;
    }
    for (uint64_t p = first / AZBK::PAGE_BYTES; byte > first && p <= (byte - 1) / AZBK::PAGE_BYTES; p++)
        m_board->touch_page((unsigned int)p);
    if (failed) {
        m_cmos[0] = 3;
        return;
    }
    m_file.open = false;
    m_cmos[0] = 0;
}

// Что снимает 044 - по заголовку в странице AZ 076 (вики MAXIOL; метку 0240
// прошивка не проверяет): адрес и длина в словах (по два слова) - длина от 1
// до 0x100000; иначе начальная страница и число страниц - от 1 до 512; иначе
// картина по копиям регистров видео: рулон слоя 0 (длина рулона - по разрядам
// 15-12 копии 177230, 12 и дальше - 8192 слова), у слоеного режима - всех трех
// слоев
void AZBKDisk::screen_plan(std::vector<Region> &regions) const
{
    regions.clear();
    const uint8_t * mem = m_board->memory();
    const uint32_t base = 076 * AZBK::PAGE_BYTES;
    if (mem == nullptr || base + 1024 > m_board->memory_size()) return;
    uint16_t h[40];
    for (unsigned int i = 0; i < 40; i++) h[i] = (uint16_t)(mem[base + i * 2] | (mem[base + i * 2 + 1] << 8));
    const uint32_t address = (uint32_t)h[1] | ((uint32_t)h[2] << 16);
    const uint32_t length = (uint32_t)h[3] | ((uint32_t)h[4] << 16);
    if (length != 0 && length <= 0x100000) {
        regions.push_back(Region{address, length});
    } else if (h[6] > 0 && h[6] <= 0x200) {
        regions.push_back(Region{(uint32_t)h[5] << 11, (uint32_t)h[6] << 11});
    } else {
        const unsigned int mode = h[29];
        const unsigned int roll = (mode >> 12) & 15;
        const uint32_t words = (roll < 12) ? AZBKVga::ROLL_WORDS[roll] : 0x2000;
        regions.push_back(Region{(uint32_t)h[30] << 11, words});
        if (mode & 4) {
            regions.push_back(Region{(uint32_t)h[31] << 11, words});
            regions.push_back(Region{(uint32_t)h[32] << 11, words});
        }
    }
}

// 044: файл - первый килобайт страницы 076 и снятая память. Имя - строка в
// буфере 023; короче 10 знаков - 0:/SCREENS/ДДЧЧММСС.SCR по часам платы, его
// прошивка и оставляет в буфере. Ошибка открытия - "ERROR код f_open имя" в
// буфере. Участок не длиннее 256 слов прошивка не пишет (только заголовок).
// Адреса памяти - по модулю 32 МБ, как 24-разрядный адрес слова
void AZBKDisk::screenshot()
{
    std::string name = buffer_string(m_cmos, 512);
    if (name.size() < 10) {
        const uint64_t s = now_seconds();
        int y, mo, d;
        civil_from_days((int64_t)(s / 86400), y, mo, d);
        const unsigned int sod = (unsigned int)(s % 86400);
        char buf[40];
        std::snprintf(buf, sizeof(buf), "0:/SCREENS/%02d%02u%02u%02u.SCR", d, sod / 3600, (sod / 60) % 60, sod % 60);
        name = buf;
        put_cmos_string(name);
    }
    std::string path;
    uint16_t date, time;
    fat_now(date, time);
    int result = AzSdCard::parse(name, path);
    if (result == AzSdCard::FR_OK) result = path.empty() ? AzSdCard::FR_INVALID_NAME : card().create(path, date, time);
    if (result != AzSdCard::FR_OK) {
        put_cmos_string("ERROR " + std::to_string(result) + " f_open " + name);
        return;
    }
    std::vector<Region> regions;
    screen_plan(regions);
    const uint8_t * mem = m_board->memory();
    const uint64_t size = m_board->memory_size();
    card().append(path, mem + 076 * AZBK::PAGE_BYTES, 1024);
    std::vector<uint8_t> data;
    for (size_t i = 0; i < regions.size(); i++) {
        if (regions[i].words <= 256) continue;
        data.resize((size_t)regions[i].words * 2);
        for (uint32_t w = 0; w < regions[i].words; w++) {
            const uint64_t b = ((uint64_t)(regions[i].word + w) * 2) % size;
            data[w * 2] = mem[b];
            data[w * 2 + 1] = mem[b + 1];
        }
        card().append(path, data.data(), (uint32_t)data.size());
    }
}

//------------------------------------------------------- обращения шины ---

// Пока контроллер занят, отвечает только CSR
bool AZBKDisk::refused()
{
    if (m_done) return false;
    m_refused++;
    if (m_mapper != nullptr) m_mapper->no_device = true;
    return true;
}

// Чтение DR. Поток: слово за концом - ошибка, поток кончается. Короткий ответ:
// после последнего слова - слово ошибки. 027: последнее слово повторяется
uint16_t AZBKDisk::dr_read()
{
    switch (m_out_mode) {
    case OUT_STREAM:
        if (m_out_pos < m_out.size()) return m_last_out = m_out[m_out_pos++];
        m_out_mode = OUT_IDLE;
        m_err = true;
        return m_last_out = 0;
    case OUT_SEQ:
        if (m_out_pos < m_out.size()) {
            m_last_out = m_out[m_out_pos++];
            if (m_out_pos >= m_out.size()) m_out_mode = OUT_IDLE;
            return m_last_out;
        }
        m_out_mode = OUT_IDLE;
        break;
    case OUT_STICKY:
        if (m_out_pos < m_out.size()) m_last_out = m_out[m_out_pos++];
        return m_last_out;
    default:
        break;
    }
    return m_last_out = m_errword;
}

uint16_t AZBKDisk::dr_peek() const
{
    switch (m_out_mode) {
    case OUT_STREAM: return (m_out_pos < m_out.size()) ? m_out[m_out_pos] : 0;
    case OUT_SEQ:    if (m_out_pos < m_out.size()) return m_out[m_out_pos]; break;
    case OUT_STICKY: return (m_out_pos < m_out.size()) ? m_out[m_out_pos] : m_last_out;
    default: break;
    }
    return m_errword;
}

// Регистры загрузки: процессор, начавший с 0177224 (0177226), исполняет то, что
// они отдают, - загрузку для машин без ПЗУ AZ. BOOT1: BR . (три раза), MTPS
// -(PC) с операндом 340, BR на 177220 - дальше загрузчик отдают CSR и DR из
// файла [ALTBOOT] B4= AZ.INI; BOOT2: MTPS, MOV -(PC),R0 и слова файла B6=. У БК
// такого файла нет (BIOS грузится своим ПЗУ), и загрузчик не моделируется:
// дальше BOOT1 повторяет BR, BOOT2 отдает нули
static const uint16_t BOOT1_WORDS[] = { 0777, 0777, 0777, 0106447, 0340, 0775 };
static const uint16_t BOOT2_WORDS[] = { 0106447, 0340, 014700 };

uint16_t AZBKDisk::boot_read(bool second, bool peek)
{
    unsigned int &pos = second ? m_boot2 : m_boot1;
    const uint16_t * w = second ? BOOT2_WORDS : BOOT1_WORDS;
    const unsigned int n = second ? 3 : 6;
    const uint16_t v = (pos < n) ? w[pos] : (second ? 0 : 0775);
    if (!peek && pos < n) pos++;
    return v;
}

unsigned int AZBKDisk::get_value_word(unsigned int address)
{
    const unsigned int a = address & ~1u;
    if (a == R_CSR) return csr();
    if (refused()) return 0177777;
    if (a == R_DR) return dr_read();
    if (a == R_BOOT1) return boot_read(false, false);
    if (a == R_BOOT2) return boot_read(true, false);
    return 0177777;
}

unsigned int AZBKDisk::get_value(unsigned int address)
{
    const unsigned int v = get_value_word(address);
    return (address & 1) ? (v >> 8) & 0xFF : v & 0xFF;
}

unsigned int AZBKDisk::get_direct(unsigned int address)
{
    const unsigned int a = address & ~1u;
    unsigned int v;
    if (a == R_CSR) v = csr();
    else if (a == R_DR) v = dr_peek();
    else if (a == R_BOOT1) v = boot_read(false, true);
    else if (a == R_BOOT2) v = boot_read(true, true);
    else v = 0;
    return (address & 1) ? (v >> 8) & 0xFF : v & 0xFF;
}

void AZBKDisk::set_value_word(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    const unsigned int a = address & ~1u;
    value &= 0177777;
    if (a == R_CSR) {
        command(value);
        return;
    }
    if (refused()) return;
    if (a != R_DR) return;
    // Поток записи живет до сброса: лишнее слово - ошибка (прошивка пишет его
    // за буфер), аргумент команды в это время не меняется
    if (m_in_target != IN_NONE) {
        if (m_in != nullptr && m_in_pos < m_in_size) m_in[m_in_pos] = (uint16_t)value;
        else m_err = true;
        if (m_in_pos < 0x10000) m_in_pos++;
        return;
    }
    m_arg = value;
}

// Байтовая запись в регистры контроллера недопустима
void AZBKDisk::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)address;
    (void)value;
    (void)force;
    note_unsupported("byte write");
}

//---------------------------------------------------------------- снимок ---

void AZBKDisk::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.b("done", m_done);
    w.b("error", m_err);
    w.b("big", m_big);
    w.b("ie", m_ie);
    w.b("network", m_network);
    w.b("irq_pending", m_irq_pending);
    w.u("offered", m_irq.offered(), 16);
    w.u("arg", m_arg, 16);
    w.u("command", m_command, 8);
    w.n("unit_no", m_unit);
    w.n("block", m_block);
    w.b("block_low", m_block_low);
    w.u("errword", m_errword, 16);
    w.u("status", m_status, 16);
    w.b("busy", m_busy);
    w.u("busy_command", m_busy_cmd, 8);
    w.n64("busy_us", m_busy_us);
    w.n64("us_acc", m_us_acc);
    w.n64("restart_left", m_restart_left);
    w.n64("clocks", m_clocks);
    w.n64("rtc_shift", (uint64_t)m_rtc_shift);
    w.array("iobuf", m_iobuf, 256);
    w.array("cmos", m_cmos, 256);
    w.array("eeprom", m_eeprom, EEPROM_WORDS);
    w.b("eeprom_valid", m_eeprom_valid);
    w.array("timestamp", m_timestamp, 14);
    w.array("simple_in", m_simple_in, 7);
    w.array("ipdata", m_ipdata, 12);
    w.array("mac", m_mac, 6);
    w.n("buf", m_buf);
    w.n("buf_len", m_buf_len);
    w.n("out_mode", m_out_mode);
    if (!m_out.empty()) w.array("out", m_out.data(), m_out.size());
    w.n("out_size", (uint32_t)m_out.size());
    w.n("out_pos", m_out_pos);
    w.u("last_out", m_last_out, 16);
    w.n("in_target", m_in_target);
    w.n("in_size", m_in_size);
    w.n("in_pos", m_in_pos);
    w.n("boot1", m_boot1);
    w.n("boot2", m_boot2);
    w.n("commands", m_commands);
    w.b("dir_open", m_dir_open);
    w.b("dir_valid", m_dir_valid);
    w.s("dir_path", m_dir_path);
    w.n("dir_pos", m_dir_pos);
    w.array("dirrec", m_dirrec, DIRREC_WORDS);
    for (unsigned int i = 0; i < UNITS; i++) {
        if (m_mount[i].empty()) continue;
        const std::string k = std::to_string(i);
        w.s(("mount_" + k).c_str(), m_mount[i]);
        w.s(("mount_file_" + k).c_str(), m_mount_file[i]);
        w.s(("mount_path_" + k).c_str(), m_mount_path[i]);
        w.n(("mount_writes_" + k).c_str(), m_mount_writes[i]);
    }
    w.b("file_open", m_file.open);
    w.b("file_write", m_file.write);
    w.s("file_path", m_file.path);
    w.n("file_size", m_file.size);
    w.n("file_pos", m_file.pos);
    w.array("length", m_length, 2);
    w.array("sizecard", m_sizecard, 2);
}

emulator::Result AZBKDisk::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.b("done", m_done);
    r.b("error", m_err);
    m_big = false;
    r.b("big", m_big);
    r.b("ie", m_ie);
    r.b("network", m_network);
    r.b("irq_pending", m_irq_pending);
    unsigned int offered = 0;
    if (r.u("offered", offered)) m_irq.set_offered(offered);
    r.u("arg", m_arg);
    r.u("command", m_command);
    // Снимок прежней сборки: номер + 1, 0 - не выбран
    unsigned int unit = 0;
    if (r.u("unit_no", unit)) m_unit = unit;
    else if (r.u("unit", unit)) m_unit = (unit >= 1) ? unit - 1 : 0;
    r.u("block", m_block);
    r.b("block_low", m_block_low);
    unsigned int v = 0;
    m_errword = 0;
    if (r.u("errword", v)) m_errword = (uint16_t)v;
    m_status = 0;
    if (r.u("status", v)) m_status = (uint16_t)v;
    r.b("busy", m_busy);
    r.u("busy_command", m_busy_cmd);
    r.n64("busy_us", m_busy_us);
    r.n64("us_acc", m_us_acc);
    r.n64("restart_left", m_restart_left);
    r.n64("clocks", m_clocks);
    uint64_t shift = 0;
    if (r.n64("rtc_shift", shift)) m_rtc_shift = (int64_t)shift;
    r.array("iobuf", m_iobuf, 256);
    r.array("cmos", m_cmos, 256);
    // Снимок прежней сборки: 023 писала отдельный буфер, его берет 024
    r.array("eeprom_in", m_cmos, EEPROM_WORDS);
    r.array("eeprom", m_eeprom, EEPROM_WORDS);
    r.b("eeprom_valid", m_eeprom_valid);
    r.array("timestamp", m_timestamp, 14);
    r.array("simple_in", m_simple_in, 7);
    r.array("ipdata", m_ipdata, 12);
    memcpy(m_mac, DEFAULT_MAC, sizeof(m_mac));
    r.array("mac", m_mac, 6);
    m_buf = BUF_IOBUF;
    m_buf_len = 256;
    r.u("buf", m_buf);
    r.u("buf_len", m_buf_len);
    if (m_buf > BUF_LENGTH) m_buf = BUF_IOBUF;
    if (m_buf_len > UNITS * ENTRY_WORDS) m_buf_len = UNITS * ENTRY_WORDS;
    unsigned int out_size = 0;
    r.u("out_size", out_size);
    if (out_size > UNITS * ENTRY_WORDS) out_size = 0;
    m_out.assign(out_size, 0);
    if (out_size > 0) r.array("out", m_out.data(), out_size);
    r.u("out_pos", m_out_pos);
    m_out_mode = out_size > 0 ? OUT_STREAM : OUT_IDLE;
    r.u("out_mode", m_out_mode);
    if (m_out_mode > OUT_STICKY) m_out_mode = OUT_IDLE;
    unsigned int last = 0;
    if (r.u("last_out", last)) m_last_out = (uint16_t)last;
    r.u("in_target", m_in_target);
    r.u("in_size", m_in_size);
    r.u("in_pos", m_in_pos);
    m_in = nullptr;
    if (m_in_target == IN_IOBUF && m_in_size <= 256) m_in = m_iobuf;
    else if (m_in_target == IN_CMOS && m_in_size <= 256) m_in = m_cmos;
    else if (m_in_target == IN_SIMPLE && m_in_size <= 7) m_in = m_simple_in;
    else if (m_in_target == IN_LENGTH && m_in_size <= 2) m_in = m_length;
    else m_in_target = IN_NONE;
    if (m_in_target == IN_NONE) m_in_size = m_in_pos = 0;
    if (m_out_pos > m_out.size()) m_out_pos = (unsigned int)m_out.size();
    m_boot1 = m_boot2 = 0;
    r.u("boot1", m_boot1);
    r.u("boot2", m_boot2);
    r.u("commands", m_commands);

    r.b("dir_open", m_dir_open);
    m_dir_valid = m_dir_open;
    r.b("dir_valid", m_dir_valid);
    r.s("dir_path", m_dir_path);
    r.u("dir_pos", m_dir_pos);
    r.array("dirrec", m_dirrec, DIRREC_WORDS);
    // Пути карты из снимка - только такие, какие дал бы разбор пути гостя
    std::string check;
    if (AzSdCard::parse(m_dir_path, check) != AzSdCard::FR_OK || check != m_dir_path) m_dir_valid = false;
    for (unsigned int i = 0; i < UNITS; i++) {
        const std::string k = std::to_string(i);
        m_mount[i].clear();
        m_mount_file[i].clear();
        m_mount_path[i].clear();
        m_mount_writes[i] = 0;
        r.s(("mount_" + k).c_str(), m_mount[i]);
        r.s(("mount_file_" + k).c_str(), m_mount_file[i]);
        r.s(("mount_path_" + k).c_str(), m_mount_path[i]);
        r.u(("mount_writes_" + k).c_str(), m_mount_writes[i]);
        if (AzSdCard::parse(m_mount_path[i], check) != AzSdCard::FR_OK || check != m_mount_path[i]) m_mount_path[i].clear();
    }
    m_file = CardFile();
    r.b("file_open", m_file.open);
    r.b("file_write", m_file.write);
    r.s("file_path", m_file.path);
    r.u("file_pos", m_file.pos);
    if (AzSdCard::parse(m_file.path, check) != AzSdCard::FR_OK || check != m_file.path) m_file.open = false;
    r.u("file_size", m_file.size);
    memset(m_length, 0, sizeof(m_length));
    if (!r.array("length", m_length, 2)) r.array("length_in", m_length, 2);
    r.array("sizecard", m_sizecard, 2);
    return emulator::Result::ok();
}

// Оглавление читается заново, когда восстановлено и гнездо карты: каталог мог
// измениться
void AZBKDisk::state_restored()
{
    AddressableDevice::state_restored();
    m_dir.clear();
    if (m_dir_valid && (!card().present() || card().list(m_dir_path, m_dir) != AzSdCard::FR_OK)) m_dir_valid = false;
    if (m_dir_pos > m_dir.size()) m_dir_pos = (unsigned int)m_dir.size();
    // Снимок прежней сборки не несет каталогов, которые контроллер заводит при
    // старте; есть уже - ничего не меняется
    if (card().present()) {
        card().make_dir("TEMP");
        card().make_dir("SCREENS");
    }
}

// Карту вынули или вставили другую: оглавление и файл закрыты, образы,
// смонтированные с нее, сняты (записанное в них пропадает вместе с картой).
// Сменилось, куда идет запись: файл на запись закрыт - его начало осталось
// там, куда шла запись
void AZBKDisk::card_changed(bool replaced)
{
    if (m_file.write) m_file = CardFile();
    if (!replaced) return;
    m_dir_open = false;
    m_dir_valid = false;
    m_dir.clear();
    m_dir_pos = 0;
    m_file = CardFile();
    for (unsigned int i = 0; i < UNITS; i++) {
        // Образ, вставленный поверх кнопкой окна, остается
        if (card_mounted(i)) m_units[i]->detach();
        m_mount[i].clear();
        m_mount_file[i].clear();
        m_mount_path[i].clear();
    }
    // На вставленной карте - каталоги, которые плата заводит при старте
    if (card().present()) {
        card().make_dir("TEMP");
        card().make_dir("SCREENS");
    }
}

//----------------------------------------------------------------- поля ---

std::vector<DeviceFieldInfo> AZBKDisk::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"csr",      "0177220 as read: 200 ready, 100000 error, 40000 BIG", false});
    r.push_back({"command",  "Last command taken",                                 false});
    r.push_back({"unit",     "Selected unit (as written for 001)",                 false});
    r.push_back({"block",    "Block number",                                       false});
    r.push_back({"error",    "Error word: 0177222 read outside a stream",          false});
    r.push_back({"busy",     "1 while a long command runs",                        false});
    r.push_back({"commands", "Commands taken since power-on",                      false});
    r.push_back({"refused",  "Accesses refused while busy (trap to 4)",           false});
    r.push_back({"trace",    "The last 64 command words written to 0177220",       false});
    r.push_back({"eeprom",   "Non-volatile memory words from-to",                  true});
    r.push_back({"rtc",      "Clock: year, month, day, hour, minute, second",      false});
    r.push_back({"vector",   "Vector offered to the processor, or 0",              false});
    r.push_back({"sd",       "1 if the microSD card is in",                       false});
    r.push_back({"file",     "Card file of 050/053: open, 1 if for writing, bytes read or written, length", false});
    return r;
}

bool AZBKDisk::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "csr")      { out.values.push_back(csr()); return true; }
    if (field == "command")  { out.width = 8; out.values.push_back(m_command); return true; }
    if (field == "unit")     { out.values.push_back(m_unit & 0177777); return true; }
    if (field == "block")    { out.width = 32; out.values.push_back(m_block); return true; }
    if (field == "error")    { out.values.push_back(m_errword); return true; }
    if (field == "busy")     { out.width = 1; out.values.push_back((m_busy || !m_done) ? 1 : 0); return true; }
    if (field == "commands") { out.width = 32; out.values.push_back(m_commands); return true; }
    if (field == "refused")  { out.width = 32; out.values.push_back(m_refused); return true; }
    if (field == "vector")   { out.values.push_back(m_irq.offered()); return true; }
    if (field == "sd")       { out.width = 1; out.values.push_back(card().present() ? 1 : 0); return true; }
    if (field == "file") {
        out.width = 32;
        out.values.push_back(m_file.open ? 1 : 0);
        out.values.push_back(m_file.write ? 1 : 0);
        out.values.push_back(m_file.pos);
        out.values.push_back(length());
        return true;
    }
    if (field == "trace")    { out.width = 8; for (uint16_t v : m_trace) out.values.push_back(v); return true; }
    if (field == "eeprom") {
        if (to < from) to = from;
        for (unsigned int i = from; i <= to && i < EEPROM_WORDS; i++) out.values.push_back(m_eeprom[i]);
        return true;
    }
    if (field == "rtc") {
        out.width = 0;
        const uint64_t s = now_seconds();
        int y, mo, d;
        civil_from_days((int64_t)(s / 86400), y, mo, d);
        const unsigned int sod = (unsigned int)(s % 86400);
        out.values.push_back((unsigned int)y);
        out.values.push_back((unsigned int)mo);
        out.values.push_back((unsigned int)d);
        out.values.push_back(sod / 3600);
        out.values.push_back((sod / 60) % 60);
        out.values.push_back(sod % 60);
        return true;
    }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_azbk_disk(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AZBKDisk(im, cd);
}
