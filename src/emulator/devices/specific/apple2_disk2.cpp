// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Disk II controller of the Apple II / Правец with its logic state sequencer

#include "apple2_disk2.h"
#include "emulator/utils.h"

// Sequencer steps in a bit cell: the PROM runs at 2 MHz, a bit takes 4 us
// The sequencer steps at twice the processor clock, the disk turns at its own
// speed: a bit cell is 4 us, 8.1 steps at the 1.018 MHz of the Правец-8А.
// Time inside a cell goes in units of 1/(16 MHz x clock): a step is 8M of
// them, a cell of t eighths of a microsecond t x 2 x clock. Writing, the
// cell is what the sequencer makes it: 8 steps.
// A WOZ 2 disk names the bit cell it was written with, and a few say 3.5 us
// (Border Zone). The sequencer does not read that: a byte stays in the
// register some 14 steps, a LDA/BPL loop looks every 14, and the boot ROM
// loses bytes (a model of P6 and the loop shows the same). Such a disk turns
// as a drive turns any disk, at 4 us a bit
#define STEP_UNITS      8000000ull
#define WRITE_CELL      (8 * STEP_UNITS)
#define CELL_TIME       32
// A run of at least so many $FF in a track is a gap of self-sync bytes

Apple2Disk2::Apple2Disk2(InterfaceManager *im, EmulatorConfigDevice *cd):
      FDC(im, cd)
    , i_select(this, im, 2, "select", MODE_W)
    , i_motor_on(this, im, 1, "motor_on", MODE_W)
{
    m_clocked = true;
}

emulator::Result Apple2Disk2::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;
    m_p6 = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("p6").value));
    if (m_p6 == nullptr || m_p6->get_size() < 256)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Disk2|" + std::string(QT_TRANSLATE_NOOP("Apple2Disk2", "The sequencer PROM p6 of 256 bytes is expected")) + "} " + name);
    std::vector<FDD*> list;
    res = load_drives(1, 2, list);
    if (!res) return res;
    m_drives_count = (int)list.size();
    for (int i = 0; i < m_drives_count; i++) m_drives[i] = list[i];
    m_cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu", false));
    set_cell(CELL_TIME);
    m_time_valid = false;
    Apple2Disk2::reset(true);
    return emulator::Result::ok();
}

void Apple2Disk2::reset(bool cold)
{
    // The reset clears the card's latch (Q6, Q7, motor, drive 1), not the heads
    flush_track();
    m_motor = false;
    m_spin_left = 0;
    m_q6 = m_q7 = false;
    m_selected = 0;
    i_motor_on.change(0);
    i_select.change(0);
    if (cold) {
        m_addr = 0;
        m_latch = 0;
    }
}

// The bits of the track under the head of the selected drive, from its bytes
void Apple2Disk2::load_track()
{
    FDD * d = m_drives[m_selected];
    const int track = m_half_track[m_selected] / 2;
    const unsigned gen = (d != nullptr) ? d->get_generation() : 0;
    if (d != nullptr && d->is_woz()) {
        // The bits themselves, of the quarter track under the head: a half
        // track between two may hold a track of its own, or the one beside
        const int quarter = m_half_track[m_selected] * 2;
        const int index = d->woz_track(quarter);
        const int key = index >= 0 ? index : -2 - quarter;
        if (m_bits_woz && m_bits_drive == m_selected && m_bits_track == key && m_generation == gen) return;
        flush_track();
        const size_t old_len = m_bits.size();
        const unsigned old_pos = m_bit_pos;
        m_bits_woz = true;
        m_bits_drive = m_selected;
        m_bits_track = key;
        m_bits_quarter = quarter;
        m_generation = gen;
        d->read_bits(index, m_bits);
        m_bit_pos = old_len ? (unsigned)((uint64_t)old_pos * m_bits.size() / old_len) : old_pos;
        if (m_bit_pos >= m_bits.size()) m_bit_pos = 0;
        return;
    }
    if (!m_bits_woz && m_bits_drive == m_selected && m_bits_track == track && m_generation == gen) return;
    flush_track();
    const size_t old_len = m_bits.size();
    const unsigned old_pos = m_bit_pos;
    // The track being left goes to the cache, unless the disk was changed
    if (!m_bits_woz && m_bits_drive >= 0 && m_bits_track >= 0 && m_bits_track < 40 && m_generation == m_cache_gen[m_bits_drive]) {
        std::vector<std::vector<uint8_t>> &c = m_cache[m_bits_drive];
        if (c.size() < 40) c.resize(40);
        c[m_bits_track].swap(m_bits);
    }
    m_bits.clear();
    m_bits_woz = false;
    m_bits_drive = m_selected;
    m_bits_track = track;
    m_generation = gen;
    if (m_cache_gen[m_selected] != gen) {
        m_cache[m_selected].clear();
        m_cache_gen[m_selected] = gen;
    }
    std::vector<std::vector<uint8_t>> &cache = m_cache[m_selected];
    if (track < (int)cache.size() && !cache[track].empty()) {
        m_bits.swap(cache[track]);
        m_bit_pos = old_len ? (unsigned)((uint64_t)old_pos * m_bits.size() / old_len) : old_pos;
        if (m_bit_pos >= m_bits.size()) m_bit_pos = 0;
        return;
    }
    std::vector<uint8_t> bytes;
    if (d == nullptr || !d->read_track(track, 0, bytes) || bytes.empty()) {
        m_bit_pos = 0;
        return;
    }
    // A run of $FF of a gap is self-sync: ten bits a byte. The track keeps the
    // length the image gives it (a .nib track is longer than a revolution, and
    // shortening it broke disks that are not DOS ones)
    FDD::nibbles_to_bits(bytes, m_bits);
    // The disk keeps turning: the head comes down where it was on the other track
    m_bit_pos = old_len ? (unsigned)((uint64_t)old_pos * m_bits.size() / old_len) : old_pos;
    if (m_bit_pos >= m_bits.size()) m_bit_pos = 0;
}

// What was written goes back to the drive as bytes: each starts with a '1',
// the zeros between them are the timing of the self-sync bytes
void Apple2Disk2::flush_track()
{
    if (!m_dirty) return;
    m_dirty = false;
    if (m_bits_drive < 0 || m_bits.empty()) return;
    FDD * d = m_drives[m_bits_drive];
    if (d == nullptr) return;
    if (m_bits_woz) {
        // As it is: a WOZ disk keeps bits. A new track takes the place it was
        // written at (blank till now)
        if (!d->is_woz() || d->get_generation() != m_generation) return;
        const int index = d->write_bits(m_bits_quarter, m_bits_track, m_bits);
        if (index >= 0) m_bits_track = index;
        return;
    }
    std::vector<uint8_t> old;
    if (!d->read_track(m_bits_track, 0, old) || old.empty()) return;
    // Cut from a place where a byte surely begins: after the longest run of zeros
    const size_t n = m_bits.size();
    size_t start = 0, best = 0, run = 0;
    for (size_t i = 0; i < 2 * n; i++) {
        if (m_bits[i % n] == 0) {
            if (++run > best) { best = run; start = (i + 1) % n; }
        } else run = 0;
    }
    std::vector<uint8_t> bytes;
    size_t i = 0;
    while (i < n && bytes.size() < old.size()) {
        while (i < n && m_bits[(start + i) % n] == 0) i++;
        if (i >= n) break;
        uint8_t b = 0;
        for (int k = 0; k < 8; k++, i++) b = (uint8_t)((b << 1) | m_bits[(start + i) % n]);
        bytes.push_back(b);
    }
    while (bytes.size() < old.size()) bytes.push_back(0xFF);
    d->write_track(m_bits_track, 0, bytes);
    // The bytes are in the image now: the bits stand for them as they were
    m_generation = d->get_generation();
    m_cache_gen[m_bits_drive] = m_generation;
}

void Apple2Disk2::set_cell(unsigned timing)
{
    const uint64_t hz = (m_cpu != nullptr && m_cpu->clock > 0) ? m_cpu->clock : 1020484;
    m_cell_units = (uint64_t)timing * 2 * hz;
    if (m_step >= m_cell_units) m_step %= m_cell_units;
}

// The stepper: a half track of the head sits at phase (half track & 3), and
// the phase next to it, switched on, pulls the head half a track its way.
// Taken from the head, not from the last phase, so a reset or a seek that
// leaves phases on keeps the head and the program in step

void Apple2Disk2::phase_on(int n)
{
    int &ht = m_half_track[m_selected];
    if (n == ((ht + 1) & 3) && ht < 79) ht++;
    else if (n == ((ht + 3) & 3) && ht > 0) ht--;
    m_last_phase = n;
}

void Apple2Disk2::select_drive(int n)
{
    if (n == m_selected) return;
    flush_track();
    m_selected = n;
    i_select.change(n);
}

// The soft switches every access goes through, read or write alike
void Apple2Disk2::access(unsigned address)
{
    const unsigned a = address & 0x0F;
    switch (a) {
        case 0x0: case 0x2: case 0x4: case 0x6: break;
        case 0x1: case 0x3: case 0x5: case 0x7: phase_on(a >> 1); break;
        case 0x8:
            if (m_motor) m_spin_left = (m_cpu != nullptr && m_cpu->clock > 0) ? m_cpu->clock : 1000000;
            m_motor = false;
            break;
        case 0x9: m_motor = true; m_spin_left = 0; i_motor_on.change(1); break;
        case 0xA: case 0xB: select_drive(a & 1); break;
        case 0xC: m_q6 = false; m_idle_steps = 0; break;
        case 0xD: m_q6 = true; m_idle_steps = 0; break;
        case 0xE: if (m_q7) flush_track(); m_q7 = false; m_idle_steps = 0; break;
        default:  m_q7 = true; m_idle_steps = 0; break;
    }
}

// The sequencer runs at the processor's clock: up to the bus access when the
// program touches the card (the processor tells the moment, CPU::bus_time()),
// up to the processor's time when the device is clocked. A read takes effect
// at the end of its cycle, a write puts its byte on the bus for the steps of
// its own cycle
void Apple2Disk2::catch_up(bool write)
{
    uint64_t t = 0;
    if (m_cpu == nullptr || !m_cpu->bus_time(t)) return;
    (void)write;
    if (t > 0) t--;
    if (!m_time_valid) { m_time = t; m_time_valid = true; return; }
    if (t <= m_time) return;
    uint64_t n = t - m_time;
    m_time = t;
    while (n > 0) {
        const unsigned k = (n > 1000000) ? 1000000u : (unsigned)n;
        run(k);
        n -= k;
    }
}

unsigned int Apple2Disk2::get_value(unsigned int address)
{
    catch_up(false);
    access(address);
    return (address & 1) ? 0xFF : m_latch;
}

unsigned int Apple2Disk2::get_direct(unsigned int address)
{
    return (address & 1) ? 0xFF : m_latch;
}

void Apple2Disk2::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    catch_up(true);
    m_bus = (uint8_t)value;
    access(address);
}

// One step of the sequencer, as the card wires the PROM (341-0028; the layout
// MAME's wozfdc uses): address bit 4 is the read pulse, low while it lasts,
// bits 3, 2, 1 are Q7, Q6 and the top bit of the register, and the next state
// comes back in data bits 7, 6, 4, 5, put into address bits 7, 6, 5, 0. The
// low nibble of the data is what to do with the register
void Apple2Disk2::step_sequencer(bool pulse)
{
    const uint8_t * p6 = m_p6->get_buffer();
    m_addr = (uint8_t)((m_addr & 0xE1) | (pulse ? 0 : 0x10) | (m_q7 ? 0x08 : 0) | (m_q6 ? 0x04 : 0)
                     | ((m_latch & 0x80) ? 0x02 : 0));
    const uint8_t op = p6[m_addr];
    m_addr = (uint8_t)((op & 0xC0) | ((op & 0x10) << 1) | ((op & 0x20) >> 5) | (m_addr & 0x1E));
    switch (op & 0x0F) {
        case 0x8: case 0xC: break;                                          // NOP
        case 0x9: m_latch = (uint8_t)(m_latch << 1); break;                 // SL0
        case 0xD: m_latch = (uint8_t)((m_latch << 1) | 1); break;           // SL1
        case 0xA: case 0xE: {                                               // SR, write protect into bit 7
            FDD * d = m_drives[m_selected];
            const bool wp = d != nullptr && d->get_loaded() && d->is_protected();
            m_latch = (uint8_t)((m_latch >> 1) | (wp ? 0x80 : 0));
            break;
        }
        case 0xB: case 0xF: m_latch = m_bus; break;                         // LD
        default: m_latch = 0; break;                                        // CLR
    }
    // Writing: the current through the head flips with address bit 7, and a
    // flip is a '1' of the bit cell
    if (m_q7) {
        const bool wl = (m_addr & 0x80) != 0;
        if (wl != m_write_line) { m_write_line = wl; m_cell_flux = true; }
    }
}

void Apple2Disk2::run(unsigned int counter)
{
    if (!m_motor) {
        if (m_spin_left > counter) m_spin_left -= counter;
        else if (m_spin_left > 0) {
            m_spin_left = 0;
            flush_track();
            i_motor_on.change(0);
        }
    }
    if (!m_motor && m_spin_left == 0) {
        // The disk stands: no pulses, and a few steps settle the sequencer;
        // after that it is left alone (the register keeps what it has)
        if (m_idle_steps < 64) {
            for (unsigned s = 0; s < counter * 2 && m_idle_steps < 64; s++, m_idle_steps++) step_sequencer(false);
        }
        return;
    }
    m_idle_steps = 0;
    load_track();
    const bool have = !m_bits.empty();
    // The write protect switch of the drive cuts the write current: the cells
    // go by under the head, but nothing lands on the disk
    FDD * drive = m_drives[m_selected];
    const bool protect = drive != nullptr && drive->get_loaded() && drive->is_protected();
    for (unsigned s = 0; s < counter * 2; s++) {
        // A '1' under the head gives the sequencer one step of read pulse at
        // the start of its cell. Long without one, the read amplifier
        // (MC3470) turns up its gain and makes ones of the noise: past three
        // zeros a cell gives a pulse now and then
        bool pulse = false;
        if (!m_q7 && m_step < STEP_UNITS && have) {
            if (m_bits[m_bit_pos] != 0) { pulse = true; m_zeros = 0; }
            else if (++m_zeros > 3) {
                m_noise ^= m_noise << 13; m_noise ^= m_noise >> 17; m_noise ^= m_noise << 5;
                pulse = (m_noise % 10) < 3;
            }
        }
        step_sequencer(pulse);
        m_step += STEP_UNITS;
        const uint64_t cell = m_q7 ? WRITE_CELL : m_cell_units;
        if (m_step >= cell) {
            m_step -= cell;
            if (have) {
                if (m_q7 && !protect) {
                    m_bits[m_bit_pos] = m_cell_flux ? 1 : 0;
                    m_dirty = true;
                    m_writes++;
                }
                if (++m_bit_pos >= m_bits.size()) m_bit_pos = 0;
            }
            m_cell_flux = false;
        }
    }
}

void Apple2Disk2::clock(unsigned int counter)
{
    uint64_t t = 0;
    if (m_cpu != nullptr && m_cpu->bus_time(t)) catch_up(false);
    else run(counter);
}

std::vector<DeviceFieldInfo> Apple2Disk2::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = FDC::get_device_fields();
    r.push_back({"track",  "Half track the head of each drive stands on", true});
    r.push_back({"motor",  "1 while the motor runs",                     false});
    r.push_back({"q6",     "Q6",                                         false});
    r.push_back({"q7",     "Q7: 1 in the write mode",                    false});
    r.push_back({"latch",  "The shift register",                         false});
    r.push_back({"state",  "Sequencer PROM address (state and inputs)",  false});
    r.push_back({"bit",    "Position on the track, in bits",             false});
    r.push_back({"bits",   "Length of the track under the head, in bits", false});
    r.push_back({"writes", "Bits written",                               false});
    r.push_back({"trackbits", "Bits of the track under the head: trackbits(from, to)", true});
    return r;
}

bool Apple2Disk2::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "track") {
        if (to > 1) to = 1;
        if (from > to) from = to;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        for (unsigned i = from; i <= to; i++) out.values.push_back((unsigned)m_half_track[i]);
        return true;
    }
    if (field == "trackbits") {
        out.numeric = false;
        out.text.clear();
        for (unsigned i = from; i <= to && i < m_bits.size(); i++) out.text += m_bits[i] ? '1' : '0';
        return true;
    }
    out.numeric = true;
    if (field == "motor") { out.values.push_back((m_motor || m_spin_left > 0) ? 1 : 0); return true; }
    if (field == "q6")    { out.values.push_back(m_q6 ? 1 : 0); return true; }
    if (field == "q7")    { out.values.push_back(m_q7 ? 1 : 0); return true; }
    if (field == "latch") { out.values.push_back(m_latch); return true; }
    if (field == "state") { out.values.push_back(m_addr); return true; }
    out.width = 32;
    if (field == "bit")    { out.values.push_back(m_bit_pos); return true; }
    if (field == "bits")   { out.values.push_back((unsigned)m_bits.size()); return true; }
    if (field == "writes") { out.values.push_back(m_writes); return true; }
    out.width = 0;
    out.numeric = false;
    return FDC::get_field(field, from, to, out);
}

// The bits are made again from the drive; what is being written goes into the
// image before the snapshot takes it
void Apple2Disk2::save_state(StateWriter &w)
{
    flush_track();
    FDC::save_state(w);
    w.n("half_track0", (unsigned)m_half_track[0]);
    w.n("half_track1", (unsigned)m_half_track[1]);
    w.n("last_phase", (unsigned)(m_last_phase + 1));
    w.n("selected", (unsigned)m_selected);
    w.b("motor", m_motor);
    w.n64("spin_left", m_spin_left);
    w.b("q6", m_q6);
    w.b("q7", m_q7);
    w.u("latch", m_latch, 8);
    w.u("bus", m_bus, 8);
    w.u("addr", m_addr, 8);
    w.b("write_line", m_write_line);
    w.b("cell_flux", m_cell_flux);
    w.n64("cell_pos", m_step);
    w.n("zeros", m_zeros);
    w.n("noise", m_noise);
    w.n("bit_pos", m_bit_pos);
    w.n("writes", m_writes);
}

emulator::Result Apple2Disk2::load_state(const StateReader &r)
{
    emulator::Result res = FDC::load_state(r);
    if (!res) return res;
    unsigned v = 0;
    if (r.u("half_track0", v)) m_half_track[0] = (int)(v % 80);
    if (r.u("half_track1", v)) m_half_track[1] = (int)(v % 80);
    if (r.u("last_phase", v)) m_last_phase = (int)(v & 7) - 1;
    if (r.u("selected", v)) m_selected = (int)(v & 1);
    r.b("motor", m_motor);
    r.n64("spin_left", m_spin_left);
    r.b("q6", m_q6);
    r.b("q7", m_q7);
    if (r.u("latch", v)) m_latch = (uint8_t)v;
    if (r.u("bus", v)) m_bus = (uint8_t)v;
    if (r.u("addr", v)) m_addr = (uint8_t)v;
    r.b("write_line", m_write_line);
    r.b("cell_flux", m_cell_flux);
    if (!r.n64("cell_pos", m_step) && r.u("step", v)) m_step = (v % 8) * STEP_UNITS;   // a snapshot of steps
    if (m_step >= WRITE_CELL * 2) m_step = 0;
    r.u("zeros", m_zeros);
    if (r.u("noise", v) && v != 0) m_noise = v;
    unsigned pos = 0;
    r.u("bit_pos", pos);
    r.u("writes", m_writes);
    m_time_valid = false;
    // The track is read again from the restored drive, the head where it was
    m_cache[0].clear();
    m_cache[1].clear();
    m_bits.clear();
    m_bits_drive = m_bits_track = -1;
    m_bits_woz = false;
    m_dirty = false;
    m_bit_pos = pos;
    return emulator::Result::ok();
}

ComputerDevice * create_apple2_disk2(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Apple2Disk2(im, cd);
}
