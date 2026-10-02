// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Поиск-1 video: display, video memory window, CGA trap

#pragma once

#include "emulator/core.h"

// The video of the Поиск-1 is built on gate arrays, not on a 6845, and has no
// text mode: the picture is always graphics, read from the top 32 KB of the
// main RAM. The BIOS draws the text itself and emulates the CGA registers by a
// trap (poisk-trap): a program touching them gets an NMI.
//
// Raster as MAME has it: a 15 MHz dot clock, 912 dots by 262 lines, of which
// 640 by 200 are the picture - 304 processor clocks (5 MHz) a line, 62.8 Hz.
//
// Mode register, port 68h (port A of the second ВВ55), here the line "mode":
//   bits 0-2  background colour, bit 4 its intensity
//   bit 3     1: writes to B8000-BBFFF do not trap
//   bit 5     palette of the 320 dot mode
//   bit 6     page: the second 16 KB is shown
//   bit 7     640 dots, one bit each
// Port 6Ah bit 7 (bit 3 of the upper half of port C of the first ВВ55, the line
// "control"): in the 640 dot mode, 1 is the plain CGA picture, 0 the mode the
// BIOS draws its text in - seven dots a byte, bit 7 picks the colour.
//
// The video fetches from the same RAM chips the processor runs from, and a
// processor cycle that falls on a fetch waits for it. The display is the
// WaitSource of the whole board RAM ("wait = display" on the ram device);
// a RAM on a cartridge names none and runs at full speed. How the slots are
// shared on the real board is not documented: wait_period, wait_busy and
// wait_blank describe a model of it, see wait_states()
class PoiskDisplay: public GenericDisplay, public WaitSource
{
private:
    Interface i_mode;
    Interface i_control;
    Interface i_hsync;      //1 outside the picture: horizontal or vertical blanking
    Interface i_vsync;      //1 in the vertical blanking

    RAM * m_ram = nullptr;
    unsigned m_base = 0;    //of the video memory in m_ram

    uint32_t m_pos = 0;     //processor clocks since the start of the frame
    unsigned m_frames = 0;
    uint64_t m_waits = 0;   //wait states given since the start

    unsigned m_wait_period = 0;
    unsigned m_wait_busy = 0;
    bool m_wait_blank = false;
    std::vector<uint8_t> m_wait_line;   //waits of a cycle beginning at each clock of a line

    uint32_t m_rgba[16];

    //A line is drawn again only when it can look different: its bytes were
    //written (through the B8000 window, the only way the processor reaches
    //them), or the mode or the palette changed since it was drawn. Every
    //FULL_EVERY frames the whole picture is drawn anyway, which also covers a
    //write that went around the window (COMMAND ram0.set from a script)
    enum { PICTURE_ROWS = 200, FULL_EVERY = 50 };
    bool m_dirty[2][PICTURE_ROWS];      //[page][line], written since drawn
    unsigned m_line_key[PICTURE_ROWS];  //mode and control it was drawn with
    bool m_full = true;
    unsigned m_since_full = 0;

    void render_line(unsigned line);
    void render_line_unlocked(unsigned line);
    void update_sync(bool force = false);

protected:
    void render_all(bool force_render) override;

public:
    PoiskDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void set_renderer(VideoRenderer &vr) override;
    void clock(unsigned int counter) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    unsigned int wait_states(unsigned int address, bool write, unsigned int offset) override;

    //The video memory as the processor sees it at B8000: the top of the RAM
    RAM * video_ram() const { return m_ram; }
    //A byte of the 32 KB at B8000 was written
    void vram_written(unsigned offset);
    void state_restored() override;
    unsigned video_base() const { return m_base; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// The trap of the CGA emulation. Ports 28h-2Ah hold what the last trapped
// access was: the low byte of the address, its high bits with the kind of the
// access, the data. 43h in 29h is a read of a CGA port, C3h a write, 80h plus
// the address bits a write to the video memory. A read of 28h takes the NMI
// away. The CGA ports 3D4h, 3D5h, 3D8h, 3D9h are mapped here at offset 10h
// (3D0h-3DFh), the trap registers at 0
class PoiskTrap: public AddressableDevice
{
private:
    Interface i_nmi;
    uint8_t m_regs[3] = {0, 0, 0};
    unsigned m_count = 0;

    void latch(uint8_t lo, uint8_t mode, uint8_t data);

public:
    PoiskTrap(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    //A write to B8000-BBFFF with the trap on
    void vram_write(unsigned offset, uint8_t data);

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

// The window B8000-BFFFF: the bytes of the top 32 KB of the board RAM (18000
// with 128 KB, 78000 with 512 KB). The processor reaches them only here: the
// map leaves their own address out, as the BIOS expects - it sizes the RAM by
// probing 16 KB blocks and stops right below them. A write to the first 16 KB
// through the window traps unless bit 3 of the mode register ("~trap_off") is
// set. It waits the way the RAM does: name the display in "wait"
class PoiskVram: public AddressableDevice
{
private:
    Interface i_trap_off;
    PoiskDisplay * m_display = nullptr;
    PoiskTrap * m_trap = nullptr;

public:
    PoiskVram(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
};

class WD1793;

// The control register of the B504 floppy cartridge, ports C4h-C7h (the
// КР1818ВГ93 itself sits at C0h-C3h). A write to C4h:
//   D0, D1  drive 0, drive 1      D2, D3  motor of drive 0, drive 1
//   D4      side 1                D5      double density
//   D6      MR of the controller: low stops it, rising runs a Restore
// goes out on the line "value". A read of C4h answers DRQ in bit 0 - and on
// the board holds the processor until DRQ or INTRQ comes; the emulator runs the
// controller forward to that point instead.
//
// The motors run off a one-shot: a write to C6h starts it again, and it keeps
// the motor of a drive whose D2/D3 is set turning for motor_time ms (line
// "motor"). Bit 0 of a read of C6h is 1 when the one-shot has run out - the
// drivers wait for the motor to spin up then. How long it lasts on the board is
// not known; 2 s, like the motor timeout of an IBM PC
class PoiskFdcControl: public AddressableDevice
{
private:
    Interface i_value;
    Interface i_motor;          //motors of drive 0 and 1, 1: turning
    WD1793 * m_fdc = nullptr;
    unsigned m_motor_time = 0;  //clocks the one-shot lasts
    unsigned m_motor_left = 0;  //clocks until it runs out, 0: stopped

    void update_motor();

public:
    PoiskFdcControl(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    unsigned int get_value(unsigned int address) override;
    unsigned get_direct(unsigned address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_poisk_fdc_control(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_poisk_display(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_poisk_trap(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_poisk_vram(InterfaceManager *im, EmulatorConfigDevice *cd);
