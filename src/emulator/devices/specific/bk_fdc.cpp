// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: БК floppy disk controller (КНГМД, К1801ВП1-128)

#include <cstring>

#include "bk_fdc.h"
#include "emulator/utils.h"

// Word registers: +0 is 0177130 (status / command), +2 is 0177132 (data)
#define REG_STATUS  0
#define REG_DATA    2

// Track layout, in bytes of the decoded MFM stream
#define GAP_FIRST   42          // GAP4a + GAP1 before the first sector
#define GAP_SECTOR  36          // GAP3 between sectors
#define GAP_HEADER  22          // GAP2 between the header and the data
#define SYNC_BYTES  12
#define GAP_BYTE    0x4E
#define MARK_BYTE   0xA1
#define MARK_HEADER 0xFE
#define MARK_DATA   0xFB

BKFDC::BKFDC(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd)
    , i_select(this, im, 2, "select", MODE_W)
    , i_side(this, im, 1, "side", MODE_W)
    , i_motor_on(this, im, 1, "motor_on", MODE_W)
    , i_control(this, im, 16, "control", MODE_W)
    , m_drives_count(0)
    , m_selected(-1)
    , m_side(0)
    , m_command(0)
    , m_status(0)
    , m_datareg(0)
    , m_writereg(0)
    , m_shiftreg(0)
    , m_writeflag(false)
    , m_shiftflag(false)
    , m_writemarker(false)
    , m_shiftmarker(false)
    , m_writing(false)
    , m_search_armed(false)
    , m_searching(false)
    , m_crc_armed(false)
    , m_crc(0xFFFF)
    , m_word_cycles(256)
    , m_ticks(0)
    , m_trace_next(0)
    , m_last_status(0)
{
    memset(m_trace, 0, sizeof(m_trace));
    addresable_size = 4;
    can_read = true;
    can_write = true;
    memset(m_drives, 0, sizeof(m_drives));
}

emulator::Result BKFDC::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    std::string s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{BKFDC|" + std::string(QT_TRANSLATE_NOOP("BKFDC", "Incorrect fdd list for")) + "} " + name);
    }

    std::vector<std::string> parts = split_string(s, '|', true);
    if (parts.empty() || parts.size() > BK_FDC_MAX_DRIVES)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{BKFDC|" + std::string(QT_TRANSLATE_NOOP("BKFDC", "Incorrect fdd list for")) + "} " + name);
    m_drives_count = parts.size();

    // The side line is shared by all drives, exactly as on the cable
    LinkData ld;
    ld.s.i = &i_side;
    ld.s.shift = 0;
    ld.s.mask = create_mask(1, 0);

    for (unsigned int i = 0; i < m_drives_count; i++) {
        FDD * fdd = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));
        if (fdd == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{BKFDC|" + std::string(QT_TRANSLATE_NOOP("BKFDC", "Not a fdd device")) + "} " + parts[i]);
        m_drives[i].fdd = fdd;
        m_drives[i].track = 0;
        m_drives[i].valid = false;

        ld.d.i = im->get_interface_by_name(parts[i], "side");
        if (ld.d.i == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{BKFDC|" + std::string(QT_TRANSLATE_NOOP("BKFDC", "Interface not found")) + "} " + parts[i] + ":side");
        ld.d.shift = 0;
        ld.d.mask = create_mask(1, 0);
        ld.s.i->connect(ld.s, ld.d, false);
    }

    // One word every 64 us regardless of the CPU speed: 256 clocks at 4 MHz, 192 at 3 MHz
    unsigned int def = (m_system_clock != 0) ? m_system_clock / BK_FDC_WORDS_PER_S : 256;
    m_word_cycles = read_confg_value(cd, "word_cycles", false, def);
    if (m_word_cycles == 0) m_word_cycles = 1;

    update_drive_lines();

    return emulator::Result::ok();
}

void BKFDC::reset(MAYBE_UNUSED bool cold)
{
    flush_all();

    m_selected = -1;
    m_side = 0;
    m_command = 0;
    m_status = 0;
    m_datareg = 0;
    m_writereg = 0;
    m_shiftreg = 0;
    m_writeflag = m_shiftflag = false;
    m_writemarker = m_shiftmarker = false;
    m_writing = false;
    m_search_armed = m_searching = false;
    m_crc_armed = false;
    m_crc = 0xFFFF;
    m_ticks = 0;

    // The heads stay where they are, the buffers are rebuilt on demand
    for (unsigned int i = 0; i < m_drives_count; i++) {
        m_drives[i].valid = false;
        m_drives[i].dirty = false;
    }

    update_drive_lines();
    i_control.change(0);
}

//----------------------- Drives -------------------------------------------//

BKFDCDrive * BKFDC::current()
{
    if (m_selected < 0 || m_selected >= (int)m_drives_count) return nullptr;
    return &m_drives[m_selected];
}

bool BKFDC::motor_on()
{
    return (m_command & BK_FDC_CMD_MOTOR) != 0;
}

// A drive counts as loaded only with an image of the controller's geometry:
// the drives read their own parameters after the controller does, so the
// check cannot be made in load_config
bool BKFDC::drive_loaded(BKFDCDrive * d)
{
    return d != nullptr && d->fdd != nullptr && d->fdd->get_loaded() != 0
           && d->fdd->get_sector_size() == BK_FDC_SECTOR_SIZE
           && d->fdd->get_sectors() >= BK_FDC_SECTORS;
}

void BKFDC::update_drive_lines()
{
    i_select.change((m_selected < 0) ? 0 : (unsigned int)m_selected);
    i_side.change((~m_side) & 1);
    i_motor_on.change((motor_on() && current() != nullptr) ? 0 : 1);
}

bool BKFDC::get_busy()
{
    return motor_on();
}

unsigned int BKFDC::get_selected_drive()
{
    return (m_selected < 0) ? 0 : (unsigned int)m_selected;
}

//----------------------- CRC ----------------------------------------------//

// CRC-16-CCITT as used in the IBM MFM format: polynomial 0x1021, MSB first,
// preset 0xFFFF at the first byte of the address mark. Running it over a
// field together with its two CRC bytes leaves zero.
void BKFDC::crc_byte(uint8_t b)
{
    m_crc ^= (uint16_t)(b << 8);
    for (int i = 0; i < 8; i++)
        m_crc = (m_crc & 0x8000) ? (uint16_t)((m_crc << 1) ^ 0x1021) : (uint16_t)(m_crc << 1);
}

//----------------------- Track buffers ------------------------------------//

// Builds the raw track for the head position of the drive from the sectors
// of its image. Tracks beyond the image are blank but properly formatted, so
// that a short image reads as a disk with empty tracks.
void BKFDC::encode_track(BKFDCDrive * d)
{
    memset(d->data, 0, sizeof(d->data));
    memset(d->marker, 0, sizeof(d->marker));

    d->cached_track = d->track;
    d->cached_side = (int)m_side;
    d->generation = (d->fdd != nullptr) ? d->fdd->get_generation() : 0;
    d->valid = true;
    d->dirty = false;

    bool have = drive_loaded(d)
                && d->track < d->fdd->get_tracks()
                && (int)m_side < d->fdd->get_sides();
    if (have) i_side.change((~m_side) & 1);

    unsigned int p = 0;
    unsigned int gap = GAP_FIRST;
    uint16_t saved_crc = m_crc;

    for (int sect = 0; sect < BK_FDC_SECTORS; sect++) {
        for (unsigned int i = 0; i < gap; i++) d->data[p++] = GAP_BYTE;
        for (unsigned int i = 0; i < SYNC_BYTES; i++) d->data[p++] = 0;

        // Header: the mark, cylinder, head, sector, size code, CRC
        d->marker[p / 2] = true;
        m_crc = 0xFFFF;
        unsigned int start = p;
        d->data[p++] = MARK_BYTE; d->data[p++] = MARK_BYTE; d->data[p++] = MARK_BYTE;
        d->data[p++] = MARK_HEADER;
        d->data[p++] = (uint8_t)d->track;
        d->data[p++] = (uint8_t)m_side;
        d->data[p++] = (uint8_t)(sect + 1);
        d->data[p++] = 2;
        for (unsigned int i = start; i < p; i++) crc_byte(d->data[i]);
        d->data[p++] = (uint8_t)(m_crc >> 8);
        d->data[p++] = (uint8_t)(m_crc & 0xFF);

        for (unsigned int i = 0; i < GAP_HEADER; i++) d->data[p++] = GAP_BYTE;
        for (unsigned int i = 0; i < SYNC_BYTES; i++) d->data[p++] = 0;

        // Data: the mark, 512 bytes, CRC
        d->marker[p / 2] = true;
        m_crc = 0xFFFF;
        start = p;
        d->data[p++] = MARK_BYTE; d->data[p++] = MARK_BYTE; d->data[p++] = MARK_BYTE;
        d->data[p++] = MARK_DATA;
        if (have) {
            d->fdd->SeekSector(d->track, sect + 1);
            for (int i = 0; i < BK_FDC_SECTOR_SIZE; i++) d->data[p++] = d->fdd->ReadNextByte();
        } else {
            p += BK_FDC_SECTOR_SIZE;
        }
        for (unsigned int i = start; i < p; i++) crc_byte(d->data[i]);
        d->data[p++] = (uint8_t)(m_crc >> 8);
        d->data[p++] = (uint8_t)(m_crc & 0xFF);

        gap = GAP_SECTOR;
    }
    while (p < BK_FDC_TRACK_BYTES) d->data[p++] = GAP_BYTE;

    m_crc = saved_crc;

#ifdef LOG_FDD
    logs("track " + std::to_string(d->track) + " side " + std::to_string(m_side) + (have ? " encoded" : " blank"));
#endif
}

// Parses a raw track back into sectors, in the order they lie on the track.
// Returns false when the stream does not look like a formatted track; the
// address marks are found by their bytes, the marker flags are not consulted.
bool BKFDC::decode_track(BKFDCDrive * d, uint8_t * sectors, int &count)
{
    const unsigned int total = BK_FDC_SECTORS * BK_FDC_SECTOR_SIZE;
    unsigned int p = 0;
    unsigned int out = 0;
    count = 0;

    for (;;) {
        while (p < BK_FDC_TRACK_BYTES && d->data[p] == GAP_BYTE) p++;
        if (p >= BK_FDC_TRACK_BYTES) break;                     // end of track
        while (p < BK_FDC_TRACK_BYTES && d->data[p] == 0) p++;
        if (p >= BK_FDC_TRACK_BYTES) return false;

        for (int i = 0; i < 3 && p < BK_FDC_TRACK_BYTES && d->data[p] == MARK_BYTE; i++) p++;
        if (p >= BK_FDC_TRACK_BYTES || d->data[p++] != MARK_HEADER) return false;

        if (p + 6 > BK_FDC_TRACK_BYTES) return false;
        uint8_t size_code = d->data[p + 3];
        p += 4 + 2;                                             // header fields and CRC
        unsigned int size;
        if (size_code == 1) size = 256;
        else if (size_code == 2) size = 512;
        else if (size_code == 3) size = 1024;
        else return false;

        while (p < BK_FDC_TRACK_BYTES && d->data[p] == GAP_BYTE) p++;
        while (p < BK_FDC_TRACK_BYTES && d->data[p] == 0) p++;
        for (int i = 0; i < 3 && p < BK_FDC_TRACK_BYTES && d->data[p] == MARK_BYTE; i++) p++;
        if (p >= BK_FDC_TRACK_BYTES || d->data[p++] != MARK_DATA) return false;

        if (p + size + 2 > BK_FDC_TRACK_BYTES) return false;
        for (unsigned int i = 0; i < size; i++) {
            if (out >= total) break;
            sectors[out++] = d->data[p++];
        }
        p += 2;                                                 // data CRC
    }

    count = out / BK_FDC_SECTOR_SIZE;
    return true;
}

void BKFDC::ensure_track(BKFDCDrive * d)
{
    unsigned int gen = (d->fdd != nullptr) ? d->fdd->get_generation() : 0;
    if (d->valid && d->cached_track == d->track && d->cached_side == (int)m_side && d->generation == gen)
        return;
    if (d->valid && d->dirty && d->generation == gen)
        flush_track(d);
    encode_track(d);
}

// Stores a written track back into the image of the drive
void BKFDC::flush_track(BKFDCDrive * d)
{
    if (!d->valid || !d->dirty) return;
    d->dirty = false;

    if (!drive_loaded(d) || d->fdd->is_protected()) return;
    if (d->generation != d->fdd->get_generation()) return;    // another disk is in the drive now
    if (d->cached_track >= d->fdd->get_tracks() || d->cached_side >= d->fdd->get_sides()) return;

    uint8_t sectors[BK_FDC_SECTORS * BK_FDC_SECTOR_SIZE];
    int count;
    if (!decode_track(d, sectors, count)) {
        logs("track " + std::to_string(d->cached_track) + " side " + std::to_string(d->cached_side)
             + ": cannot decode the written track, not stored");
        return;
    }

    i_side.change((~d->cached_side) & 1);
    for (int s = 0; s < count && s < BK_FDC_SECTORS; s++) {
        d->fdd->SeekSector(d->cached_track, s + 1);
        for (int i = 0; i < BK_FDC_SECTOR_SIZE; i++)
            d->fdd->WriteNextByte(sectors[s * BK_FDC_SECTOR_SIZE + i]);
    }
    i_side.change((~m_side) & 1);

#ifdef LOG_FDD
    logs("track " + std::to_string(d->cached_track) + " side " + std::to_string(d->cached_side)
         + " stored, " + std::to_string(count) + " sectors");
#endif
}

void BKFDC::flush_all()
{
    for (unsigned int i = 0; i < m_drives_count; i++) flush_track(&m_drives[i]);
}

//----------------------- Rotation -----------------------------------------//

void BKFDC::clock(unsigned int counter)
{
    m_ticks += counter;
    while (m_ticks >= m_word_cycles) {
        m_ticks -= m_word_cycles;
        tick();
    }
}

// One word of the track passes under the heads
void BKFDC::tick()
{
    if (!motor_on()) return;

    // All disks spin together
    for (unsigned int i = 0; i < m_drives_count; i++) {
        m_drives[i].position += 2;
        if (m_drives[i].position >= BK_FDC_TRACK_BYTES) m_drives[i].position = 0;
    }

    BKFDCDrive * d = current();
    if (!drive_loaded(d)) return;
    ensure_track(d);

    unsigned int p = d->position;

    if (!m_writing) {
        m_datareg = (uint16_t)((d->data[p] << 8) | d->data[p + 1]);

        if (m_status & BK_FDC_ST_MOREDATA) {
            // The CPU left the previous word unread: the field is over and the
            // word under the head is its CRC, so the running CRC is judged now
            if (m_crc_armed) {
                m_crc_armed = false;
                if (m_crc == 0) m_status |= BK_FDC_ST_CRC_OK;
                else            m_status &= ~BK_FDC_ST_CRC_OK;
                trace(6, m_crc);
#ifdef LOG_FDD
                logs("crc " + std::to_string(m_crc) + " at " + std::to_string(p));
#endif
            }
        } else if (m_searching) {
            if (d->marker[p / 2]) {
                m_status |= BK_FDC_ST_MOREDATA;
                m_searching = false;
                m_crc_armed = true;
                m_crc = 0xFFFF;
                crc_byte(d->data[p]);
                crc_byte(d->data[p + 1]);
                trace(5, m_datareg);
#ifdef LOG_FDD
                logs("mark at " + std::to_string(p) + " track " + std::to_string(d->track) + " side " + std::to_string(m_side));
#endif
            }
        } else {
            m_status |= BK_FDC_ST_MOREDATA;
            if (m_crc_armed) {
                crc_byte(d->data[p]);
                crc_byte(d->data[p + 1]);
            }
        }
    } else {
        if (!m_shiftflag) return;

        uint8_t lo = (uint8_t)(m_shiftreg & 0xFF);
        uint8_t hi = (uint8_t)(m_shiftreg >> 8);
        d->data[p] = lo;
        d->data[p + 1] = hi;
        d->dirty = true;
        d->marker[p / 2] = m_shiftmarker;
        if (m_shiftmarker) {
            m_crc = 0xFFFF;
            m_crc_armed = true;
        }
        if (m_crc_armed) {
            crc_byte(lo);
            crc_byte(hi);
        }
        m_shiftflag = false;
        m_shiftmarker = false;

        if (m_writeflag) {
            m_shiftreg = m_writereg;
            m_shiftflag = true;
            m_writeflag = false;
            m_shiftmarker = m_writemarker;
            m_writemarker = false;
            m_status |= BK_FDC_ST_MOREDATA;
        } else if (m_crc_armed) {
            // The CPU stopped feeding data: the CRC closes the field,
            // high byte first in the stream
            m_shiftreg = (uint16_t)(((m_crc & 0xFF) << 8) | (m_crc >> 8));
            m_shiftflag = true;
            m_shiftmarker = false;
            m_crc_armed = false;
            m_status |= BK_FDC_ST_CRC_OK;
#ifdef LOG_FDD
            logs("write crc at " + std::to_string(p));
#endif
        }
    }
}

//----------------------- Registers ----------------------------------------//

// Kinds: 1 command write, 2 data read, 3 data write, 4 status change, 5 mark found, 6 crc check
void BKFDC::trace(unsigned int kind, unsigned int value)
{
    BKFDCDrive * d = current();
    // Consecutive identical reads carry no information: keep one
    if (kind == 2 && m_trace_next > 0) {
        const TraceEntry &prev = m_trace[(m_trace_next - 1) & 1023];
        if (prev.kind == 2 && prev.value == (value & 0xFFFF)) return;
    }
    TraceEntry &e = m_trace[m_trace_next & 1023];
    e.kind = kind;
    e.value = value & 0xFFFF;
    e.pos = d ? d->position : 0xFFFF;
    m_trace_next++;
}

unsigned int BKFDC::read_status()
{
    if (m_selected < 0) return 0;
    BKFDCDrive * d = current();
    if (d == nullptr) return BK_FDC_ST_INDEX | BK_FDC_ST_TRACK0;

    unsigned int st = 0;
    if (d->track == 0) st |= BK_FDC_ST_TRACK0;
    if (!drive_loaded(d)) return st | BK_FDC_ST_INDEX;

    if (d->position < BK_FDC_INDEX_BYTES) st |= BK_FDC_ST_INDEX;
    if (d->fdd->is_protected()) st |= BK_FDC_ST_WRPROT;
    if (motor_on()) st |= BK_FDC_ST_READY | (m_status & (BK_FDC_ST_MOREDATA | BK_FDC_ST_CRC_OK));
    return st;
}

unsigned int BKFDC::read_data()
{
    m_status &= ~BK_FDC_ST_MOREDATA;
    m_searching = false;
    m_writeflag = m_shiftflag = false;
    if (m_writing) {
        m_writing = false;
        BKFDCDrive * d = current();
        if (d != nullptr) flush_track(d);
    }

    BKFDCDrive * d = current();
    if (!drive_loaded(d)) return 0;
    return m_datareg;
}

void BKFDC::write_command(unsigned int value)
{
    uint16_t cmd = (uint16_t)value;
    bool was_motor = motor_on();
    m_command = cmd;

    int newdrive = (cmd & 1) ? 0 : (cmd & 2) ? 1 : (cmd & 4) ? 2 : (cmd & 8) ? 3 : -1;
    if (newdrive != m_selected) {
        flush_all();
        m_selected = newdrive;
    }

    if (!motor_on()) {
        m_status &= ~(BK_FDC_ST_MOREDATA | BK_FDC_ST_CRC_OK);
        if (was_motor) flush_all();
    }

    if (m_selected >= 0) {
        m_side = (cmd & BK_FDC_CMD_SIDE) ? 1 : 0;

        BKFDCDrive * d = current();
        if ((cmd & BK_FDC_CMD_STEP) && d != nullptr) {
            if (cmd & BK_FDC_CMD_DIR) {
                if (d->track < BK_FDC_MAX_TRACK) d->track++;
            } else {
                if (d->track > 0) d->track--;
            }
        }

        // The mark search starts when bit 8 drops after having been set
        if (cmd & BK_FDC_CMD_READ) {
            m_search_armed = true;
        } else if (m_search_armed) {
            m_search_armed = false;
            m_searching = true;
            m_crc_armed = false;
            m_status &= ~(BK_FDC_ST_CRC_OK | BK_FDC_ST_MOREDATA);
        }

        if (m_writing && (cmd & BK_FDC_CMD_MARKER)) {
            m_writemarker = true;
            m_status &= ~(BK_FDC_ST_CRC_OK | BK_FDC_ST_MOREDATA);
        }
    }

    update_drive_lines();
    i_control.change(cmd);
}

void BKFDC::write_data(unsigned int value)
{
    uint16_t v = (uint16_t)value;
    m_writing = true;
    m_searching = false;

    if (!m_writeflag && !m_shiftflag) {
        m_shiftreg = v;
        m_shiftflag = true;
        m_status |= BK_FDC_ST_MOREDATA;
    } else if (!m_writeflag && m_shiftflag) {
        m_writereg = v;
        m_writeflag = true;
        m_status &= ~BK_FDC_ST_MOREDATA;
    } else if (m_writeflag && !m_shiftflag) {
        m_shiftreg = m_writereg;
        m_shiftflag = true;
        m_shiftmarker = m_writemarker;
        m_writemarker = false;
        m_writereg = v;
        m_writeflag = true;
        m_status &= ~BK_FDC_ST_MOREDATA;
    } else {
        m_writereg = v;
    }
}

unsigned int BKFDC::get_value_word(unsigned int address)
{
    if ((address & 2) == REG_DATA) {
        unsigned int v = read_data();
        trace(2, v);
        return v;
    }
    unsigned int st = read_status();
    if (st != m_last_status) {
        m_last_status = st;
        trace(4, st);
    }
    return st;
}

void BKFDC::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if ((address & 2) == REG_DATA) {
        trace(3, value);
        write_data(value);
    } else {
        trace(1, value);
        write_command(value);
    }
}

unsigned int BKFDC::get_value(unsigned int address)
{
    unsigned int v = get_value_word(address);
    return (address & 1) ? ((v >> 8) & 0xFF) : (v & 0xFF);
}

void BKFDC::set_value(unsigned int address, unsigned int value, bool force)
{
    // A byte write replaces its half of the register; the data register has
    // no persistent value to merge with
    unsigned int v = ((address & 2) == REG_DATA) ? 0 : m_command;
    if (address & 1) v = (v & 0x00FF) | ((value & 0xFF) << 8);
    else             v = (v & 0xFF00) | (value & 0xFF);
    set_value_word(address, v, force);
}

unsigned int BKFDC::get_direct(unsigned int address)
{
    return ((address & 2) == REG_DATA) ? m_datareg : read_status();
}

//----------------------- Introspection ------------------------------------//

std::vector<DeviceFieldInfo> BKFDC::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"status",   "Status register as the CPU sees it",   false});
    r.push_back({"command",  "Last command written",                 false});
    r.push_back({"drive",    "Index of the selected drive, -1 none", false});
    r.push_back({"track",    "Head position of the selected drive",  false});
    r.push_back({"side",     "Selected side",                        false});
    r.push_back({"motor",    "1 while the motor is on",              false});
    r.push_back({"writing",  "1 in the write mode",                  false});
    r.push_back({"busy",     "Same as motor",                        false});
    r.push_back({"position", "Byte offset of the head on the track", false});
    r.push_back({"raw",      "Bytes of the raw track under the head", true});
    r.push_back({"marker",   "Address mark flags of the raw track, per word", true});
    r.push_back({"trace",    "Ring of the last register accesses: kind, value, position", true});
    return r;
}

bool BKFDC::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    BKFDCDrive * d = current();

    out.numeric = true;
    out.width = 16;
    if (field == "status")   { out.values.push_back(read_status());                     return true; }
    if (field == "command")  { out.values.push_back(m_command);                         return true; }
    out.width = 0;
    if (field == "drive")    { out.values.push_back((unsigned int)m_selected);          return true; }
    if (field == "track")    { out.values.push_back(d ? d->track : 0);                  return true; }
    if (field == "side")     { out.values.push_back(m_side);                            return true; }
    if (field == "motor")    { out.values.push_back(motor_on() ? 1 : 0);                return true; }
    if (field == "writing")  { out.values.push_back(m_writing ? 1 : 0);                 return true; }
    if (field == "busy")     { out.values.push_back(get_busy() ? 1 : 0);                return true; }
    out.width = 32;
    if (field == "position") { out.values.push_back(d ? d->position : 0);               return true; }
    out.width = 0;

    // Debugging aid: the last register accesses, oldest first; index 0 is the
    // oldest entry of the ring. Each entry is three values.
    if (field == "trace") {
        if (to >= 1024) to = 1023;
        out.has_start = true;
        out.start = from;
        out.width = 16;
        for (unsigned int i = from; i <= to; i++) {
            const TraceEntry &e = m_trace[(m_trace_next + i) & 1023];
            out.values.push_back(e.kind);
            out.values.push_back(e.value);
            out.values.push_back(e.pos);
        }
        return true;
    }

    // Debugging aid: the raw track of the selected drive as the CPU would see it
    if (field == "raw" || field == "marker") {
        unsigned int limit = (field == "raw") ? BK_FDC_TRACK_BYTES : BK_FDC_TRACK_WORDS;
        if (to >= limit) to = limit - 1;
        out.has_start = true;
        out.start = from;
        for (unsigned int i = from; i <= to && d != nullptr && d->valid; i++)
            out.values.push_back((field == "raw") ? d->data[i] : (d->marker[i] ? 1 : 0));
        return true;
    }

    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_bk_fdc(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new BKFDC(im, cd);
}
