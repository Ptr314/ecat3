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

// Дисковый контроллер AZ - микроконтроллер STM32 платы AZBK. Описание MAXIOL
// «API контроллеров AZ» (azwiki.maxiol.com), сверенное с его прошивкой 00019
// (AZBK_00019.hex; разбор - docs/dev/machines.md): 177220 CSR - команда в
// разрядах 0-5 и разрешение прерывания 6 (пишутся только словом), чтение -
// готовность 7, ошибка 15 и BIG 14 (007 по диску от 65536 блоков); 177222 DR;
// 177224/177226 - регистры загрузки. Пока контроллер занят («думаю, прошу не
// мешать»), DR и регистры загрузки не отвечают (trap 4), а команды он не
// принимает - кроме 030, которая только ставит разрешение прерывания.
// «Длительная» команда по окончании дает прерывание 0174, если оно разрешено.
//
// DR - два независимых автомата, как в прошивке. Чтение: поток буфера (за его
// концом - ошибка, и поток кончается), короткий ответ команды (007, 017, 020,
// 027) или, вне потока, слово ошибки: код последней неудачной команды
// (0x8600 - нет такого привода, 0x8C00 - привод пуст, 0x8D00 - блок за концом
// образа, 0x8B00 - оглавление не открыто) или итог 003, 004, 013, 014 - у них
// разряда 15 нет, итог только в слове ошибки. Запись: поток в буфер (016, 023,
// 033, 054; лишнее слово - ошибка) живет до сброса 000 или команды, которая
// сбрасывает автоматы, вне потока - аргумент следующей команды.
//
// Псевдодиски AZ0..AZ31 - устройства hdd-unit, образы из блоков по 512 байт.
// Энергонезависимая память - 255 слов (их пишет SETUP), часы - от rtc_start
// по эмулируемому времени. Сети нет: 043 отдает MAC платы (из AZ.INI), адреса и
// часы стека TCP/IP - нули. Команда 037 перезапускает плату (azbk), БК
// (импульс на выходе dclo) и сам контроллер: тест памяти AZ, каталоги карты,
// AZ.INI - как при включении.
//
// Карта microSD - гнездо azbk-sdcard (azbk_sdcard.h): оглавление (003, 013),
// монтирование ее файла на AZn и снятие (004, 014), файл (050-055: один на
// чтение и запись), файл в память AZ (047), объем (056, 057), снимок экрана в
// файл (044). Без карты эти команды кончаются ошибкой (на плате без карты нет и
// AZ.INI - она не стартует). Файловые команды и энергонезависимая память
// работают с одним буфером на 256 слов (022, 023). При включении и по команде
// 037 диски берутся из раздела [DISKS] файла AZ.INI карты, как у платы:
// перечисленные приводы монтируются с карты, смонтированные с нее и не
// перечисленные снимаются, образы конфигурации и окна на приводах, которых
// AZ.INI не называет, остаются.
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
    static const unsigned int DIRREC_WORDS = 12;        // FILINFO FatFs без длинных имен

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
    bool m_err = false;                 // разряд 15 CSR: до следующей команды
    bool m_big = false;                 // разряд 14 CSR
    bool m_ie = false;
    bool m_network = false;             // 010 с IE: обслуживает сеть до сброса
    bool m_irq_pending = false;
    unsigned int m_arg = 0;             // DR, записанный вне потока
    unsigned int m_command = 0;         // последняя принятая команда
    unsigned int m_unit = 0;            // как в DR команды 001, и 32 и дальше
    uint32_t m_block = 0;
    bool m_block_low = false;           // была 002: можно 012
    uint16_t m_errword = 0;             // слово ошибки: DR вне потока
    uint16_t m_status = 0;              // итог старта; 014 копит в нем ошибки разбора

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
    // Буфер энергонезависимой памяти и файловых команд: после 021 - слово
    // состояния и 255 слов памяти, 023 пишет его с начала
    uint16_t m_cmos[256]{};
    uint16_t m_eeprom[EEPROM_WORDS]{};
    bool m_eeprom_valid = false;
    uint16_t m_timestamp[14]{};
    uint16_t m_simple_in[7]{};
    uint16_t m_ipdata[12]{};
    uint16_t m_dirrec[DIRREC_WORDS]{};  // запись оглавления (013)
    // Длина файла 050-055, 047: длина файла на чтение (050), 0 (053) или длина
    // на запись (054); 051 отдает ее
    uint16_t m_length[2]{};
    uint16_t m_sizecard[2]{};           // 056: объем и свободно, МБ
    uint8_t m_mac[6]{};                 // MAC платы для 043: из AZ.INI
    // Что отдает 015: буфер последней из 005, 011, 013, 016, 023, 033, 054 и
    // длина из последней из 005, 011, 013
    enum { BUF_IOBUF, BUF_TABLE, BUF_DIRREC, BUF_CMOS, BUF_SIMPLE, BUF_LENGTH };
    unsigned int m_buf = BUF_IOBUF;
    unsigned int m_buf_len = 0;

    // Карта microSD: гнездо, без него - пустая карта
    AZBKSdCard * m_sd = nullptr;
    AzSdCard m_no_card;
    AzSdCard & card() { return m_sd != nullptr ? m_sd->card() : m_no_card; }
    std::vector<AzSdCard::Entry> m_dir; // открытое оглавление (003)
    bool m_dir_open = false;            // после удачной 003 - до перезапуска
    bool m_dir_valid = false;           // последняя 003 удалась
    std::string m_dir_path;
    unsigned int m_dir_pos = 0;
    std::string m_mount[UNITS];         // строка монтирования 004 ...
    std::string m_mount_file[UNITS];    // ... имя образа, который она поставила,
    std::string m_mount_path[UNITS];    // ... путь на карте
    unsigned int m_mount_writes[UNITS]{};   // ... и записи привода к тому времени
    // Файл 050 (чтение) или 053 (запись) - один, как у прошивки
    struct CardFile {
        bool open = false;
        bool write = false;
        std::string path;
        uint32_t size = 0;              // длина файла на чтение при открытии
        uint32_t pos = 0;               // прочитано или записано байт
    };
    CardFile m_file;

    // Чтение DR: поток буфера, короткий ответ, ответ, который повторяет
    // последнее слово (027), или слово ошибки
    enum { OUT_IDLE, OUT_STREAM, OUT_SEQ, OUT_STICKY };
    unsigned int m_out_mode = OUT_IDLE;
    std::vector<uint16_t> m_out;
    unsigned int m_out_pos = 0;
    uint16_t m_last_out = 0;
    // Запись DR: поток в буфер, пока его не сбросят
    uint16_t * m_in = nullptr;
    unsigned int m_in_size = 0;
    unsigned int m_in_pos = 0;          // за m_in_size - лишние слова
    unsigned int m_in_target = 0;       // куда идет поток: IN_*
    enum { IN_NONE, IN_IOBUF, IN_CMOS, IN_SIMPLE, IN_LENGTH };
    // Регистры загрузки: сколько слов отдано
    unsigned int m_boot1 = 0;
    unsigned int m_boot2 = 0;

    // Для скриптов
    unsigned int m_commands = 0;
    unsigned int m_refused = 0;
    std::vector<uint16_t> m_trace;

    bool refused();
    void command(unsigned int value);
    void start_long(unsigned int cmd, uint64_t us);
    void finish_long();
    void finish();
    void clear_regs();
    void stream_out(const uint16_t * data, unsigned int count, unsigned int mode);
    void stream_in(unsigned int target, uint16_t * data, unsigned int count);
    void stream_buffer();
    void zero_rest_of_input();
    uint16_t dr_read();
    uint16_t dr_peek() const;
    uint16_t boot_read(bool second, bool peek);
    bool mounted();
    uint32_t length() const { return (uint32_t)m_length[0] | ((uint32_t)m_length[1] << 16); }
    void set_length(uint32_t v) { m_length[0] = (uint16_t)v; m_length[1] = (uint16_t)(v >> 16); }
    unsigned int csr() const;
    void update_irq();
    void reset_controller();
    void stm_start();
    void fill_words(uint32_t word, uint32_t count, uint16_t value);
    void build_table(std::vector<uint16_t> &out) const;
    HddUnit * unit() const;
    uint64_t now_seconds() const;
    void fill_timestamp(uint64_t seconds);
    void fat_now(uint16_t &date, uint16_t &time) const;

    std::string buffer_string(const uint16_t * buffer, unsigned int max_bytes) const;
    void put_cmos_string(const std::string &s);
    bool card_command(unsigned int cmd);
    uint32_t read_file(uint32_t pos, uint8_t * buffer, uint32_t count);
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
    void screen_plan(std::vector<Region> &regions) const;
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
