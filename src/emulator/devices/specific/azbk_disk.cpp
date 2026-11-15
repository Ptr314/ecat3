// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the AZ disk controller

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

#include "azbk_disk.h"
#include "azbk.h"
#include "emulator/devices/common/hdd_unit.h"
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
static const unsigned int CSR_ERR  = 0100000;

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

    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));
    return emulator::Result::ok();
}

// Сброс самого контроллера, как после перезапуска микроконтроллера
void AZBKDisk::reset_controller()
{
    m_done = true;
    m_err = false;
    m_ie = false;
    m_network = false;
    m_busy = false;
    m_busy_us = 0;
    m_us_acc = 0;
    m_unit = -1;
    m_block = 0;
    m_block_low = false;
    m_read_source = SRC_IOBUF;
    m_out.clear();
    m_out_pos = 0;
    m_in = nullptr;
    m_in_target = IN_NONE;
    m_irq_pending = false;
    update_irq();
}

void AZBKDisk::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        m_clocks = 0;
        m_rtc_shift = 0;
        m_restart_left = 0;
        i_dclo.change(1);
        memset(m_iobuf, 0, sizeof(m_iobuf));
        m_iobuf_filled = 0;
        m_commands = m_refused = 0;
        m_trace.clear();
    }
    m_irq.clear();
    reset_controller();
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
    if (m_unit < 0) return nullptr;
    HddUnit * u = m_units[m_unit];
    return (u != nullptr && u->attached()) ? u : nullptr;
}

void AZBKDisk::stream_out(const uint16_t * data, unsigned int count)
{
    m_out.assign(data, data + count);
    m_out_pos = 0;
    m_in = nullptr;
    m_in_target = IN_NONE;
}

void AZBKDisk::stream_in(unsigned int target, uint16_t * data, unsigned int count)
{
    m_out.clear();
    m_out_pos = 0;
    m_in = data;
    m_in_size = count;
    m_in_pos = 0;
    m_in_target = target;
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

// Таблица назначений (011): 32 записи {размер в байтах (2 слова), дата и
// время FAT, флаги, атрибуты, имя 386 байт}; пустая - нулевое имя (AZSMNT)
void AZBKDisk::build_table(std::vector<uint16_t> &out) const
{
    out.assign(UNITS * ENTRY_WORDS, 0);
    for (unsigned int i = 0; i < UNITS; i++) {
        const HddUnit * u = m_units[i];
        if (u == nullptr || !u->attached()) continue;
        uint16_t * e = &out[i * ENTRY_WORDS];
        const uint64_t bytes = const_cast<HddUnit*>(u)->hdd_image().size();
        e[0] = (uint16_t)bytes;
        e[1] = (uint16_t)(bytes >> 16);
        // Флаги - как у gid (1 - описан в AZ.INI, 4 - смонтирован): программы
        // MAXIOL смотрят только имя
        e[4] = 5;
        const std::string n = "0:/" + dsk_tools::get_filename(const_cast<HddUnit*>(u)->hdd_image().file_name());
        uint8_t * name = reinterpret_cast<uint8_t*>(e + 5);
        for (size_t k = 0; k < n.size() && k + 1 < NAME_BYTES; k++) name[k] = (uint8_t)n[k];
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
            m_out.clear();
            m_in = nullptr;
            m_in_target = IN_NONE;
            m_read_source = SRC_IOBUF;
            m_err = false;
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

    HddUnit * u;
    uint16_t tmp[2];
    switch (cmd) {
    case 000:   // сброс
        m_out.clear();
        m_in = nullptr;
        m_in_target = IN_NONE;
        m_read_source = SRC_IOBUF;
        break;
    case 001:   // выбор устройства, мгновенно
        m_unit = (int)(m_arg & 037);
        m_block = 0;
        m_block_low = false;
        if (unit() == nullptr) m_err = true;
        break;
    case 002:   // младшие биты номера блока, старшие очищаются
        m_block = m_arg & 0177777;
        u = unit();
        m_block_low = (u != nullptr);
        if (u == nullptr || m_block >= u->blocks()) m_err = true;
        break;
    case 012:   // старшие биты, только после 002
        u = unit();
        if (!m_block_low || u == nullptr) { m_err = true; break; }
        m_block = (m_block & 0177777) | ((m_arg & 0177777) << 16);
        if (m_block >= u->blocks()) m_err = true;
        break;
    case 005:
    case 006:
        start_long(cmd, m_block_us);
        return;
    case 007: { // размер, не больше 65534
        u = unit();
        const uint64_t b = (u != nullptr) ? u->blocks() : 0;
        tmp[0] = (uint16_t)std::min<uint64_t>(b, 65534);
        stream_out(tmp, 1);
        break;
    }
    case 017: { // размер, два слова
        u = unit();
        const uint64_t b = (u != nullptr) ? u->blocks() : 0;
        tmp[0] = (uint16_t)b;
        tmp[1] = (uint16_t)(b >> 16);
        stream_out(tmp, 2);
        break;
    }
    case 010:   // с IE - обслуживание сети до сброса, без прерывания
        if (m_ie) {
            m_network = true;
            m_done = false;
            m_busy = false;
        }
        return;
    case 011: { // таблица назначений - ее и отдаст 015
        m_read_source = SRC_TABLE;
        break;
    }
    case 015:   // буфер (или таблица) через DR
        if (m_read_source == SRC_TABLE) {
            std::vector<uint16_t> t;
            build_table(t);
            stream_out(t.data(), (unsigned int)t.size());
        } else
            stream_out(m_iobuf, 256);
        break;
    case 016:   // прием блока в буфер
        m_iobuf_filled = 0;
        stream_in(IN_IOBUF, m_iobuf, 256);
        break;
    case 020:   // расширенная диагностика: два слова
        tmp[0] = tmp[1] = 0;
        stream_out(tmp, 2);
        break;
    case 021:
    case 024:
    case 031:
    case 034:
    case 040:
    case 042:
    case 043:
        start_long(cmd, m_command_us);
        return;
    case 022:   // EEPROM через DR: слово состояния и 255 слов
        stream_out(m_cmos, 256);
        break;
    case 023:
        stream_in(IN_EEPROM, m_eeprom_in, EEPROM_WORDS);
        break;
    case 027:   // версия: прошивка STM32 и плата (4 - AZБК), затем старший диск
        tmp[0] = (uint16_t)((m_stm_version << 8) | 4);
        tmp[1] = 037;
        stream_out(tmp, 2);
        break;
    case 030:
        break;
    case 032:
        stream_out(m_timestamp, 14);
        break;
    case 033:
        stream_in(IN_SIMPLE, m_simple_in, 7);
        break;
    case 035:   // NTP и часы стека TCP/IP: сети нет (вопрос Б.3)
    case 036:
        start_long(cmd, m_command_us);
        return;
    case 037:   // перезапуск платы и всей БК
        m_board->restart();
        reset_controller();
        m_restart_left = (uint64_t)m_restart_ms * m_system_clock / 1000;
        if (m_restart_left == 0) m_restart_left = 1;
        i_dclo.change(0);
        return;
    case 041:
        stream_out(m_ipdata, 12);
        break;
    default:
        // Карта microSD (003, 004, 013, 014, 044, 047, 050-057) и Hall of
        // Fame (025, 026) - пока нет
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
        if (u == nullptr || m_block >= u->blocks() || !u->read_block(m_block, block)) {
            m_err = true;
            break;
        }
        for (unsigned int i = 0; i < 256; i++) m_iobuf[i] = (uint16_t)(block[i * 2] | (block[i * 2 + 1] << 8));
        break;
    case 006:
        // Не было записи в буфер - ошибка; неполный буфер дополняется нулями
        if (u == nullptr || m_iobuf_filled == 0 || m_block >= u->blocks() || u->is_protected()) {
            m_err = true;
            break;
        }
        for (unsigned int i = m_iobuf_filled; i < 256; i++) m_iobuf[i] = 0;
        for (unsigned int i = 0; i < 256; i++) {
            block[i * 2] = (uint8_t)m_iobuf[i];
            block[i * 2 + 1] = (uint8_t)(m_iobuf[i] >> 8);
        }
        if (!u->write_block(m_block, block)) m_err = true;
        break;
    case 021:   // слово состояния: 0 - прочитано, 1 - размер не тот (память не записана)
        m_cmos[0] = m_eeprom_valid ? 0 : 1;
        memcpy(m_cmos + 1, m_eeprom, sizeof(m_eeprom));
        break;
    case 024:   // буфер записи идет без слова состояния
        memcpy(m_eeprom, m_eeprom_in, sizeof(m_eeprom));
        m_eeprom_valid = true;
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
    case 040:   // адреса стека TCP/IP: сети нет, адреса нулевые (Б.3)
    case 043:
        memset(m_ipdata, 0, sizeof(m_ipdata));
        break;
    case 042:   // часы стека TCP/IP никто не ставил (Б.3)
        memset(m_timestamp, 0, sizeof(m_timestamp));
        break;
    default:
        break;
    }
    finish();
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

unsigned int AZBKDisk::get_value_word(unsigned int address)
{
    const unsigned int a = address & ~1u;
    if (a == R_CSR) return (m_done ? CSR_DONE : 0) | (m_done && m_err ? CSR_ERR : 0);
    if (refused()) return 0177777;
    if (a == R_DR) {
        if (m_out_pos < m_out.size()) m_last_out = m_out[m_out_pos++];
        return m_last_out;
    }
    // Регистры загрузки: на БК не используются, значения - как у gid
    if (a == R_BOOT1) return 0;
    if (a == R_BOOT2) return 0776;
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
    if (a == R_CSR) v = (m_done ? CSR_DONE : 0) | (m_done && m_err ? CSR_ERR : 0);
    else if (a == R_DR) v = (m_out_pos < m_out.size()) ? m_out[m_out_pos] : m_last_out;
    else if (a == R_BOOT2) v = 0776;
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
    if (m_in != nullptr && m_in_pos < m_in_size) {
        m_in[m_in_pos++] = (uint16_t)value;
        if (m_in_target == IN_IOBUF) m_iobuf_filled = m_in_pos;
        if (m_in_pos >= m_in_size) {
            m_in = nullptr;
            m_in_target = IN_NONE;
        }
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
    w.b("ie", m_ie);
    w.b("network", m_network);
    w.b("irq_pending", m_irq_pending);
    w.u("offered", m_irq.offered(), 16);
    w.u("arg", m_arg, 16);
    w.u("command", m_command, 8);
    w.n("unit", (uint32_t)(m_unit + 1));
    w.n("block", m_block);
    w.b("block_low", m_block_low);
    w.b("busy", m_busy);
    w.u("busy_command", m_busy_cmd, 8);
    w.n64("busy_us", m_busy_us);
    w.n64("us_acc", m_us_acc);
    w.n64("restart_left", m_restart_left);
    w.n64("clocks", m_clocks);
    w.n64("rtc_shift", (uint64_t)m_rtc_shift);
    w.array("iobuf", m_iobuf, 256);
    w.n("iobuf_filled", m_iobuf_filled);
    w.array("cmos", m_cmos, 256);
    w.array("eeprom", m_eeprom, EEPROM_WORDS);
    w.b("eeprom_valid", m_eeprom_valid);
    w.array("eeprom_in", m_eeprom_in, EEPROM_WORDS);
    w.array("timestamp", m_timestamp, 14);
    w.array("simple_in", m_simple_in, 7);
    w.array("ipdata", m_ipdata, 12);
    w.n("read_source", m_read_source);
    if (!m_out.empty()) w.array("out", m_out.data(), m_out.size());
    w.n("out_size", (uint32_t)m_out.size());
    w.n("out_pos", m_out_pos);
    w.u("last_out", m_last_out, 16);
    w.n("in_target", m_in_target);
    w.n("in_size", m_in_size);
    w.n("in_pos", m_in_pos);
    w.n("commands", m_commands);
}

emulator::Result AZBKDisk::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.b("done", m_done);
    r.b("error", m_err);
    r.b("ie", m_ie);
    r.b("network", m_network);
    r.b("irq_pending", m_irq_pending);
    unsigned int offered = 0;
    if (r.u("offered", offered)) m_irq.set_offered(offered);
    r.u("arg", m_arg);
    r.u("command", m_command);
    unsigned int unit = 0;
    r.u("unit", unit);
    m_unit = (unit >= 1 && unit <= UNITS) ? (int)unit - 1 : -1;
    r.u("block", m_block);
    r.b("block_low", m_block_low);
    r.b("busy", m_busy);
    r.u("busy_command", m_busy_cmd);
    r.n64("busy_us", m_busy_us);
    r.n64("us_acc", m_us_acc);
    r.n64("restart_left", m_restart_left);
    r.n64("clocks", m_clocks);
    uint64_t shift = 0;
    if (r.n64("rtc_shift", shift)) m_rtc_shift = (int64_t)shift;
    r.array("iobuf", m_iobuf, 256);
    r.u("iobuf_filled", m_iobuf_filled);
    r.array("cmos", m_cmos, 256);
    r.array("eeprom", m_eeprom, EEPROM_WORDS);
    r.b("eeprom_valid", m_eeprom_valid);
    r.array("eeprom_in", m_eeprom_in, EEPROM_WORDS);
    r.array("timestamp", m_timestamp, 14);
    r.array("simple_in", m_simple_in, 7);
    r.array("ipdata", m_ipdata, 12);
    r.u("read_source", m_read_source);
    unsigned int out_size = 0;
    r.u("out_size", out_size);
    m_out.assign(out_size, 0);
    if (out_size > 0) r.array("out", m_out.data(), out_size);
    r.u("out_pos", m_out_pos);
    unsigned int last = 0;
    if (r.u("last_out", last)) m_last_out = (uint16_t)last;
    r.u("in_target", m_in_target);
    r.u("in_size", m_in_size);
    r.u("in_pos", m_in_pos);
    m_in = nullptr;
    if (m_in_target == IN_IOBUF && m_in_size <= 256) m_in = m_iobuf;
    else if (m_in_target == IN_EEPROM && m_in_size <= EEPROM_WORDS) m_in = m_eeprom_in;
    else if (m_in_target == IN_SIMPLE && m_in_size <= 7) m_in = m_simple_in;
    else m_in_target = IN_NONE;
    if (m_in_pos > m_in_size) m_in_pos = m_in_size;
    if (m_out_pos > m_out.size()) m_out_pos = (unsigned int)m_out.size();
    r.u("commands", m_commands);
    return emulator::Result::ok();
}

//----------------------------------------------------------------- поля ---

std::vector<DeviceFieldInfo> AZBKDisk::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"csr",      "0177220 as read: 200 ready, 100000 error",          false});
    r.push_back({"command",  "Last command taken",                                 false});
    r.push_back({"unit",     "Selected unit, or 177777 when none",                false});
    r.push_back({"block",    "Block number",                                       false});
    r.push_back({"busy",     "1 while a long command runs",                        false});
    r.push_back({"commands", "Commands taken since power-on",                      false});
    r.push_back({"refused",  "Accesses refused while busy (trap to 4)",           false});
    r.push_back({"trace",    "The last 64 command words written to 0177220",       false});
    r.push_back({"eeprom",   "Non-volatile memory words from-to",                  true});
    r.push_back({"rtc",      "Clock: year, month, day, hour, minute, second",      false});
    r.push_back({"vector",   "Vector offered to the processor, or 0",              false});
    return r;
}

bool AZBKDisk::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "csr")      { out.values.push_back((m_done ? CSR_DONE : 0) | (m_done && m_err ? CSR_ERR : 0)); return true; }
    if (field == "command")  { out.width = 8; out.values.push_back(m_command); return true; }
    if (field == "unit")     { out.values.push_back(m_unit < 0 ? 0177777u : (unsigned int)m_unit); return true; }
    if (field == "block")    { out.width = 32; out.values.push_back(m_block); return true; }
    if (field == "busy")     { out.width = 1; out.values.push_back((m_busy || !m_done) ? 1 : 0); return true; }
    if (field == "commands") { out.width = 32; out.values.push_back(m_commands); return true; }
    if (field == "refused")  { out.width = 32; out.values.push_back(m_refused); return true; }
    if (field == "vector")   { out.values.push_back(m_irq.offered()); return true; }
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
