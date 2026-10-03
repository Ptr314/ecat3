// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: ДВК floppy controller MX (3.057.122)

#pragma once

#include <vector>

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

#define DVK_MX_MAX_DRIVES   4
#define DVK_MX_SECTORS      11
#define DVK_MX_SECTOR_SIZE  256

// Контроллер НГМД «MX» ДВК: до четырёх 5,25" приводов, запись FM. Он не
// знает секторов: читает и пишет дорожку целиком, словами, а формат дорожки
// задаёт драйвер. Обязательно только синхрослово 000363 - контроллер ищет его
// при чтении, и с него же отдаёт первое слово.
//
//   177130  MXCS  разряд 1 - запрет выбора привода (0 - привод выбран
//                 разрядами 2-3), 4 - шаг, 5 - направление (1 - к центру),
//                 6 - двигатель, 7 - таймер 2 кГц (запись разрешает, чтение
//                 показывает тик и снимает его), 8 - ошибка (запись не
//                 поспела), 9 - индекс, 10 - защита записи, 11 - дорожка 0,
//                 12 - верхняя сторона, 13 - запись, 14 - пуск (не
//                 хранится), 15 - готовность слова (TR)
//   177132        чтение - принятое слово (снимает TR), запись - слово на
//                 запись (снимает TR)
//
// Пуск ждёт индекса. Чтение отдаёт слова от синхрослова до следующего
// индекса, по слову за 128 мкс (32 ячейки FM на 250 кГц); не взятое вовремя
// слово затирается следующим. Запись идёт от индекса до индекса: на каждом
// слове контроллер берёт регистр записи и поднимает TR, и если к концу
// слова программа его не заполнила, ставит ошибку и останавливается.
// Прерываний у контроллера нет - драйвер опрашивает TR.
//
// Образ - плоский файл секторов (как у MAME, dvk_mx_dsk): 11 секторов по 256
// байт на сторону дорожки, стороны дорожки подряд; 80 x 2 = 450560 байт.
//
// Шаг: запись с разрядом 4 запускает импульс на 2 мс, и головка сдвигается
// в его конце. Запись шага до конца импульса начинает его заново - драйвер
// пишет разряд дважды подряд, а головка делает один шаг (так и в MAME).
// Из него при чтении собирается дорожка формата MX: 8 нулевых слов, 000363,
// номер дорожки, 11 секторов по 128 слов, за каждым - сумма его слов, и
// три слова 0203xx (xx - номер дорожки * 2 + сторона). Записанная дорожка
// разбирается обратно по тому же синхрослову; дорожка без него в образ не
// попадает.
class DVKMX: public FDC
{
private:
    Interface i_select;
    Interface i_side;
    Interface i_motor_on;

    struct Drive {
        FDD * fdd = nullptr;
        int track = 0;          // физическая дорожка под головкой
    };
    Drive m_drives[DVK_MX_MAX_DRIVES];
    unsigned int m_drives_count = 0;

    enum Op { OP_IDLE = 0, OP_WAIT_READ, OP_READ, OP_WAIT_WRITE, OP_WRITE };

    unsigned int m_csr = 0;         // хранимые разряды MXCS
    unsigned int m_rbuf = 0;
    unsigned int m_wbuf = 0;
    bool m_tr = false;
    bool m_err = false;
    bool m_timer_tick = false;

    unsigned int m_op = OP_IDLE;
    std::vector<uint16_t> m_track;  // дорожка под головкой, словами
    unsigned int m_pos = 0;         // следующее слово дорожки
    unsigned int m_shift = 0;       // слово, которое пишется сейчас

    // Время в тактах домена: оборот, слово, индексный импульс, тик таймера
    uint64_t m_rev_ticks = 0;
    uint64_t m_word_ticks = 0;
    uint64_t m_index_ticks = 0;
    uint64_t m_timer_ticks = 0;
    uint64_t m_angle = 0;           // положение диска от индекса
    uint64_t m_word_left = 0;       // до конца текущего слова
    uint64_t m_timer_left = 0;
    uint64_t m_step_left = 0;       // шаг ещё не закончен

    unsigned int m_reads = 0;       // дорожек прочитано и записано, для сценариев
    unsigned int m_writes = 0;
    unsigned int m_underruns = 0;
    unsigned int m_steps = 0;
    unsigned int m_written = 0;     // слов в последней записанной дорожке
    std::vector<uint16_t> m_last_write;

    // Последние записи в MXCS - что делал драйвер, для поля trace
    static const unsigned int TRACE_SIZE = 64;
    unsigned int m_trace[TRACE_SIZE] = {};
    unsigned int m_trace_count = 0;

    bool m_step_dir = false;        // направление шага, ждущего конца импульса

    Drive * current();
    bool selected_loaded();
    void update_drive_lines();
    void build_track();
    void store_track();
    void start_word();

public:
    DVKMX(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    bool get_busy() override;
    unsigned int get_selected_drive() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_dvk_mx(InterfaceManager *im, EmulatorConfigDevice *cd);
