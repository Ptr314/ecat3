// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DEC RX11 floppy controller (ДВК DX, ГМД-70)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/virq_line.h"

#define DVK_DX_DRIVES       2
#define DVK_DX_SECTOR_SIZE  128
#define DVK_DX_SECTORS      26
#define DVK_DX_TRACKS       77

// Контроллер 8" дисковода RX11 (у ДВК - DX, ГМД-7012). Два регистра, буфер
// на сектор внутри контроллера, обмен с ним - побайтно через RXDB.
//
//   177170  RXCS  разряд 0 - пуск, 1-3 - функция, 4 - привод, 5 - готово,
//                 6 - разрешение прерывания, 7 - TR (ждёт байт или отдаёт
//                 его), 14 - сброс (запись), 15 - ошибка
//   177172  RXDB  байт буфера, номер сектора и дорожки, после команды -
//                 регистр состояния RXES (разряд 2 - сброс закончен, 7 -
//                 привод готов) или код ошибки
//
// Функции: 0 - заполнить буфер (128 байт в RXDB по TR), 1 - выдать буфер,
// 2 - записать сектор, 3 - прочитать сектор (в RXDB по TR номер сектора
// 1-26, потом дорожки 0-76), 5 - прочитать RXES, 6 - записать с меткой
// удаления, 7 - прочитать регистр ошибок. Конец функции - «готово» и
// прерывание по вектору 264. Сброс читает сектор 1 дорожки 1 привода 0.
//
// Диск в приводе - дорожки IBM 3740 FM целиком (fdd mode fm_ibm): сектор
// ищется по заголовку на дорожке, метка удаления F8 пишется функцией 6 и
// показывается при чтении разрядом DD в RXES. Образ - 77 дорожек по 26
// секторов по 128 байт подряд, 256256 байт (SIMH): привод при загрузке
// размечает его в дорожки, при сохранении разбирает обратно (меток удаления
// в нём нет), .mfm хранит дорожки как есть. Приводы - fdd с sides = _1,
// sectors = _26, sector_size = _128.
class DVKDX: public FDC
{
private:
    Interface i_virq;
    Interface i_vector;
    Interface i_virq_in;
    Interface i_vector_in;
    VirqLine m_irq;
    Interface i_iako;
    Interface i_init;
    Interface i_select;
    Interface i_motor_on;       // активный ноль, пока идёт обмен с диском

    FDD * m_drives[DVK_DX_DRIVES] = {};
    unsigned int m_drives_count = 0;

    enum Phase { PH_IDLE = 0, PH_FILL, PH_EMPTY, PH_SECTOR, PH_TRACK, PH_BUSY };

    unsigned int m_phase = PH_IDLE;
    unsigned int m_func = 0;
    unsigned int m_unit = 0;
    bool m_done = true;
    bool m_ie = false;
    bool m_tr = false;
    bool m_err = false;
    bool m_pending = false;
    unsigned int m_db = 0;          // что читается из RXDB
    unsigned int m_rxes = 0;
    unsigned int m_error_code = 0;
    unsigned int m_sector = 1;
    unsigned int m_track = 0;
    unsigned int m_pos = 0;
    uint8_t m_buffer[DVK_DX_SECTOR_SIZE] = {};
    unsigned int m_vector = 0264;

    uint64_t m_op_ticks = 1;        // время операции с диском
    uint64_t m_left = 0;

    unsigned int m_reads = 0;
    unsigned int m_writes = 0;

    void update_irq();
    void finish(bool error, unsigned int code);
    void execute();
    void init_controller();
    FDD * track_drive(FDD * f);
    bool read_sector(FDD * drive, unsigned int track, unsigned int sector, bool &deleted);
    unsigned int status() const;

public:
    DVKDX(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

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

ComputerDevice * create_dvk_dx(InterfaceManager *im, EmulatorConfigDevice *cd);
