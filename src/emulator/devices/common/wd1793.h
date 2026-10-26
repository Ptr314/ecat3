// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: wd1793 FDC device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

#define wd1793_REG_STATUS       0x00
#define wd1793_REG_COMMAND      0x00
#define wd1793_REG_TRACK        0x01
#define wd1793_REG_SECTOR       0x02
#define wd1793_REG_DATA         0x03

#define wd1793_FLAG_BUSY        0x01
#define wd1793_FLAG_DRQ         0x02
#define wd1793_FLAG_INDEX       0x02
#define wd1793_FLAG_TR00        0x04
#define wd1793_FLAG_LOST_DATA   0x04
#define wd1793_FLAG_BAD_CRC     0x08
#define wd1793_FLAG_ERR_SEEK    0x10
#define wd1793_FLAG_HLD         0x20
#define wd1793_FLAG_DATA_TYPE   0x20
#define wd1793_FLAG_ERR_WRITE   0x20
#define wd1793_FLAG_PROTECTED   0x40
#define wd1793_FLAG_NOT_READY   0x80

#define wd1793_PARAM_T          0x10
#define wd1793_PARAM_m          0x10
#define wd1793_PARAM_h          0x08
#define wd1793_PARAM_V          0x04

//задержка записи значения в регистр
//Если 0 - эмулятор не проходит некоторые тесты
//Если слишком большое - не работают программы
//Все значения задержек - в тактах контроллера

#define wd1793_DELAY_REGISTER       10

#define wd1793_DELAY_RESTORE        100
#define wd1793_COMMAND_RESTORE      1

#define wd1793_DELAY_SECTOR         100
#define wd1793_COMMAND_READ_SECTOR  2
#define wd1793_COMMAND_WRITE_SECTOR 3

#define wd1793_DELAY_NEXT_BYTE      10
#define wd1793_COMMAND_READ_BYTE    4
#define wd1793_COMMAND_WRITE_BYTE   5

#define wd1793_DELAY_STEP           100
#define wd1793_COMMAND_SEEK         6
#define wd1793_COMMAND_STEP         7
#define wd1793_COMMAND_READ_ADDRESS 8
#define wd1793_COMMAND_WRITE_TRACK  9
#define wd1793_COMMAND_TRACK_BYTE   10

// Bytes of a track from index to index: 6250 at 250 kbit/s in MFM
#define wd1793_TRACK_LENGTH         6250

// The head stays loaded for a while after the last command (the real chip
// counts 15 index pulses, about 3 s), which is what keeps the drive motor and
// its led on between consecutive accesses
#define wd1793_DELAY_HLD_RELEASE    5000000

class WD1793 : public FDC
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_INTRQ;
    Interface i_DRQ;
    Interface i_HLD;
    FDD * drives[4];
    unsigned int drives_count;
    int selected_drive;
    unsigned int command;
    int delay;
    int register_delay;
    unsigned int register_to_write;
    unsigned int value_to_write;
    int sector_size;
    int bytes;
    int step_dir;
    unsigned int sync_mask;
    bool hld;
    int hld_timer;
    unsigned int sectors_read;
    unsigned int sectors_written;
    bool intrq_forced;      //INTRQ held by Force Interrupt I3 until a $D0

    //Delay of a register write, controller clocks (wd1793_DELAY_REGISTER)
    int m_register_delay = wd1793_DELAY_REGISTER;
    //The heads of the drives as the drives hold them: TR00 comes from the
    //head standing on cylinder 0, a step past the last cylinder does not move
    //it, a disk is read where the head is. Off: the track register is the head
    bool m_physical = false;
    int m_max_cylinder = 83;
    uint32_t m_head[4] = {0, 0, 0, 0};
    //Index pulses in the type I status, every m_index_period clocks for
    //m_index_width of them; 0: none
    unsigned m_index_period = 0;
    unsigned m_index_width = 0;
    unsigned m_index_clock = 0;
    bool m_type1 = true;    //the status shows the type I bits
    uint8_t m_id[6] = {};   //Read Address: the ID field it hands out
    bool m_reading_id = false;
    //Write Track: the image keeps sectors, not the track, so the stream is
    //read as the chip would write it - an ID field after $FE, the data of
    //that sector after $FB/$F8 - and the data goes into the sector
    unsigned m_track_length = wd1793_TRACK_LENGTH;
    unsigned m_wt_state = 0;    //0 gaps, 1 ID field, 2 data field
    unsigned m_wt_left = 0;     //bytes of the field still to come
    bool m_wt_write = false;    //the data field lands in the image
    bool m_wt_have_id = false;  //an ID field came since the last data field

    uint8_t m_wt_id[4] = {};
    unsigned m_tracks_written = 0;
    void track_byte(uint8_t value);

    int head_cylinder();
    void move_head(int steps);

    void SetDRQ();
    bool GetDRQ();
    void ClearDRQ();
    void SetFlag(unsigned flag);
    void ClearFlag(unsigned flag);
    void SetINTRQ();
    void ClearINTRQ();
    void SetHLD(bool value);
    void SetTypeIFlags(uint8_t T, uint8_t S);
    void SetTypeIIFlags();
    void FindSelectedDrive();
    void WriteRegister(unsigned address, unsigned  value);
    void ExecuteCommand();
    void SyncAccess();

public:
    uint8_t registers[5];
    WD1793(InterfaceManager *im, EmulatorConfigDevice *cd);
    bool get_busy() override;
    //For a board whose own register holds the processor until DRQ or INTRQ
    //(the B504 of the Поиск-1, port C4h): runs the controller forward to that
    //point, as "sync" does for a data register
    void wait_ready() { SyncAccess(); }
    bool drq_active() { return GetDRQ(); }
    unsigned get_selected_drive() override;
    unsigned get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned address, unsigned value, bool force=false) override;
    emulator::Result load_config(SystemData *sd) override;
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd);
