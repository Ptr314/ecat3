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
//   ~virq_in, ~vector_in - запрос и вектор клавиатуры БК: она ближе к
//                  процессору, ее запрос идет раньше своего; ~iako -
//                  подтверждение прерывания
class AZBKDisk: public AddressableDevice
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
    uint16_t m_cmos[256]{};             // слово состояния + 255 слов
    uint16_t m_eeprom[EEPROM_WORDS]{};
    bool m_eeprom_valid = false;
    uint16_t m_eeprom_in[EEPROM_WORDS]{};
    uint16_t m_timestamp[14]{};
    uint16_t m_simple_in[7]{};
    uint16_t m_ipdata[12]{};
    enum { SRC_IOBUF, SRC_TABLE };
    unsigned int m_read_source = SRC_IOBUF;

    // Поток слов через DR
    std::vector<uint16_t> m_out;
    unsigned int m_out_pos = 0;
    uint16_t m_last_out = 0;
    uint16_t * m_in = nullptr;
    unsigned int m_in_size = 0;
    unsigned int m_in_pos = 0;
    unsigned int m_in_target = 0;       // куда идет поток: IN_*
    enum { IN_NONE, IN_IOBUF, IN_EEPROM, IN_SIMPLE };

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

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_azbk_disk(InterfaceManager *im, EmulatorConfigDevice *cd);
