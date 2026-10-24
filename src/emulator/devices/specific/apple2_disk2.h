// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Disk II controller of the Apple II / Правец with its logic state sequencer

#pragma once

#include <vector>

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

// The controller as it is built: the sequencer PROM P6 (341-0028) steps a
// state machine twice a processor clock, the drive turns a stream of bits
// under the head, a bit every 4 us (8 steps), and a '1' gives the sequencer a
// read pulse. What the program reads is the 8-bit shift register; what it
// writes, the sequencer shifts out to the drive. So a disk is read the way
// the card reads it, timing, self-sync bytes and all, and programs that time
// the disk themselves (copy protections, fast loaders) see what they expect.
//
//   $C0n0-$C0n7  stepper phases 0-3 off/on
//   $C0n8/9      motor off/on
//   $C0nA/B      drive 1/2
//   $C0nC/D      Q6 low/high, $C0nE/F Q7 low/high
//   an even address reads the shift register, any write puts its byte on
//   the bus the sequencer loads from
//
// The drive keeps its tracks as bytes (the nibble images of the fdd device):
// a track becomes bits when the head comes to it - a run of $FF of a gap is
// the 10-bit self-sync - and what was written is cut back into bytes, each
// starting with a '1', when the writing ends or the head leaves the track.
class Apple2Disk2 : public FDC
{
private:
    Interface i_select;
    Interface i_motor_on;

    ROM * m_p6 = nullptr;
    FDD * m_drives[2] = {nullptr, nullptr};
    int m_drives_count = 0;
    int m_selected = 0;
    int m_half_track[2] = {0, 0};   // head position in half tracks
    int m_last_phase = -1;
    bool m_motor = false;
    // The drive turns on for a second after the motor is switched off (a
    // timer of the card): DOS sees the disk still spinning and skips its wait
    uint64_t m_spin_left = 0;
    bool m_q6 = false, m_q7 = false;
    uint8_t m_latch = 0;            // the shift register
    uint8_t m_bus = 0;              // the last byte the processor put on the bus
    uint8_t m_addr = 0;             // the P6 address: the state and the inputs
    bool m_write_line = false;      // the write current, toggled by address bit 7
    bool m_cell_flux = false;       // a flux change written in this bit cell
    unsigned m_step = 0;            // sequencer steps into the current bit cell

    // The track under the head of the selected drive
    std::vector<uint8_t> m_bits;
    int m_bits_drive = -1;
    int m_bits_track = -1;
    unsigned m_bit_pos = 0;
    bool m_dirty = false;
    unsigned m_generation = 0;      // of the image the bits came from
    // The bits of every track the head has visited, as long as the disk stays
    // in: going back to a track finds them as they were. Made again from the
    // bytes they would round-trip through, a track would come back shifted
    std::vector<std::vector<uint8_t>> m_cache[2];
    unsigned m_cache_gen[2] = {0, 0};

    unsigned m_reads = 0;           // nibbles completed in the read mode
    unsigned m_writes = 0;          // bits written

    void phase_on(int n);
    void select_drive(int n);
    void load_track();
    void flush_track();
    void step_sequencer(bool pulse);
    void run(unsigned cycles);
    void catch_up(bool write);
    CPU * m_cpu = nullptr;
    uint64_t m_time = 0;            // processor clock the sequencer has been run up to
    bool m_time_valid = false;
    void access(unsigned address);

public:
    Apple2Disk2(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    bool get_busy() override { return m_motor; }
    unsigned int get_selected_drive() override { return (unsigned)m_selected; }
    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_apple2_disk2(InterfaceManager *im, EmulatorConfigDevice *cd);
