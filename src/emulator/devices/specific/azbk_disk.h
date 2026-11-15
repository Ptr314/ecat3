// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the AZ disk controller, header

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/core.h"
#include "emulator/devices/common/virq_line.h"
#include "azbk_sdcard.h"

class AZBK;
class HddUnit;

// Дисковый контроллер AZ (микроконтроллер STM32 платы AZBK) по описанию
// MAXIOL «API контроллеров AZ» (azwiki.maxiol.com): 177220 CSR - команда в
// разрядах 0-5 и разрешение прерывания 6 (пишутся только словом, читаются
// нулем), готовность 7 и ошибка 15; 177222 DR - аргумент и поток слов
// буферов; 177224/177226 - регистры загрузки. Пока контроллер занят («думаю,
// прошу не мешать»), DR и регистры загрузки не отвечают (trap 4), а команды
// он не принимает - кроме 030, которая только ставит разрешение прерывания.
// «Длительная» команда по окончании дает прерывание 0174, если оно разрешено.
//
// Псевдодиски AZ0..AZ31 - устройства hdd-unit, образы из блоков по 512 байт.
// Энергонезависимая память - 255 слов (их пишет SETUP), часы - от rtc_start
// по эмулируемому времени. Сети нет. Команда 037 перезапускает плату (azbk)
// и БК: импульс на выходе dclo.
//
// Карта microSD - гнездо azbk-sdcard (azbk_sdcard.h): оглавление (003, 013),
// монтирование ее файла на AZn и снятие (004, 014), чтение и запись файлов
// (050-055), файл в память AZ (047), объем (056, 057), снимок экрана в файл
// (044). Без карты эти команды кончаются ошибкой. Файловые команды и
// энергонезависимая память работают с одним буфером на 256 слов (022, 023).
// При включении и по команде 037 диски берутся из раздела [DISKS] файла AZ.INI
// карты, как у платы: перечисленные приводы монтируются с карты, смонтированные
// с нее и не перечисленные снимаются, образы конфигурации и окна на приводах,
// которых AZ.INI не называет, остаются.
//
// Параметры:
//   board        - устройство azbk
//   units        - псевдодиски, hdd-unit через |: AZ0, AZ1, ...
//   stm_version  - старший байт первого слова команды 027 (версия прошивки STM32)
//   block_us     - чтение и запись блока на карте, мкс (MAXIOL: 500-800)
//   command_us   - прочие «длительные» команды, мкс
//   restart_ms   - сколько команда 037 держит БК сбросом, мс
//   rtc_start    - время часов при включении, "ГГГГ-ММ-ДД ЧЧ:ММ:СС"
//   eeprom       - начальное содержимое энергонезависимой памяти (файл 510
//                  байт), только чтение; без него память не записана
//   card         - гнездо карты microSD, azbk-sdcard; нет - нет и карты
//   ~virq_in, ~vector_in - запрос и вектор клавиатуры БК: она ближе к
//                  процессору, ее запрос идет раньше своего; ~iako -
//                  подтверждение прерывания
class AZBKDisk: public AddressableDevice, public AzSdListener
{
public:
    static const unsigned int UNITS = 32;
    static const unsigned int EEPROM_WORDS = 255;
    static const unsigned int NAME_BYTES = 386;         // имя в таблице назначений с v17
    static const unsigned int ENTRY_WORDS = (10 + NAME_BYTES) / 2;

private:
    AZBK * m_board = nullptr;
    MemoryMapper * m_mapper = nullptr;
    HddUnit * m_units[UNITS]{};

    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_dclo;

    unsigned int m_stm_version = 0;
    unsigned int m_block_us = 650;
    unsigned int m_command_us = 20;
    unsigned int m_restart_ms = 100;
    uint64_t m_rtc_start = 0;           // секунды от 1970 года при включении

    // Состояние
    bool m_done = true;
    bool m_err = false;
    bool m_ie = false;
    bool m_network = false;             // 010 с IE: обслуживает сеть до сброса
    bool m_irq_pending = false;
    unsigned int m_arg = 0;             // последнее записанное в DR
    unsigned int m_command = 0;         // последняя принятая команда
    int m_unit = -1;
    uint32_t m_block = 0;
    bool m_block_low = false;           // была 002: можно 012

    // Отложенное окончание команды
    unsigned int m_busy_cmd = 0;
    bool m_busy = false;
    uint64_t m_busy_us = 0;             // осталось
    uint64_t m_us_acc = 0;              // такты * 1000000 в счет микросекунды
    uint64_t m_restart_left = 0;        // такты удержания БК командой 037

    uint64_t m_clocks = 0;              // такты с включения - время часов
    int64_t m_rtc_shift = 0;            // поправка часов командами 033/034

    // Буферы
    uint16_t m_iobuf[256]{};
    unsigned int m_iobuf_filled = 0;    // слов, принятых командой 016
    // Буфер энергонезависимой памяти и файловых команд: после 021 - слово
    // состояния и 255 слов памяти, 023 пишет его с начала
    uint16_t m_cmos[256]{};
    uint16_t m_eeprom[EEPROM_WORDS]{};
    bool m_eeprom_valid = false;
    uint16_t m_timestamp[14]{};
    uint16_t m_simple_in[7]{};
    uint16_t m_ipdata[12]{};
    enum { SRC_IOBUF, SRC_TABLE, SRC_DIRREC };
    unsigned int m_read_source = SRC_IOBUF;

    // Карта microSD: гнездо, без него - пустая карта
    AZBKSdCard * m_sd = nullptr;
    AzSdCard m_no_card;
    AzSdCard & card() { return m_sd != nullptr ? m_sd->card() : m_no_card; }
    bool m_iobuf_string = false;        // после сброса была 016: 014 берет строку
    std::vector<AzSdCard::Entry> m_dir; // открытое оглавление (003)
    bool m_dir_open = false;
    std::string m_dir_path;
    unsigned int m_dir_pos = 0;
    uint16_t m_dirrec[11]{};            // запись оглавления (013)
    std::string m_mount[UNITS];         // строка монтирования 004 ...
    std::string m_mount_file[UNITS];    // ... имя образа, который она поставила,
    std::string m_mount_path[UNITS];    // ... путь на карте
    unsigned int m_mount_writes[UNITS]{};   // ... и записи привода к тому времени
    // Файлы 050 (чтение) и 053 (запись) - разные, как у gid: копирование может
    // чередовать 052 и 055. 051 отвечает про последний открытый
    struct CardFile {
        bool open = false;
        std::string path;
        uint32_t size = 0;              // чтение - длина файла, запись - записано
        uint32_t pos = 0;               // прочитано или записано байт
        unsigned int result = 0;        // код FatFs открытия
    };
    CardFile m_rd;
    CardFile m_wr;
    enum { IO_NONE, IO_READ, IO_WRITE };
    unsigned int m_io_mode = IO_NONE;   // последней была 050 или 053
    uint16_t m_length_in[2]{};          // 054: длина файла на запись
    uint16_t m_sizecard[2]{};           // 056: объем и свободно, МБ

    // Поток слов через DR
    std::vector<uint16_t> m_out;
    unsigned int m_out_pos = 0;
    uint16_t m_last_out = 0;
    uint16_t * m_in = nullptr;
    unsigned int m_in_size = 0;
    unsigned int m_in_pos = 0;
    unsigned int m_in_target = 0;       // куда идет поток: IN_*
    enum { IN_NONE, IN_IOBUF, IN_EEPROM, IN_SIMPLE, IN_LENGTH };

    // Для скриптов
    unsigned int m_commands = 0;
    unsigned int m_refused = 0;
    std::vector<uint16_t> m_trace;

    bool refused();
    void command(unsigned int value);
    void start_long(unsigned int cmd, uint64_t us);
    void finish_long();
    void finish();
    void stream_out(const uint16_t * data, unsigned int count);
    void stream_in(unsigned int target, uint16_t * data, unsigned int count);
    void update_irq();
    void reset_controller();
    void build_table(std::vector<uint16_t> &out) const;
    HddUnit * unit() const;
    uint64_t now_seconds() const;
    void fill_timestamp(uint64_t seconds);
    void fat_now(uint16_t &date, uint16_t &time) const;

    std::string buffer_string(const uint16_t * buffer, unsigned int max_bytes) const;
    void put_cmos_string(const std::string &s);
    bool card_command(unsigned int cmd);
    void open_dir();
    void read_dir();
    void mount();
    void unmount();
    bool card_mounted(unsigned int n) const;
    bool mount_unit(unsigned int n, const std::string &given);
    void drop_unit(unsigned int n);
    void apply_az_ini();
    void open_read();
    void read_file_block();
    void open_write();
    void write_file_block();
    void read_into_memory();
    struct Region { uint32_t word; uint32_t words; };
    bool screen_plan(std::vector<Region> &regions, std::string &error) const;
    void screenshot();

public:
    AZBKDisk(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    unsigned int clocks_to_event() const override;
    bool has_clock_events() const override { return true; }
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    void card_changed(bool replaced) override;
    void state_restored() override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_azbk_disk(InterfaceManager *im, EmulatorConfigDevice *cd);
