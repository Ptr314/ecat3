// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Поиск-1 video: display, video memory window, CGA trap

#include <cstring>

#include "poisk.h"
#include "emulator/utils.h"
#include "emulator/devices/common/wd1793.h"

namespace {

//The raster in processor clocks: 912 dots of 15 MHz are 304 clocks of 5 MHz
const unsigned LINE_CLOCKS   = 304;
const unsigned FRAME_LINES   = 262;
const unsigned FRAME_CLOCKS  = LINE_CLOCKS * FRAME_LINES;
const unsigned PICTURE_LINES = 200;
//640 dots are 213.3 clocks: the picture ends inside the 214th
const unsigned PICTURE_CLOCKS = 214;
const unsigned LINE_BYTES    = 80;
const unsigned VRAM_SIZE     = 0x8000;

//The CGA colours, RGBI with the dark yellow made brown
const uint8_t CGA_RGB[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}
};

inline unsigned background(unsigned mode) { return (mode & 7) | ((mode & 0x10) >> 1); }

}

//------------------------------ PoiskDisplay ---------------------------------

PoiskDisplay::PoiskDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , i_mode(this, im, 8, "mode", MODE_R)
    , i_control(this, im, 4, "control", MODE_R)
    , i_hsync(this, im, 1, "hsync", MODE_W)
    , i_vsync(this, im, 1, "vsync", MODE_W)
{
    m_clocked = true;
    //The picture lines are doubled: 200 lines of 640 dots look like a 4:3
    //screen only drawn twice as high
    sx = 640;
    sy = PICTURE_LINES * 2;
    for (unsigned i = 0; i < 16; i++) m_rgba[i] = 0;
    static_assert(PICTURE_ROWS == PICTURE_LINES, "one flag per picture line");
    memset(m_dirty, 0, sizeof(m_dirty));
    memset(m_line_key, 0, sizeof(m_line_key));
}

emulator::Result PoiskDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    const std::string ram = cd->get_parameter("ram", false).value;
    m_ram = dynamic_cast<RAM*>(im->dm->get_device_by_name(ram, false));
    if (m_ram == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskDisplay|" + std::string(QT_TRANSLATE_NOOP("PoiskDisplay", "The board RAM is expected in 'ram'")) + "} " + name);

    try {
        //A cycle that falls on a fetch of the video waits until the fetch is
        //over. Slots repeat every wait_period clocks along a picture line, the
        //video holds the first wait_busy of them; wait_blank = 1 keeps the
        //slots going in the blanking too (a refresh of the RAM chips)
        m_wait_period = read_confg_value(cd, "wait_period", false, (unsigned int)0);
        m_wait_busy   = read_confg_value(cd, "wait_busy", false, (unsigned int)0);
        m_wait_blank  = read_confg_value(cd, "wait_blank", false, false);
    } catch (std::exception &) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskDisplay|" + std::string(QT_TRANSLATE_NOOP("PoiskDisplay", "Incorrect parameters for")) + "} " + name);
    }
    if (m_wait_period == 0 || m_wait_busy >= m_wait_period) m_wait_busy = 0;
    //The table keeps a wait in a byte
    if (m_wait_busy > 255)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskDisplay|" + std::string(QT_TRANSLATE_NOOP("PoiskDisplay", "Incorrect parameters for")) + "} " + name + ": wait_busy");

    //The waits of a cycle that begins at each clock of a picture line, so
    //that the question asked on every memory cycle costs no division
    m_wait_line.assign(LINE_CLOCKS, 0);
    if (m_wait_busy != 0)
        for (unsigned x = 0; x < LINE_CLOCKS; x++) {
            const unsigned phase = x % m_wait_period;
            if ((m_wait_blank || x < PICTURE_CLOCKS) && phase < m_wait_busy)
                m_wait_line[x] = (uint8_t)(m_wait_busy - phase);
        }
    m_pos = 0;
    return emulator::Result::ok();
}

//The beam is not stopped by a reset; the lines are driven here because only
//now is everything wired to them, and they have to start from a real level
void PoiskDisplay::reset(bool cold)
{
    GenericDisplay::reset(cold);
    m_full = true;
    update_sync(true);
}

void PoiskDisplay::state_restored()
{
    GenericDisplay::state_restored();
    m_full = true;
}

void PoiskDisplay::vram_written(unsigned offset)
{
    //Each 16 KB page holds the even lines from its start and the odd ones
    //from 2000h, 80 bytes a line; the last 192 bytes of each half show nothing
    const unsigned page = (offset >> 14) & 1;
    const unsigned in_half = offset & 0x1FFF;
    if (in_half >= (PICTURE_LINES / 2) * LINE_BYTES) return;
    const unsigned line = (in_half / LINE_BYTES) * 2 + ((offset >> 13) & 1);
    m_dirty[page][line] = true;
}

void PoiskDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    vr.FillRGB(CGA_RGB, m_rgba, 16);
}

void PoiskDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

//The video memory is the top of the RAM, whatever its size: the RAM has been
//sized by the time the first line is drawn
static inline unsigned vram_base(RAM * ram)
{
    const unsigned size = ram->get_size();
    return (size >= VRAM_SIZE) ? size - VRAM_SIZE : 0;
}

void PoiskDisplay::update_sync(bool force)
{
    const unsigned line = m_pos / LINE_CLOCKS;
    const unsigned x = m_pos % LINE_CLOCKS;
    const unsigned v = (line >= PICTURE_LINES) ? 1 : 0;
    const unsigned h = (v != 0 || x >= PICTURE_CLOCKS) ? 1 : 0;
    if (force || (i_vsync.value & 1) != v) i_vsync.change(v);
    if (force || (i_hsync.value & 1) != h) i_hsync.change(h);
}

//The beam moves on by counter processor clocks. A line is drawn when the beam
//leaves its picture part, with the mode and the memory of that moment
void PoiskDisplay::clock(unsigned int counter)
{
    while (counter > 0) {
        const unsigned line = m_pos / LINE_CLOCKS;
        const unsigned x = m_pos % LINE_CLOCKS;
        const unsigned next = (x < PICTURE_CLOCKS) ? PICTURE_CLOCKS - x : LINE_CLOCKS - x;
        const unsigned step = (counter < next) ? counter : next;
        m_pos += step;
        counter -= step;
        if (step != next) break;
        if (x + step == PICTURE_CLOCKS) {
            if (line < PICTURE_LINES) render_line(line);
        } else if (line + 1 == PICTURE_LINES) {
            m_frames++;
            was_updated = true;
            m_full = ++m_since_full >= FULL_EVERY;
            if (m_full) m_since_full = 0;
        } else if (line + 1 == FRAME_LINES) {
            m_pos = 0;
        }
    }
    update_sync();
}

//The processor runs a memory cycle offset clocks after the point the display
//has been clocked to. With the beam inside the picture, slots of the RAM go
//by every wait_period clocks from the start of the line and the video owns the
//first wait_busy clocks of each: a cycle that begins there waits until the
//slot is over
unsigned int PoiskDisplay::wait_states(MAYBE_UNUSED unsigned int address, MAYBE_UNUSED bool write, unsigned int offset)
{
    if (m_wait_busy == 0) return 0;
    unsigned p = m_pos + offset;
    if (p >= FRAME_CLOCKS) p %= FRAME_CLOCKS;
    const unsigned line = p / LINE_CLOCKS;
    if (!m_wait_blank && line >= PICTURE_LINES) return 0;
    const unsigned w = m_wait_line[p - line * LINE_CLOCKS];
    m_waits += w;
    return w;
}

void PoiskDisplay::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid)
        for (unsigned line = 0; line < PICTURE_LINES; line++)
            render_line_unlocked(line);
    screen_valid = true;
    was_updated = true;
}

void PoiskDisplay::render_line(unsigned line)
{
    const unsigned mode = i_mode.value & 0xFF;
    const unsigned key = mode | ((i_control.value & 0x0F) << 8);
    const unsigned page = (mode & 0x40) ? 1 : 0;
    if (!m_full && !m_dirty[page][line] && m_line_key[line] == key) return;
    m_dirty[page][line] = false;
    m_line_key[line] = key;
    compat_lock_guard guard(m_surface_mutex);
    render_line_unlocked(line);
}

void PoiskDisplay::render_line_unlocked(unsigned line)
{
    if (!has_valid_renderer() || render_pixels == nullptr || m_ram == nullptr) return;
    if ((int)(sx * 4) > line_bytes) return;

    const unsigned mode = i_mode.value & 0xFF;
    const unsigned page = (mode & 0x40) ? 0x4000 : 0;
    //Even lines from the start of the page, odd ones from 2000h on
    const unsigned src = vram_base(m_ram) + page + (line & 1) * 0x2000 + (line >> 1) * LINE_BYTES;
    if (src + LINE_BYTES > m_ram->get_size()) return;
    const uint8_t * video = m_ram->get_buffer() + src;

    uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + line * 2 * line_bytes);
    const unsigned bg = background(mode);

    if (mode & 0x80) {
        if ((i_control.value & 0x08) != 0) {
            //Plain CGA: white on the background
            for (unsigned i = 0; i < LINE_BYTES; i++) {
                const uint8_t b = video[i];
                for (unsigned k = 0; k < 8; k++)
                    *p++ = m_rgba[(b & (0x80 >> k)) ? 15 : bg];
            }
        } else {
            //The text of the BIOS: the first dot of a byte is always the
            //background, bit 7 picks a highlighted colour for the other seven.
            //A photo of a real screen (Volkov Commander in the mode the BIOS
            //sets, bit 5 of 68h clear) shows the highlight light green, and
            //thin dark gaps between the cells of an inverse bar - that first
            //dot. MAME has the two colours the other way round
            for (unsigned i = 0; i < LINE_BYTES; i++) {
                const uint8_t b = video[i];
                const unsigned fg = (b & 0x80) ? ((mode & 0x20) ? 11 : 10) : 15;
                *p++ = m_rgba[bg];
                for (unsigned k = 1; k < 8; k++)
                    *p++ = m_rgba[(b & (0x80 >> k)) ? fg : bg];
            }
        }
    } else {
        unsigned lut[4];
        lut[0] = bg;
        if (mode & 0x20) { lut[1] = 11; lut[2] = 13; lut[3] = 15; }
        else             { lut[1] = 2;  lut[2] = 4;  lut[3] = 6; }
        for (unsigned i = 0; i < LINE_BYTES; i++) {
            const uint8_t b = video[i];
            for (unsigned k = 0; k < 4; k++) {
                const uint32_t c = m_rgba[lut[(b >> (6 - k * 2)) & 3]];
                *p++ = c;
                *p++ = c;
            }
        }
    }

    //Each line is shown twice
    memcpy(static_cast<uint8_t*>(render_pixels) + (line * 2 + 1) * line_bytes,
           static_cast<uint8_t*>(render_pixels) + line * 2 * line_bytes, sx * 4);
}

std::vector<DeviceFieldInfo> PoiskDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"line",    "Raster line the beam is on, 0-261", false});
    r.push_back({"column",  "Processor clocks since the start of the line", false});
    r.push_back({"frames",  "Frames drawn since the start", false});
    r.push_back({"waits",   "Wait states the processor got from the video, low 32 bits", false});
    r.push_back({"mode",    "Mode register (port 68h)", false});
    return r;
}

bool PoiskDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "line" || field == "column" || field == "frames" || field == "waits" || field == "mode") {
        out.numeric = true;
        if (field == "line") out.values.push_back(m_pos / LINE_CLOCKS);
        else if (field == "column") out.values.push_back(m_pos % LINE_CLOCKS);
        else if (field == "frames") out.values.push_back(m_frames);
        else if (field == "waits") { out.width = 32; out.values.push_back((unsigned)m_waits); }
        else { out.width = 8; out.values.push_back(i_mode.value & 0xFF); }
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

void PoiskDisplay::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    w.n("pos", m_pos);
    w.n("frames", m_frames);
    w.n64("waits", m_waits);
}

emulator::Result PoiskDisplay::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.u("pos", m_pos);
    if (m_pos >= FRAME_CLOCKS) m_pos = 0;
    r.u("frames", m_frames);
    r.n64("waits", m_waits);
    return emulator::Result::ok();
}

//------------------------------- PoiskTrap -----------------------------------

PoiskTrap::PoiskTrap(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_nmi(this, im, 1, "nmi", MODE_W)
{
    addresable_size = 0x20;
    can_read = true;
    can_write = true;
}

void PoiskTrap::reset(bool cold)
{
    AddressableDevice::reset(cold);
    i_nmi.change(0);
}

void PoiskTrap::latch(uint8_t lo, uint8_t mode, uint8_t data)
{
    m_regs[0] = lo;
    m_regs[1] = mode;
    m_regs[2] = data;
    m_count++;
    i_nmi.change(1);
}

void PoiskTrap::vram_write(unsigned offset, uint8_t data)
{
    latch((uint8_t)offset, (uint8_t)(0x80 | ((offset >> 8) & 0x3F)), data);
}

static inline bool trapped_cga_port(unsigned k) { return k == 4 || k == 5 || k == 8 || k == 9; }

unsigned int PoiskTrap::get_value(unsigned int address)
{
    if (address < 3) {
        const uint8_t v = m_regs[address];
        if (address == 0) i_nmi.change(0);
        return v;
    }
    const unsigned k = address - 0x10;
    if (address >= 0x10 && trapped_cga_port(k)) latch((uint8_t)(0xD0 + k), 0x43, 0);
    return 0;
}

unsigned PoiskTrap::get_direct(unsigned address)
{
    return (address < 3) ? m_regs[address] : 0;
}

void PoiskTrap::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    const unsigned k = address - 0x10;
    if (address >= 0x10 && trapped_cga_port(k)) latch((uint8_t)(0xD0 + k), 0xC3, (uint8_t)value);
}

std::vector<DeviceFieldInfo> PoiskTrap::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"regs",  "Ports 28h-2Ah: address, kind, data of the last trap", false});
    r.push_back({"count", "Traps since the start", false});
    r.push_back({"nmi",   "1 while the NMI is asserted", false});
    return r;
}

bool PoiskTrap::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "regs") {
        out.numeric = true;
        out.width = 8;
        for (unsigned i = 0; i < 3; i++) out.values.push_back(m_regs[i]);
        return true;
    }
    if (field == "count" || field == "nmi") {
        out.numeric = true;
        out.values.push_back(field == "count" ? m_count : (i_nmi.value & 1));
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

void PoiskTrap::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.array("regs", m_regs, 3);
    w.n("count", m_count);
}

emulator::Result PoiskTrap::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.array("regs", m_regs, 3);
    r.u("count", m_count);
    return emulator::Result::ok();
}

//------------------------------- PoiskVram -----------------------------------

PoiskVram::PoiskVram(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_trap_off(this, im, 1, "trap_off", MODE_R)
{
    addresable_size = VRAM_SIZE;
    can_read = true;
    can_write = true;
}

emulator::Result PoiskVram::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_display = dynamic_cast<PoiskDisplay*>(im->dm->get_device_by_name(cd->get_parameter("display", false).value, false));
    if (m_display == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskVram|" + std::string(QT_TRANSLATE_NOOP("PoiskVram", "The display is expected in 'display'")) + "} " + name);
    const std::string trap = cd->get_parameter("trap", false).value;
    if (!trap.empty()) {
        m_trap = dynamic_cast<PoiskTrap*>(im->dm->get_device_by_name(trap, false));
        if (m_trap == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{PoiskVram|" + std::string(QT_TRANSLATE_NOOP("PoiskVram", "Not a trap device")) + "} " + name + ": " + trap);
    }
    return emulator::Result::ok();
}

unsigned int PoiskVram::get_value(unsigned int address)
{
    RAM * ram = m_display->video_ram();
    return ram->get_value(vram_base(ram) + (address & (VRAM_SIZE - 1)));
}

unsigned PoiskVram::get_direct(unsigned address)
{
    RAM * ram = m_display->video_ram();
    return ram->get_direct(vram_base(ram) + (address & (VRAM_SIZE - 1)));
}

//The byte is written and the trap goes off after it: the NMI handler of the
//BIOS reads the latched address and data and draws the character
void PoiskVram::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    RAM * ram = m_display->video_ram();
    const unsigned a = address & (VRAM_SIZE - 1);
    ram->set_value(vram_base(ram) + a, value);
    m_display->vram_written(a);
    if (m_trap != nullptr && a < 0x4000 && (i_trap_off.value & 1) == 0)
        m_trap->vram_write(a, (uint8_t)value);
}

//---------------------------- PoiskFdcControl --------------------------------

PoiskFdcControl::PoiskFdcControl(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_value(this, im, 8, "value", MODE_W)
    , i_motor(this, im, 2, "motor", MODE_W)
{
    addresable_size = 4;
    can_read = true;
    can_write = true;
    m_clocked = true;
}

emulator::Result PoiskFdcControl::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    const std::string fdc = read_confg_value(cd, "fdc", false, std::string("fdc"));
    m_fdc = dynamic_cast<WD1793*>(im->dm->get_device_by_name(fdc, false));
    if (m_fdc == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskFdcControl|" + std::string(QT_TRANSLATE_NOOP("PoiskFdcControl", "Not a wd1793 device")) + "} " + name + ": " + fdc);
    unsigned ms;
    try {
        ms = read_confg_value(cd, "motor_time", false, (unsigned int)2000);
    } catch (std::exception &) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskFdcControl|" + std::string(QT_TRANSLATE_NOOP("PoiskFdcControl", "Incorrect parameters for")) + "} " + name);
    }
    const uint64_t clocks = (uint64_t)m_system_clock * ms / 1000;
    if (clocks > 0xFFFFFFFFu)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{PoiskFdcControl|" + std::string(QT_TRANSLATE_NOOP("PoiskFdcControl", "Incorrect parameters for")) + "} " + name + ": motor_time");
    m_motor_time = (unsigned)clocks;
    return emulator::Result::ok();
}

//No drive and no motor: the lines start from a real level, now that the
//drives are wired to them
void PoiskFdcControl::reset(bool cold)
{
    AddressableDevice::reset(cold);
    m_motor_left = 0;
    i_value.change(0);
    i_motor.change(0);
}

//A drive turns while its motor bit is set and the one-shot is running
void PoiskFdcControl::update_motor()
{
    const unsigned m = (m_motor_left > 0) ? ((i_value.value >> 2) & 3) : 0;
    if ((i_motor.value & 3) != m) i_motor.change(m);
}

void PoiskFdcControl::clock(unsigned int counter)
{
    if (m_motor_left == 0) return;
    m_motor_left = (counter >= m_motor_left) ? 0 : m_motor_left - counter;
    if (m_motor_left == 0) update_motor();
}

unsigned int PoiskFdcControl::get_value(unsigned int address)
{
    switch (address & 3) {
    case 0:
        m_fdc->wait_ready();
        return m_fdc->drq_active() ? 1 : 0;
    case 2:
        return (m_motor_left == 0) ? 1 : 0;
    default:
        return 0xFF;
    }
}

unsigned PoiskFdcControl::get_direct(unsigned address)
{
    switch (address & 3) {
    case 0: return m_fdc->drq_active() ? 1 : 0;
    case 2: return (m_motor_left == 0) ? 1 : 0;
    default: return 0xFF;
    }
}

//D6 is the MR input of the КР1818ВГ93. While it is low the chip stops what
//it does; when it goes high it runs a Restore by itself - which is what the
//ROM waits for after a reset: TR00 without BUSY in the status. A write to C6h
//starts the motor one-shot again
void PoiskFdcControl::set_value(unsigned int address, unsigned int value, bool force)
{
    //A forced write sets the register and nothing more: no line moves, the
    //controller is not reset and the motor is not started
    if (force) {
        if ((address & 3) == 0) i_value.value = value & 0xFF;
        return;
    }
    switch (address & 3) {
    case 0: {
        const unsigned old = i_value.value;
        i_value.change(value & 0xFF);
        if ((old & 0x40) != 0 && (value & 0x40) == 0)
            m_fdc->set_value(0, 0xD0);
        else if ((old & 0x40) == 0 && (value & 0x40) != 0) {
            //The reset leaves 01h in the sector register (datasheet, MR)
            m_fdc->set_value(2, 0x01);
            m_fdc->set_value(0, 0x03);
        }
        break;
    }
    case 2:
        m_motor_left = (m_motor_time > 0) ? m_motor_time : 1;
        break;
    default:
        return;
    }
    update_motor();
}

std::vector<DeviceFieldInfo> PoiskFdcControl::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"control", "Last value written to port C4h", false});
    r.push_back({"motor",   "Motors turning: bit 0 drive 0, bit 1 drive 1", false});
    return r;
}

bool PoiskFdcControl::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "control" || field == "motor") {
        out.numeric = true;
        out.width = 8;
        out.values.push_back(field == "control" ? (i_value.value & 0xFF) : (i_motor.value & 3));
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

void PoiskFdcControl::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.n("motor_left", m_motor_left);
}

emulator::Result PoiskFdcControl::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("motor_left", m_motor_left);
    return emulator::Result::ok();
}

ComputerDevice * create_poisk_fdc_control(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PoiskFdcControl(im, cd);
}

ComputerDevice * create_poisk_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PoiskDisplay(im, cd);
}

ComputerDevice * create_poisk_trap(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PoiskTrap(im, cd);
}

ComputerDevice * create_poisk_vram(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PoiskVram(im, cd);
}
