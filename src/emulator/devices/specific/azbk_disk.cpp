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
    // Микроконтроллер начинает сначала: оглавление и файл закрыты
    m_iobuf_string = false;
    m_dir_open = false;
    m_dir.clear();
    m_io_mode = IO_NONE;
    m_rd = CardFile();
    m_wr = CardFile();
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
    // Включение: контроллер читает AZ.INI карты
    if (cold) apply_az_ini();
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
        const std::string &file = const_cast<HddUnit*>(u)->hdd_image().file_name();
        // Смонтированный командой 004 - той строкой, какой его монтировали,
        // пока в приводе тот же образ (из снимка состояния он приходит копией
        // под тем же именем)
        const std::string n = (!m_mount[i].empty()
                               && str_tolower(dsk_tools::get_filename(m_mount_file[i])) == str_tolower(dsk_tools::get_filename(file)))
                            ? m_mount[i] : "0:/" + dsk_tools::get_filename(file);
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
            m_iobuf_string = false;
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
    case 000:   // сброс: оглавление, файл и буферы остаются
        m_out.clear();
        m_in = nullptr;
        m_in_target = IN_NONE;
        m_read_source = SRC_IOBUF;
        m_iobuf_string = false;
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
        } else if (m_read_source == SRC_DIRREC)
            stream_out(m_dirrec, 11);
        else
            stream_out(m_iobuf, 256);
        break;
    case 016:   // прием блока в буфер
        m_iobuf_filled = 0;
        m_iobuf_string = true;
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
    case 023:   // в буфер с начала: память для 024, имя или данные файла
        stream_in(IN_EEPROM, m_cmos, 256);
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
    case 037:   // перезапуск платы и всей БК: контроллер снова читает AZ.INI
        m_board->restart();
        reset_controller();
        apply_az_ini();
        m_restart_left = (uint64_t)m_restart_ms * m_system_clock / 1000;
        if (m_restart_left == 0) m_restart_left = 1;
        i_dclo.change(0);
        return;
    case 041:
        stream_out(m_ipdata, 12);
        break;
    case 003: case 004: case 013: case 014: case 044: case 047:
    case 050: case 051: case 052: case 053: case 054: case 055: case 056: case 057:
        if (card_command(cmd)) return;
        break;
    default:
        // Hall of Fame (025, 026) - нет
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
        memcpy(m_eeprom, m_cmos, sizeof(m_eeprom));
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

// "Dnn" строки монтирования (AZ.INI): номер привода десятичный
static bool parse_unit(const std::string &s, size_t &i, unsigned int &unit)
{
    while (i < s.size() && s[i] == ' ') i++;
    if (i >= s.size() || (s[i] != 'D' && s[i] != 'd')) return false;
    i++;
    unsigned int n = 0, digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 3) {
        n = n * 10 + (unsigned int)(s[i] - '0');
        i++;
        digits++;
    }
    while (i < s.size() && s[i] == ' ') i++;
    unit = n;
    return digits > 0;
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
    case 051: {     // про последний из 050 и 053: длина файла на чтение или код FatFs с разрядом 31
        if (m_io_mode == IO_NONE) {
            m_err = true;
            return false;
        }
        const CardFile &f = (m_io_mode == IO_READ) ? m_rd : m_wr;
        uint16_t w[2] = { 0, 0 };
        if (f.result != AzSdCard::FR_OK) {
            w[0] = (uint16_t)f.result;
            w[1] = 0100000;
        } else if (m_io_mode == IO_READ) {
            w[0] = (uint16_t)f.size;
            w[1] = (uint16_t)(f.size >> 16);
        }
        stream_out(w, 2);
        return false;
    }
    case 054:       // длина файла на запись: два слова через DR
        m_length_in[0] = m_length_in[1] = 0;
        stream_in(IN_LENGTH, m_length_in, 2);
        return false;
    case 057:
        stream_out(m_sizecard, 2);
        return false;
    case 052:
    case 055:
        start_long(cmd, m_block_us);
        return true;
    case 047: {     // весь файл: по блоку карты на 512 байт
        const uint64_t blocks = m_rd.open ? (m_rd.size + 511) / 512 : 0;
        start_long(cmd, m_command_us + blocks * m_block_us);
        return true;
    }
    case 044: {
        std::vector<Region> regions;
        std::string error;
        uint64_t words = 0;
        if (screen_plan(regions, error))
            for (size_t i = 0; i < regions.size(); i++) words += regions[i].words;
        start_long(cmd, m_command_us + (words * 2 + 1024 + 511) / 512 * m_block_us);
        return true;
    }
    default:        // 003, 004, 013, 014, 050, 053, 056
        start_long(cmd, m_command_us);
        return true;
    }
}

// 003: путь оглавления - строка в буфере блока (016), до 384 байт
void AZBKDisk::open_dir()
{
    m_dir_open = false;
    m_dir.clear();
    m_dir_pos = 0;
    std::string path;
    if (AzSdCard::parse(buffer_string(m_iobuf, 384), path) != AzSdCard::FR_OK
        || card().list(path, m_dir) != AzSdCard::FR_OK) {
        m_err = true;
        return;
    }
    m_dir_path = path;
    m_dir_open = true;
}

// 013: запись оглавления, 11 слов для 015 - размер (2 слова), дата и время
// FAT, байт атрибутов, имя "ИМЯ.ТИП" с нулем (13 байт). За последней записью -
// запись с пустым именем, как у f_readdir FatFs
void AZBKDisk::read_dir()
{
    memset(m_dirrec, 0, sizeof(m_dirrec));
    m_read_source = SRC_DIRREC;
    if (!m_dir_open) {
        m_err = true;
        return;
    }
    if (m_dir_pos >= m_dir.size()) return;
    const AzSdCard::Entry &e = m_dir[m_dir_pos++];
    uint8_t rec[22] = {};
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
    for (unsigned int w = 0; w < 11; w++) m_dirrec[w] = (uint16_t)(rec[w * 2] | (rec[w * 2 + 1] << 8));
}

// 004: строка как в AZ.INI, "D04=0:/DISKS/A.IMG", в буфере блока. Привод
// должен быть свободен. Монтирование только до перезапуска эмулятора: в AZ.INI
// карты оно не пишется (пишет ли его туда плата, MAXIOL не описал)
void AZBKDisk::mount()
{
    const std::string s = buffer_string(m_iobuf, NAME_BYTES);
    size_t i = 0;
    unsigned int n = 0;
    if (!parse_unit(s, i, n) || i >= s.size() || s[i] != '=') {
        m_err = true;
        return;
    }
    if (n >= UNITS || m_units[n] == nullptr || m_units[n]->attached()
        || !mount_unit(n, s.substr(i + 1)))
        m_err = true;
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
    if (card_mounted(n) && !card().writes_host() && u->writes() != m_mount_writes[n]) {
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

// Раздел [DISKS] файла AZ.INI карты: "Dnn=путь" (D0, D04, ...), как у MAXIOL.
// Привод, который AZ.INI называет, получает этот файл; уже стоящий там по той же
// строке остается (записанное в него не теряется). Смонтированный с карты и
// не названный - снимается. Образ конфигурации или окна на приводе, которого
// AZ.INI не называет, остается: настоящей плате его взять неоткуда. Привод,
// которого у машины нет (D4 и дальше), и файл, которого нет на карте,
// пропускаются: привод остается пустым
void AZBKDisk::apply_az_ini()
{
    AzSdCard &c = card();
    uint32_t size = 0;
    if (!c.present() || c.file_size("AZ.INI", size) != AzSdCard::FR_OK || size == 0 || size > 65536) return;
    std::vector<uint8_t> raw(size);
    if (!c.read("AZ.INI", 0, raw.data(), size)) return;
    std::map<unsigned int, std::string> disks;
    std::string section;
    size_t pos = 0;
    const std::string text(raw.begin(), raw.end());
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = str_trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            section = str_tolower(str_trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
            continue;
        }
        if (section != "disks") continue;
        size_t i = 0;
        unsigned int n = 0;
        if (!parse_unit(line, i, n) || i >= line.size() || line[i] != '=' || n >= UNITS) continue;
        disks[n] = collapse_separators(str_trim(line.substr(i + 1)));
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

// 014: привод - строкой "D04" в буфере блока, как у AZUMNT MAXIOL, или
// номером в DR, как в вики. Ошибка, если на нем ничего нет. Без записи на
// диск компьютера записанное машиной на смонтированном с карты образе
// остается на карте - в ее памяти, как файл (до 32 МБ)
void AZBKDisk::unmount()
{
    unsigned int n = m_arg & 0177777;
    if (m_iobuf_string) {
        const std::string s = buffer_string(m_iobuf, NAME_BYTES);
        size_t i = 0;
        if (!parse_unit(s, i, n)) {
            m_err = true;
            return;
        }
    }
    if (n >= UNITS || m_units[n] == nullptr || !m_units[n]->attached()) {
        m_err = true;
        return;
    }
    drop_unit(n);
}

// 050: имя файла - строка в буфере 023, до 256 байт. Ошибку открытия отдает 051
void AZBKDisk::open_read()
{
    m_io_mode = IO_READ;
    m_rd = CardFile();
    std::string path;
    int result = AzSdCard::parse(buffer_string(m_cmos, 256), path);
    if (result == AzSdCard::FR_OK) result = card().file_size(path, m_rd.size);
    m_rd.result = (unsigned int)result;
    m_rd.open = (result == AzSdCard::FR_OK);
    m_rd.path = path;
}

// 052: следующие 512 байт файла в буфер (последний блок дополнен нулями);
// прочитанный до конца файл закрывается
void AZBKDisk::read_file_block()
{
    if (!m_rd.open || m_rd.pos >= m_rd.size) {
        m_rd.open = false;
        m_err = true;
        return;
    }
    uint8_t block[512] = {};
    const uint32_t n = std::min<uint32_t>(512, m_rd.size - m_rd.pos);
    if (!card().read(m_rd.path, m_rd.pos, block, n)) {
        m_rd.open = false;
        m_err = true;
        return;
    }
    for (unsigned int i = 0; i < 256; i++) m_cmos[i] = (uint16_t)(block[i * 2] | (block[i * 2 + 1] << 8));
    m_rd.pos += n;
    if (m_rd.pos >= m_rd.size) m_rd.open = false;
}

// 053: файл создается (или обрезается) сразу; 051 отдает 0, 0 или код FatFs
void AZBKDisk::open_write()
{
    m_io_mode = IO_WRITE;
    m_wr = CardFile();
    std::string path;
    int result = AzSdCard::parse(buffer_string(m_cmos, 256), path);
    if (result == AzSdCard::FR_OK) {
        uint16_t date, time;
        fat_now(date, time);
        result = card().create(path, date, time);
    }
    m_wr.result = (unsigned int)result;
    m_wr.open = (result == AzSdCard::FR_OK);
    m_wr.path = path;
}

// 055: до 512 байт буфера в файл, сколько осталось до длины 054; на ней файл
// закрывается
void AZBKDisk::write_file_block()
{
    const uint32_t declared = (uint32_t)m_length_in[0] | ((uint32_t)m_length_in[1] << 16);
    if (!m_wr.open || m_wr.pos >= declared) {
        m_wr.open = false;
        m_err = true;
        return;
    }
    uint8_t block[512];
    for (unsigned int i = 0; i < 256; i++) {
        block[i * 2] = (uint8_t)m_cmos[i];
        block[i * 2 + 1] = (uint8_t)(m_cmos[i] >> 8);
    }
    const uint32_t n = std::min<uint32_t>(512, declared - m_wr.pos);
    if (!card().append(m_wr.path, block, n)) {
        m_wr.open = false;
        m_err = true;
        return;
    }
    m_wr.pos += n;
    m_wr.size = m_wr.pos;
    if (m_wr.pos >= declared) m_wr.open = false;
}

// 047: файл 050 целиком в память AZ со словного адреса из буфера (младшее,
// старшее слово). Состояние - в буфере и сразу в DR: длина или код FatFs с
// разрядом 31 (9 - файл не открыт, 19 - не помещается в 32 МБ)
void AZBKDisk::read_into_memory()
{
    const uint32_t word = (uint32_t)m_cmos[0] | ((uint32_t)m_cmos[1] << 16);
    unsigned int code = AzSdCard::FR_OK;
    if (!m_rd.open)
        code = 9;
    else if ((uint64_t)word * 2 + m_rd.size > m_board->memory_size())
        code = 19;
    else if (m_rd.size > 0) {
        const uint64_t byte = (uint64_t)word * 2;
        if (!card().read(m_rd.path, 0, m_board->memory() + byte, m_rd.size)) code = AzSdCard::FR_DISK_ERR;
        for (uint64_t p = byte / AZBK::PAGE_BYTES; p <= (byte + m_rd.size - 1) / AZBK::PAGE_BYTES; p++)
            m_board->touch_page((unsigned int)p);
    }
    const uint32_t size = m_rd.size;
    m_rd.open = false;
    memset(m_cmos, 0, sizeof(m_cmos));
    if (code != AzSdCard::FR_OK) {
        m_err = true;
        m_cmos[0] = (uint16_t)code;
        m_cmos[1] = 0100000;
    } else {
        m_cmos[0] = (uint16_t)size;
        m_cmos[1] = (uint16_t)(size >> 16);
    }
    stream_out(m_cmos, 256);
}

// Что снимает 044 - по заголовку в странице AZ 076 (вики MAXIOL): метка 0240,
// затем адрес и длина в словах (по два слова), затем начальная страница и
// число страниц, затем копии 177300-177352 и 177230-177256, палитра. Есть
// длина - участок по адресу; нет - страницы; нет и их - картина по копиям
// регистров видео: рулон слоя 0, у слоеного режима - всех трех слоев (как
// MAXIOL считает рулон здесь, он не описал). Не больше 2 МБ
bool AZBKDisk::screen_plan(std::vector<Region> &regions, std::string &error) const
{
    regions.clear();
    const uint8_t * mem = m_board->memory();
    const uint32_t base = 076 * AZBK::PAGE_BYTES;
    if (mem == nullptr || base + 1024 > m_board->memory_size()) {
        error = "ERROR memory";
        return false;
    }
    uint16_t h[40];
    for (unsigned int i = 0; i < 40; i++) h[i] = (uint16_t)(mem[base + i * 2] | (mem[base + i * 2 + 1] << 8));
    if (h[0] != 0240) {
        error = "ERROR tag";
        return false;
    }
    const uint32_t page_words = AZBK::PAGE_BYTES / 2;
    const uint32_t address = (uint32_t)h[1] | ((uint32_t)h[2] << 16);
    const uint32_t length = (uint32_t)h[3] | ((uint32_t)h[4] << 16);
    if (length != 0) {
        regions.push_back(Region{address, length});
    } else if (h[6] != 0) {
        regions.push_back(Region{(uint32_t)h[5] * page_words, (uint32_t)h[6] * page_words});
    } else {
        const unsigned int mode = h[29];
        unsigned int roll = (mode >> 12) & 15;
        if (roll > 11) roll = 11;
        const uint32_t words = AZBKVga::ROLL_WORDS[roll];
        regions.push_back(Region{(uint32_t)h[30] * page_words, words});
        if (mode & 4) {
            regions.push_back(Region{(uint32_t)h[31] * page_words, words});
            regions.push_back(Region{(uint32_t)h[32] * page_words, words});
        }
    }
    uint64_t total = 0;
    for (size_t i = 0; i < regions.size(); i++) {
        total += regions[i].words;
        if (((uint64_t)regions[i].word + regions[i].words) * 2 > m_board->memory_size()) {
            error = "ERROR address";
            return false;
        }
    }
    if (total > (1u << 20)) {
        error = "ERROR size";
        return false;
    }
    return true;
}

// 044: файл - первый килобайт страницы 076 и снятая память. Имя - строка в
// буфере 023; пустая - 0:/SCREENS/ДДЧЧММСС.SCR по часам платы. В буфер
// возвращается имя файла или "ERROR ..." (как у MAXIOL: "ERROR f_open 5" -
// нет каталога SCREENS)
void AZBKDisk::screenshot()
{
    std::string name = buffer_string(m_cmos, 256);
    if (name.empty()) {
        const uint64_t s = now_seconds();
        int y, mo, d;
        civil_from_days((int64_t)(s / 86400), y, mo, d);
        const unsigned int sod = (unsigned int)(s % 86400);
        char buf[40];
        std::snprintf(buf, sizeof(buf), "0:/SCREENS/%02d%02u%02u%02u.SCR", d, sod / 3600, (sod / 60) % 60, sod % 60);
        name = buf;
    }
    std::vector<Region> regions;
    std::string error, path;
    int result = AzSdCard::FR_OK;
    uint16_t date, time;
    fat_now(date, time);
    if (!screen_plan(regions, error)) {
    } else if ((result = AzSdCard::parse(name, path)) != AzSdCard::FR_OK || path.empty()
               || (result = card().create(path, date, time)) != AzSdCard::FR_OK) {
        error = "ERROR f_open " + std::to_string(result == AzSdCard::FR_OK ? AzSdCard::FR_INVALID_NAME : result);
    } else {
        const uint8_t * mem = m_board->memory();
        bool ok = card().append(path, mem + 076 * AZBK::PAGE_BYTES, 1024);
        for (size_t i = 0; ok && i < regions.size(); i++)
            ok = card().append(path, mem + (uint64_t)regions[i].word * 2, regions[i].words * 2);
        if (!ok) error = "ERROR f_write";
    }
    if (!error.empty()) {
        m_err = true;
        put_cmos_string(error);
        return;
    }
    put_cmos_string("0:/" + path);
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
    w.b("iobuf_string", m_iobuf_string);
    w.b("dir_open", m_dir_open);
    w.s("dir_path", m_dir_path);
    w.n("dir_pos", m_dir_pos);
    w.array("dirrec", m_dirrec, 11);
    for (unsigned int i = 0; i < UNITS; i++) {
        if (m_mount[i].empty()) continue;
        const std::string k = std::to_string(i);
        w.s(("mount_" + k).c_str(), m_mount[i]);
        w.s(("mount_file_" + k).c_str(), m_mount_file[i]);
        w.s(("mount_path_" + k).c_str(), m_mount_path[i]);
        w.n(("mount_writes_" + k).c_str(), m_mount_writes[i]);
    }
    w.n("io_mode", m_io_mode);
    const CardFile * files[2] = { &m_rd, &m_wr };
    const char * names[2] = { "rd", "wr" };
    for (unsigned int i = 0; i < 2; i++) {
        const std::string k = names[i];
        w.b((k + "_open").c_str(), files[i]->open);
        w.s((k + "_path").c_str(), files[i]->path);
        w.n((k + "_size").c_str(), files[i]->size);
        w.n((k + "_pos").c_str(), files[i]->pos);
        w.n((k + "_result").c_str(), files[i]->result);
    }
    w.array("length_in", m_length_in, 2);
    w.array("sizecard", m_sizecard, 2);
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
    // Снимок прежней сборки: 023 писала отдельный буфер, его берет 024
    r.array("eeprom_in", m_cmos, EEPROM_WORDS);
    r.array("eeprom", m_eeprom, EEPROM_WORDS);
    r.b("eeprom_valid", m_eeprom_valid);
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
    else if (m_in_target == IN_EEPROM && m_in_size <= 256) m_in = m_cmos;
    else if (m_in_target == IN_SIMPLE && m_in_size <= 7) m_in = m_simple_in;
    else if (m_in_target == IN_LENGTH && m_in_size <= 2) m_in = m_length_in;
    else m_in_target = IN_NONE;
    if (m_in_pos > m_in_size) m_in_pos = m_in_size;
    if (m_out_pos > m_out.size()) m_out_pos = (unsigned int)m_out.size();
    r.u("commands", m_commands);

    r.b("iobuf_string", m_iobuf_string);
    r.b("dir_open", m_dir_open);
    r.s("dir_path", m_dir_path);
    r.u("dir_pos", m_dir_pos);
    r.array("dirrec", m_dirrec, 11);
    // Пути карты из снимка - только такие, какие дал бы разбор пути гостя
    std::string check;
    if (AzSdCard::parse(m_dir_path, check) != AzSdCard::FR_OK || check != m_dir_path) m_dir_open = false;
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
    r.u("io_mode", m_io_mode);
    if (m_io_mode > IO_WRITE) m_io_mode = IO_NONE;
    CardFile * files[2] = { &m_rd, &m_wr };
    const char * names[2] = { "rd", "wr" };
    for (unsigned int i = 0; i < 2; i++) {
        const std::string k = names[i];
        *files[i] = CardFile();
        r.b((k + "_open").c_str(), files[i]->open);
        r.s((k + "_path").c_str(), files[i]->path);
        r.u((k + "_size").c_str(), files[i]->size);
        r.u((k + "_pos").c_str(), files[i]->pos);
        r.u((k + "_result").c_str(), files[i]->result);
        if (AzSdCard::parse(files[i]->path, check) != AzSdCard::FR_OK || check != files[i]->path) files[i]->open = false;
    }
    r.array("length_in", m_length_in, 2);
    r.array("sizecard", m_sizecard, 2);
    return emulator::Result::ok();
}

// Оглавление читается заново, когда восстановлено и гнездо карты: каталог мог
// измениться
void AZBKDisk::state_restored()
{
    AddressableDevice::state_restored();
    m_dir.clear();
    if (m_dir_open && (!card().present() || card().list(m_dir_path, m_dir) != AzSdCard::FR_OK)) m_dir_open = false;
    if (m_dir_pos > m_dir.size()) m_dir_pos = (unsigned int)m_dir.size();
}

// Карту вынули или вставили другую: оглавление и файлы закрыты, образы,
// смонтированные с нее, сняты (записанное в них пропадает вместе с картой).
// Сменилось, куда идет запись: файл на запись закрыт - его начало осталось
// там, куда шла запись
void AZBKDisk::card_changed(bool replaced)
{
    m_wr = CardFile();
    if (m_io_mode == IO_WRITE) m_io_mode = IO_NONE;
    if (!replaced) return;
    m_dir_open = false;
    m_dir.clear();
    m_dir_pos = 0;
    m_rd = CardFile();
    m_io_mode = IO_NONE;
    for (unsigned int i = 0; i < UNITS; i++) {
        // Образ, вставленный поверх кнопкой окна, остается
        if (card_mounted(i)) m_units[i]->detach();
        m_mount[i].clear();
        m_mount_file[i].clear();
        m_mount_path[i].clear();
    }
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
    r.push_back({"sd",       "1 if the microSD card is in",                       false});
    r.push_back({"file",     "Card files: last opened (1 - 050, 2 - 053); the read file open, bytes read, size; the write file open, bytes written", false});
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
    if (field == "sd")       { out.width = 1; out.values.push_back(card().present() ? 1 : 0); return true; }
    if (field == "file") {
        out.width = 32;
        out.values.push_back(m_io_mode);
        out.values.push_back(m_rd.open ? 1 : 0);
        out.values.push_back(m_rd.pos);
        out.values.push_back(m_rd.size);
        out.values.push_back(m_wr.open ? 1 : 0);
        out.values.push_back(m_wr.pos);
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
