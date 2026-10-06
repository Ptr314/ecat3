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
    m_clocked = true;   //clock() is overridden here
    memset(m_trace, 0, sizeof(m_trace));
    addresable_size = 4;
    can_read = true;
    can_write = true;
    memset(m_drives, 0, sizeof(m_drives));
    memset(m_legacy_deleted, 0, sizeof(m_legacy_deleted));
    memset(m_legacy, 0, sizeof(m_legacy));
}

emulator::Result BKFDC::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    std::vector<FDD*> list;
    res = load_drives(1, BK_FDC_MAX_DRIVES, list);
    if (!res) return res;
    m_drives_count = list.size();

    // The side line is shared by all drives, exactly as on the cable
    LinkData ld;
    ld.s.i = &i_side;
    ld.s.shift = 0;
    ld.s.mask = create_mask(1, 0);

    for (unsigned int i = 0; i < m_drives_count; i++) {
        m_drives[i].fdd = list[i];
        m_drives[i].track = 0;
        m_drives[i].valid = false;

        ld.d.i = im->get_interface_by_name(list[i]->name, "side");
        if (ld.d.i == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{BKFDC|" + std::string(QT_TRANSLATE_NOOP("BKFDC", "Interface not found")) + "} " + list[i]->name + ":side");
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

// The drive keeps the disk as whole tracks (mode mfm_ibm). One left with a
// sector image - a configuration without that mode, a snapshot from before
// it - is turned into tracks on first use
FDD * BKFDC::track_drive(BKFDCDrive * d)
{
    if (!drive_loaded(d)) return nullptr;
    if (!d->fdd->whole_track()) d->fdd->rebuild_ibm_mfm(nullptr);
    return d->fdd->whole_track() ? d->fdd : nullptr;
}

// The raw track under the head of the drive, from its disk. A track beyond
// the disk (the head goes a couple further than the image) reads as a
// formatted blank one, so that a short image is a disk with empty tracks
void BKFDC::load_track(BKFDCDrive * d)
{
    memset(d->data, 0, sizeof(d->data));
    memset(d->marker, 0, sizeof(d->marker));

    d->cached_track = d->track;
    d->cached_side = (int)m_side;
    d->generation = (d->fdd != nullptr) ? d->fdd->get_generation() : 0;
    d->valid = true;
    d->dirty = false;

    std::vector<uint8_t> bytes;
    std::vector<int> marks;
    FDD * f = track_drive(d);
    if (f != nullptr && f->read_track(d->track, (int)m_side, bytes)) {
        f->read_track_marks(d->track, (int)m_side, marks);
        memcpy(d->data, bytes.data(), bytes.size() < sizeof(d->data) ? bytes.size() : sizeof(d->data));
    } else {
        dsk_tools::BYTES data, special;
        dsk_tools::ibm_mfm_format_track(data, special, d->track, (int)m_side, BK_FDC_SECTORS, BK_FDC_SECTOR_SIZE, nullptr, 0);
        memcpy(d->data, data.data(), sizeof(d->data));
        std::map<int, int> blank;
        marks_from_special(special, true, blank);
        for (const auto &e : blank) marks.push_back(e.first);
    }
    for (int p : marks)
        if (p >= 0 && p / 2 < BK_FDC_TRACK_WORDS) d->marker[p / 2] = true;
}

void BKFDC::ensure_track(BKFDCDrive * d)
{
    unsigned int gen = (d->fdd != nullptr) ? d->fdd->get_generation() : 0;
    if (d->valid && d->cached_track == d->track && d->cached_side == (int)m_side && d->generation == gen)
        return;
    if (d->valid && d->dirty && d->generation == gen)
        flush_track(d);
    load_track(d);
}

// Stores a written track on the disk of the drive, bytes and sync marks as
// they are: a deleted data mark or a layout of its own reads back as written
void BKFDC::flush_track(BKFDCDrive * d)
{
    if (!d->valid || !d->dirty) return;
    d->dirty = false;

    FDD * f = track_drive(d);
    if (f == nullptr || f->is_protected()) return;
    if (d->generation != f->get_generation()) return;      // another disk is in the drive now

    std::vector<uint8_t> bytes(d->data, d->data + BK_FDC_TRACK_BYTES);
    std::vector<int> marks;
    for (int w = 0; w < BK_FDC_TRACK_WORDS; w++)
        if (d->marker[w]) marks.push_back(w * 2);
    if (f->write_track(d->cached_track, d->cached_side, bytes))
        f->write_track_marks(d->cached_track, d->cached_side, marks);
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

void BKFDC::save_state(StateWriter &w)
{
    FDC::save_state(w);

    //The track buffers are a write-back cache over the images. Flushing them
    //first means the drives carry everything the machine has written, and the
    //caches that go out below are only what is under the heads
    flush_all();

    w.n("selected", static_cast<uint32_t>(m_selected));
    w.n("side", m_side);
    w.u("command", m_command);
    w.u("status", m_status);
    w.u("datareg", m_datareg);
    w.u("writereg", m_writereg);
    w.u("shiftreg", m_shiftreg);
    w.b("writeflag", m_writeflag);
    w.b("shiftflag", m_shiftflag);
    w.b("writemarker", m_writemarker);
    w.b("shiftmarker", m_shiftmarker);
    w.b("writing", m_writing);
    w.b("search_armed", m_search_armed);
    w.b("searching", m_searching);
    w.b("crc_armed", m_crc_armed);
    w.u("crc", m_crc);
    w.n("ticks", m_ticks);

    for (unsigned int i = 0; i < m_drives_count; i++)
    {
        const BKFDCDrive &d = m_drives[i];
        w.push(("drive" + std::to_string(i)).c_str());
        w.n("position", d.position);
        w.n("track", static_cast<uint32_t>(d.track));
        w.n("cached_track", static_cast<uint32_t>(d.cached_track));
        w.n("cached_side", static_cast<uint32_t>(d.cached_side));
        w.n("generation", d.generation);
        w.b("valid", d.valid);
        if (d.valid)
        {
            w.hex("data", d.data, BK_FDC_TRACK_BYTES);
            w.array("marker", d.marker, BK_FDC_TRACK_WORDS);
        }
        w.pop();
    }
    //The ring of register accesses is a debugging aid, read by scripts
}

emulator::Result BKFDC::load_state(const StateReader &r)
{
    emulator::Result res = FDC::load_state(r);
    if (!res) return res;

    r.u("selected", m_selected);
    r.u("side", m_side);
    r.u("command", m_command);
    r.u("status", m_status);
    r.u("datareg", m_datareg);
    r.u("writereg", m_writereg);
    r.u("shiftreg", m_shiftreg);
    r.b("writeflag", m_writeflag);
    r.b("shiftflag", m_shiftflag);
    r.b("writemarker", m_writemarker);
    r.b("shiftmarker", m_shiftmarker);
    r.b("writing", m_writing);
    r.b("search_armed", m_search_armed);
    r.b("searching", m_searching);
    r.b("crc_armed", m_crc_armed);
    r.u("crc", m_crc);
    r.u("ticks", m_ticks);

    for (unsigned int i = 0; i < m_drives_count; i++)
    {
        BKFDCDrive &d = m_drives[i];
        const StateReader dr = r.sub(("drive" + std::to_string(i)).c_str());
        dr.u("position", d.position);
        dr.u("track", d.track);
        dr.u("cached_track", d.cached_track);
        dr.u("cached_side", d.cached_side);
        dr.u("generation", d.generation);
        dr.b("valid", d.valid);
        // A snapshot from before the drives kept whole tracks carries the
        // deleted data marks here; they go onto the tracks in state_restored()
        memset(m_legacy_deleted[i], 0, sizeof(m_legacy_deleted[i]));
        m_legacy[i] = dr.array("deleted", &m_legacy_deleted[i][0][0], BK_FDC_TRACKS * 2);
        if (d.valid)
        {
            dr.hex("data", d.data, BK_FDC_TRACK_BYTES);
            dr.array("marker", d.marker, BK_FDC_TRACK_WORDS);
        }
        //Flushed before the snapshot was taken, so what is in the buffer is
        //also in the image
        d.dirty = false;
    }
    return emulator::Result::ok();
}

void BKFDC::state_restored()
{
    FDC::state_restored();
    for (unsigned int i = 0; i < m_drives_count; i++) {
        BKFDCDrive &d = m_drives[i];
        if (!drive_loaded(&d) || d.fdd->whole_track()) { m_legacy[i] = false; continue; }
        // A drive restored with a sector image: its tracks are built now,
        // with the deleted data marks the controller used to keep itself
        std::vector<uint16_t> deleted((size_t)d.fdd->get_tracks() * d.fdd->get_sides(), 0);
        if (m_legacy[i])
            for (int t = 0; t < d.fdd->get_tracks() && t < BK_FDC_TRACKS; t++)
                for (int sd = 0; sd < d.fdd->get_sides() && sd < 2; sd++)
                    deleted[(size_t)t * d.fdd->get_sides() + sd] = m_legacy_deleted[i][t][sd];
        d.fdd->rebuild_ibm_mfm(deleted.data());
        m_legacy[i] = false;
    }
}

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
