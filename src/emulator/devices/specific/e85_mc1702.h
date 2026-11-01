// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Сопроцессор МС 1702 Электроники МС 0585: плата с К1810ВМ86 в позиции магистрали

#pragma once

#include <vector>

#include "emulator/core.h"
#include "emulator/devices/cpu/i8086.h"

// Плата - отдельная машина на 8086 (ТО 3.098.002): 512 КБ ОЗУ, ПЗУ на FC000,
// видеопамять 16 КБ на B8000. Устройств ввода-вывода у неё нет: цикл ввода-вывода
// и запись в видеопамять обслуживает ЦП комплекса, а процессор платы его ждёт.
// ОЗУ и ПЗУ - свои устройства; здесь регистры позиции, видеопамять и сам обмен.
//
// Со стороны ЦП комплекса (ТО, таблица 2, смещения слов):
//   0  чтение - байт тестового ПЗУ со счётчиком адреса; запись - NMI
//   2  чтение - слово состояния; запись - сброс счётчика
//   4  чтение - адрес цикла A0-A15; запись - RES, INTR и вектор
//   6  чтение - данные цикла, завершает запись; запись - данные, завершает чтение
//      (данные инвертированы в обе стороны: BA1M и EMUL.TSK)
//
// Слово состояния (таблица 3): 0 ВНЕ, 1 нет сброса, 2 нет записи в видеопамять,
// 3 нет ожидания, 4 есть INTR, 5 нет HALT, 6 нет записи, 7 нет чтения УВВ. Разряды
// 7 и 6 выведены на магистраль как ЗОА и ЗОБ, поэтому линии здесь ~irqa и ~irqb.
//
// Как держит процессор: запись защёлкивается сразу, а 8086 останавливается на границе
// следующей команды, пока хост её не примет (I8086Gate::ready). Чтение ядро узнаёт до
// выполнения команды: плата поднимает ЗОА, а команда идёт, когда данные пришли
// (I8086Gate::input), а прерывание ждёт конца начатого IN. Для хоста это те же циклы.
// Слово по нечётному порту - один цикл,
// а не два, как на кристалле. Очередь записей нужна для слова в видеопамяти по
// нечётному адресу: это два цикла одной команды.
class E85Mc1702: public AddressableDevice, public I8086Gate, public WaitSource
{
public:
    enum { VRAM_SIZE = 16384, PORT_VRAM = 0x20000 };

private:
    Interface i_irqa;               // ЗОА, чтение УВВ
    Interface i_irqb;               // ЗОБ, запись УВВ или видеопамяти
    Interface i_init;
    Interface i_res;
    Interface i_intr;
    Interface i_nmi;
    Interface i_vector;
    Interface i_inta;

    i8086 * m_cpu = nullptr;
    AddressableDevice * m_test_rom = nullptr;
    std::vector<uint8_t> m_vram;
    unsigned int m_memory_wait = 0;

    struct Write { uint16_t address; uint16_t data; bool word; bool vram; };
    enum { QUEUE = 8 };
    Write m_queue[QUEUE];
    unsigned int m_head = 0;
    unsigned int m_count = 0;

    unsigned int m_rom_counter = 0;
    unsigned int m_control = 0;
    bool m_res = true;
    bool m_intr = false;

    bool m_reading = false;         // ЗОА поднят: ждём данные от хоста
    bool m_read_done = false;       // данные пришли, команда IN может идти
    uint16_t m_read_data = 0;
    uint16_t m_address = 0;         // адрес цикла, как его держат D22, D23
    uint16_t m_data = 0;            // данные цикла
    bool m_word = false;

    unsigned int m_reads = 0;
    unsigned int m_writes = 0;
    unsigned int m_vram_writes = 0;
    unsigned int m_nmis = 0;
    unsigned int m_ints = 0;

    // Отладка: кольцо обращений хоста - чтение или запись, регистр, значение, адрес
    // цикла; тестовое ПЗУ и подряд одинаковые чтения состояния не пишутся
    struct TraceEntry { uint16_t kind; uint16_t reg; uint16_t value; uint16_t address; };
    TraceEntry m_trace[1024] = {};
    unsigned int m_trace_next = 0;
    void trace(unsigned int kind, unsigned int reg, unsigned int value);

    void init_board();
    void update_lines();
    void set_res(bool on);
    void next_write();
    void push_write(uint16_t address, uint16_t data, bool word, bool vram);
    void end_read(unsigned int value);
    unsigned int status() const;
    unsigned int read_reg(unsigned int reg, bool peek);
    void write_reg(unsigned int reg, unsigned int value);

public:
    E85Mc1702(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned get_direct(unsigned address) override;

    // I8086Gate
    bool gate_ready() override;
    bool gate_input(uint16_t port, bool word) override;
    uint16_t gate_in(uint16_t port, bool word) override;
    void gate_out(uint16_t port, uint16_t value, bool word) override;

    // Такты ожидания цикла памяти: ответ платы процессору, wait у её мэппера
    unsigned int wait_states(unsigned int address, bool write, unsigned int offset) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_e85_mc1702(InterfaceManager *im, EmulatorConfigDevice *cd);
