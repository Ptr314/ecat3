// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК floppy disk controller (КНГМД, К1801ВП1-128)

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

// The controller is a raw MFM engine: it feeds the CPU 16-bit words of the
// track byte stream and detects address marks; all sector logic lives in the
// DISK ROM. Everything below follows the standard КНГМД (registers 0177130
// and 0177132) as described in the БК-0011 system programmer's guide and as
// reproduced by the BKBTL emulator, which runs the same ROM.

#define BK_FDC_MAX_DRIVES   4
#define BK_FDC_TRACK_BYTES  6250        // raw track length, bytes
#define BK_FDC_TRACK_WORDS  (BK_FDC_TRACK_BYTES / 2)
#define BK_FDC_INDEX_BYTES  30          // index hole length, bytes of the track
#define BK_FDC_SECTORS      10
#define BK_FDC_SECTOR_SIZE  512
#define BK_FDC_MAX_TRACK    82          // the head does not go further
#define BK_FDC_WORDS_PER_S  15625       // 300 rpm, 3125 words per revolution

// Command register (0177130 write)
#define BK_FDC_CMD_DRIVES   0x000F      // bits 0-3 select drives 0-3
#define BK_FDC_CMD_MOTOR    (1 << 4)
#define BK_FDC_CMD_SIDE     (1 << 5)    // 1 = side 1
#define BK_FDC_CMD_DIR      (1 << 6)    // 1 = towards the last track
#define BK_FDC_CMD_STEP     (1 << 7)
#define BK_FDC_CMD_READ     (1 << 8)    // "start reading": arms the mark search
#define BK_FDC_CMD_MARKER   (1 << 9)    // write an address mark
#define BK_FDC_CMD_PRECOMP  (1 << 10)   // write precompensation, ignored

// Status register (0177130 read)
#define BK_FDC_ST_TRACK0    (1 << 0)
#define BK_FDC_ST_READY     (1 << 1)
#define BK_FDC_ST_WRPROT    (1 << 2)
#define BK_FDC_ST_MOREDATA  (1 << 7)    // a word is waiting / the write register is free
#define BK_FDC_ST_CRC_OK    (1 << 14)
#define BK_FDC_ST_INDEX     (1 << 15)

struct BKFDCDrive {
    FDD * fdd;
    uint8_t data[BK_FDC_TRACK_BYTES];   // raw track under the head
    bool marker[BK_FDC_TRACK_WORDS];    // address mark positions, per word
    unsigned int position;              // byte offset of the head within the track
    int track;                          // head position
    int cached_track;                   // what the buffer holds
    int cached_side;
    unsigned int generation;            // FDD image generation the buffer was made from
    bool valid;
    bool dirty;                         // written to, not yet stored into the image
};

class BKFDC : public FDC
{
private:
    Interface i_select;                 // index of the selected drive
    Interface i_side;                   // side, inverted as the fdd device expects
    Interface i_motor_on;               // active low, for the drive LEDs
    Interface i_control;                // copy of the command word for the memory map

    BKFDCDrive m_drives[BK_FDC_MAX_DRIVES];
    unsigned int m_drives_count;
    int m_selected;                     // -1 when no drive is selected
    unsigned int m_side;

    uint16_t m_command;
    uint16_t m_status;                  // only MOREDATA and CRC_OK live here
    uint16_t m_datareg;                 // read mode: the word under the head
    uint16_t m_writereg;                // write mode: the word waiting behind the shift register
    uint16_t m_shiftreg;                // write mode: the word being written
    bool m_writeflag;
    bool m_shiftflag;
    bool m_writemarker;                 // the word in the write register carries an address mark
    bool m_shiftmarker;
    bool m_writing;
    bool m_search_armed;                // bit 8 was seen set, waiting for it to drop
    bool m_searching;                   // looking for an address mark
    bool m_crc_armed;                   // accumulating a CRC
    uint16_t m_crc;

    unsigned int m_word_cycles;         // system clocks per word
    unsigned int m_ticks;

    // Debugging aid: a ring of the last register accesses, readable from scripts
    struct TraceEntry { unsigned int kind; unsigned int value; unsigned int pos; };
    TraceEntry m_trace[1024];
    unsigned int m_trace_next;
    unsigned int m_last_status;
    void trace(unsigned int kind, unsigned int value);

    BKFDCDrive * current();
    bool motor_on();
    bool drive_loaded(BKFDCDrive * d);
    void update_drive_lines();
    void tick();
    void crc_byte(uint8_t b);

    void encode_track(BKFDCDrive * d);
    bool decode_track(BKFDCDrive * d, uint8_t * sectors, int &count);
    void ensure_track(BKFDCDrive * d);
    void flush_track(BKFDCDrive * d);
    void flush_all();

    unsigned int read_status();
    unsigned int read_data();
    void write_command(unsigned int value);
    void write_data(unsigned int value);

public:
    BKFDC(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_direct(unsigned int address) override;

    bool get_busy() override;
    unsigned int get_selected_drive() override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_bk_fdc(InterfaceManager *im, EmulatorConfigDevice *cd);
