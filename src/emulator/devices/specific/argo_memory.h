// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Арго ФВ-6511: диспетчер памяти и пересылки ПДП

#pragma once

#include "emulator/core.h"

class I8257;

#define ARGO_LOG_SIZE 16

// Регистров конфигурации памяти четыре, и лежат они в одной микросхеме -
// регистровом файле К555ИР26 (D6) на четыре четырехразрядных ячейки. Ячейку для
// записи выбирают разряды A3 и A4 адреса порта ($A1, $A9, $B1, $B9), а
// действующую - линии /BUSAK и /DRQ0: у процессора своя, у канала 0 ПДП своя, у
// остальных каналов своя. Поэтому программы пишут конфигурацию процессора сразу
// в оба его порта, $B1 и $B9 (загрузчик TCP/M, $0105): какая из двух ячеек
// действует, зависит от того, что в этот миг на линии /DRQ0.
//
// На двух картах памяти и держится пересылка: МОНИТОР дает одному каналу чистое
// $61, другому $61 с номером блока в разрядах 1-3 и переливает данные между
// двумя по-разному отображенными пространствами ($FA18 и $FA36, они же точки
// входа $F818 и $F81B).
//
// Отображение у процессора и у канала считается по-разному, и это не упрощение,
// а то, что следует из машины:
//
//   - у процессора работает прошивка D42 (MEMCFG, К155РЕ3 на 32 байта со
//     схемы). Адресуют ее два старших разряда адреса и три разряда
//     конфигурации, в ответе - новые старшие разряды адреса и выбор банки
//     основных 128 Кбайт. Строка для $61 - точное отображение первой банки,
//     строка для $63 оставляет страницы 0 и 1 на месте, а страницу 3 отдает
//     второй банке: это и есть ОЗУ знакогенератора, которое МОНИТОР заполняет
//     по $F800 ($FC56-$FC81). Что страницы 0 и 1 при этом не двигаются -
//     проверяется сразу тремя местами загрузчика TCP/M ($0105, $39AB, $7804):
//     он ставит ту же конфигурацию значением $43 и продолжает выполняться из
//     ОЗУ по этим самым страницам;
//
//   - у канала ПДП номер блока выбирает банку дополнительного ОЗУ целиком,
//     нулевой номер - основную память. По прошивке так не выходит: строк в ней
//     восемь, и среди них нет семи разных мест под блоки TCP/M - блок 1 лег бы
//     на страницу 0 основной памяти, и первая же пересылка квазидиска затерла
//     бы саму систему. Значит, номер блока разбирает сама плата расширения,
//     помимо D42. Проверить это не по чему: листа с дополнительным ОЗУ в
//     имеющейся схеме нет.
//
// Сама пересылка здесь же: ВТ57 в eCat3 не умеет память-память, а машина ничем
// другим в дополнительную память и не ходит. Включение каналов 0 и 1 вместе -
// это и есть команда "перешли", и на живой машине процессор стоит, пока ПДП не
// отдаст шину: МОНИТОР пишет в $A9 сразу за командой пуска ($FA66-$FA6A) и
// рассчитывает, что к этой команде все уже переслано
class ArgoMemory: public AddressableDevice
{
private:
    AddressableDevice * Memory = nullptr;   // Основная память и все блоки
    AddressableDevice * Map = nullptr;      // Прошивка MEMCFG, 32 байта
    I8257 * DMA = nullptr;

    // Ячейки регистрового файла, действующие у процессора и у каналов
    Interface i_cpu;
    Interface i_dma0;
    Interface i_dma1;

    unsigned int m_cfg_bits[3]  = {1, 3, 2};    // Разряды значения -> индекс
    unsigned int m_page_bits[2] = {0, 1};       // Разряды ответа -> страница
    unsigned int m_bank_bit = 7;                // Разряд ответа -> банка
    bool m_bank_invert = true;                  // 1 в нем - первая банка

    unsigned int m_block_mask = 0x0E;           // Разряды значения с номером
    unsigned int m_block_shift = 1;
    unsigned int m_blocks = 8;                  // Блоков, считая основной
    unsigned int m_ext_base = 0x20000;          // Где начинается блок 1

    // Отображение процессора считается на каждое обращение к памяти, поэтому
    // четыре его страницы держатся посчитанными: пока значение в ячейке не
    // изменилось, обращение стоит сравнения и сложения
    mutable unsigned int m_cpu_value = 0;
    mutable unsigned int m_cpu_pages[4] = {0, 0, 0, 0};
    mutable bool m_cpu_valid = false;

    // Журнал последних пересылок: по нему их и разбирают - какой блок стоял у
    // каждого канала, откуда, куда, сколько и куда это легло на самом деле
    struct LogEntry { unsigned int blk0, blk1, addr0, addr1, count, phys0, phys1; };
    LogEntry m_log[ARGO_LOG_SIZE];
    unsigned int m_log_count = 0;

    uint64_t m_transfers = 0;
    uint64_t m_bytes = 0;
    unsigned int m_last_count = 0;

    emulator::Result read_bits(EmulatorConfigDevice *cd, const std::string &name,
                               unsigned int *bits, unsigned int count);
    unsigned int config_index(unsigned int value) const;
    unsigned int block_of(const Interface &i) const;
    unsigned int translate(unsigned int address, unsigned int value) const;
    unsigned int translate_cpu(unsigned int address) const;
    unsigned int translate_dma(unsigned int address, unsigned int block) const;
    void run_transfer();

public:
    ArgoMemory(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    void state_restored() override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_argo_memory(InterfaceManager *im, EmulatorConfigDevice *cd);
