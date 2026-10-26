// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: wd1793 FDC device

#include <cstring>
#include <cstdlib>

#include "wd1793.h"
#include "emulator/utils.h"

WD1793::WD1793(InterfaceManager *im, EmulatorConfigDevice *cd):
      FDC(im, cd)
    , drives_count(0)
    , selected_drive(-1)
    , command(0)
    , delay(0)
    , register_delay(0)
    , sector_size(0)
    , bytes(0)
    , step_dir(1)
    , sync_mask(0)
    , hld(false)
    , hld_timer(0)
    , sectors_read(0)
    , sectors_written(0)
    , intrq_forced(false)
    , i_address(this, im, 2, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_INTRQ(this, im, 1, "intrq", MODE_W)
    , i_DRQ(this, im, 1, "drq", MODE_W)
    , i_HLD(this, im, 1, "hld", MODE_W)
{
    m_clocked = true;   //clock() is overridden here
    memset(&registers, 0, sizeof(registers));
}

emulator::Result WD1793::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    //Four drive selects
    std::vector<FDD*> list;
    res = load_drives(1, sizeof(drives)/sizeof(drives[0]), list);
    if (!res) return res;
    drives_count = list.size();
    for (unsigned int i = 0; i < drives_count; i++) drives[i] = list[i];

    // Address bits selecting a copy of the data register whose access waits
    // for DRQ/INTRQ (the Irisha KNGMD holds READY low on port 37 this way)
    sync_mask = read_confg_value(cd, "sync", false, (unsigned int)0);

    m_register_delay = read_confg_value(cd, "register_delay", false, (unsigned int)wd1793_DELAY_REGISTER);
    m_physical = read_confg_value(cd, "physical_head", false, false);
    m_max_cylinder = read_confg_value(cd, "max_cylinder", false, (unsigned int)83);
    // index_period in ms of the controller's clock, index_width in us
    const unsigned period_ms = read_confg_value(cd, "index_period", false, (unsigned int)0);
    const unsigned width_us = read_confg_value(cd, "index_width", false, (unsigned int)4000);
    m_index_period = (unsigned)((uint64_t)m_system_clock * period_ms / 1000);
    m_index_width = (unsigned)((uint64_t)m_system_clock * width_us / 1000000);
    m_track_length = read_confg_value(cd, "track_length", false, (unsigned int)wd1793_TRACK_LENGTH);

    return emulator::Result::ok();
}

void WD1793::SetDRQ()
{
    SetFlag(wd1793_FLAG_DRQ);
    i_DRQ.change(1);
}

bool WD1793::GetDRQ()
{
    return (registers[wd1793_REG_STATUS] & wd1793_FLAG_DRQ) != 0;
}

void WD1793::ClearDRQ()
{
    ClearFlag(wd1793_FLAG_DRQ);
    i_DRQ.change(0);
}

void WD1793::SetFlag(unsigned int flag)
{
    registers[wd1793_REG_STATUS] |= flag;
}

void WD1793::ClearFlag(unsigned int flag)
{
    registers[wd1793_REG_STATUS] &= ~flag;
}

void WD1793::SetINTRQ()
{
    i_INTRQ.change(1);
}

void WD1793::ClearINTRQ()
{
    i_INTRQ.change(0);
}

// The HLD output: asserted by type I commands with the h flag and by every
// type II/III command, released some time after the last command completes
void WD1793::SetHLD(bool value)
{
    if (hld != value) {
        hld = value;
        i_HLD.change(value ? 1 : 0);
    }
    hld_timer = value ? wd1793_DELAY_HLD_RELEASE : 0;
}

void WD1793::FindSelectedDrive()
{
    for (unsigned int i=0; i < drives_count; i++)
        if (drives[i]->is_selected())
        {
            selected_drive = i;
            return void();
        }
    selected_drive = -1;
}

void WD1793::WriteRegister(unsigned int address, unsigned int  value)
{
    unsigned int a = address & 3;
    if (a==wd1793_REG_COMMAND)
    {
        //Command register
        registers[4] = value;
        if (!intrq_forced) ClearINTRQ();
        FindSelectedDrive();
        unsigned int c = (value & 0xF0) >> 4;
        m_type1 = c < 0x08 || c == 0x0D;
        //Any command, Force Interrupt included, ends a Read Address
        m_reading_id = false;
        //Execute commands
        switch (c) {
        case 0x00: //Restore
            delay = wd1793_DELAY_RESTORE;
            command = wd1793_COMMAND_RESTORE;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x01: //Seek
            delay = wd1793_DELAY_STEP * abs((int)registers[wd1793_REG_TRACK] - (int)registers[wd1793_REG_DATA]);
            //A seek to the track the head is on still keeps the chip busy for
            //the settling time: the Liberty Drive ROM waits for BUSY to rise
            if (m_physical && delay < wd1793_DELAY_STEP) delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_SEEK;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x02:
        case 0x03: //Step
            delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_STEP;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x04:
        case 0x05: //Step-In
            step_dir = 1;
            delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_STEP;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x06:
        case 0x07: //Step-Out
            step_dir = -1;
            delay = wd1793_DELAY_STEP;
            command = wd1793_COMMAND_STEP;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        //With the m flag both go on to the next sector until the sector
        //register runs past the track, which ends the command with Record
        //Not Found - see the end of a sector in ExecuteCommand()
        case 0x08:
        case 0x09: //Read sector
            delay = wd1793_DELAY_SECTOR;
            command = wd1793_COMMAND_READ_SECTOR;
            SetFlag(wd1793_FLAG_BUSY);
            SetHLD(true);
            break;

        case 0x0A:
        case 0x0B: //Write sector
            delay = wd1793_DELAY_SECTOR;
            command = wd1793_COMMAND_WRITE_SECTOR;
            SetFlag(wd1793_FLAG_BUSY);
            SetHLD(true);
            break;

        case 0x0C: //Read address
            delay = wd1793_DELAY_SECTOR;
            command = wd1793_COMMAND_READ_ADDRESS;
            SetFlag(wd1793_FLAG_BUSY);
            SetHLD(true);
            break;

        case 0x0E: //Read track
            note_unsupported("wd1793: Read Track");
            SetHLD(true);
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0F: //Write track
            delay = wd1793_DELAY_SECTOR;
            command = wd1793_COMMAND_WRITE_TRACK;
            SetFlag(wd1793_FLAG_BUSY);
            SetHLD(true);
            break;

        case 0x0D: //Force interrupt
            ClearFlag(wd1793_FLAG_BUSY);
            command = 0;
            delay = 0;
            ClearDRQ();
            //I3 - an interrupt at once, which neither a status read nor the
            //next command clears: only a Force Interrupt without conditions
            //does. Copiers and BIOSes reset the controller with $D8
            intrq_forced = (value & 0x08) != 0;
            if (intrq_forced) SetINTRQ();
            //I2 - on every index pulse: there are no index pulses here, so
            //once, as soon as the first would come. I0/I1 - on a change of
            //the ready line, which no drive here ever changes
            else if ((value & 0x04) != 0) SetINTRQ();
            //No condition: the command ends without an interrupt, and this
            //is what releases one forced by I3
            else ClearINTRQ();
            if ((value & 0x07) != 0)
                note_unsupported("wd1793: Force Interrupt on index or ready");
            break;
        }
    } else {
        registers[a] = value;
    }
    if (a==wd1793_REG_DATA) ClearDRQ();
}

bool WD1793::get_busy()
{
    return (registers[wd1793_REG_STATUS] & wd1793_FLAG_BUSY) != 0;
}

unsigned int WD1793::get_selected_drive()
{
    return selected_drive;
}

// Models a data register whose access holds the CPU until the controller
// raises DRQ or INTRQ. The emulator cannot stall the CPU, so the controller
// is run forward instead until the byte the CPU waits for is ready.
void WD1793::SyncAccess()
{
    if (register_delay > 0)
    {
        register_delay = 0;
        WriteRegister(register_to_write, value_to_write);
    }
    for (int guard = 0; guard < 8; guard++)
    {
        if (command == 0 || GetDRQ() || (i_INTRQ.value & 1) != 0) break;
        delay = 0;
        ExecuteCommand();
    }
}

unsigned int WD1793::get_value(unsigned int address)
{
    unsigned int a = address & 0x03;
    if (a==wd1793_REG_DATA && (address & sync_mask) != 0) SyncAccess();
    if (a==wd1793_REG_DATA) ClearDRQ();
    if (a==wd1793_REG_STATUS && !intrq_forced) ClearINTRQ();
    if (a==wd1793_REG_STATUS && m_index_period > 0 && m_type1) {
        const bool disk = selected_drive >= 0 && drives[selected_drive]->get_loaded();
        const bool index = disk && m_index_clock < m_index_width;
        return index ? (registers[a] | wd1793_FLAG_INDEX) : (registers[a] & ~wd1793_FLAG_INDEX);
    }
    return registers[a];
}

// The cylinder the disk is read at: the head of the selected drive, or the
// track register when the heads are not modelled
int WD1793::head_cylinder()
{
    if (!m_physical) return registers[wd1793_REG_TRACK];
    return (selected_drive >= 0) ? (int)m_head[selected_drive] : 0;
}

void WD1793::move_head(int steps)
{
    if (!m_physical || selected_drive < 0) return;
    int h = (int)m_head[selected_drive] + steps;
    if (h < 0) h = 0;
    if (h > m_max_cylinder) h = m_max_cylinder;
    m_head[selected_drive] = (uint32_t)h;
}

unsigned WD1793::get_direct(unsigned address)
{
    //Reading a register here must not move the controller: this is where a
    //LOG in a script, the port window and the memory dump come from, and
    //get_value() clears DRQ/INTRQ and can run the state machine forward
    return registers[address & 0x03];
}

void WD1793::set_value(unsigned int address, unsigned int value, bool force)
{
    if ((address & 0x03) == wd1793_REG_DATA && (address & sync_mask) != 0)
    {
        // The controller must have taken the previous byte before the CPU
        // is allowed to store the next one
        SyncAccess();
        WriteRegister(address, value);
        return;
    }
    if (m_register_delay > 0){
        //There is room for one delayed write only. A 16-bit OUT of the 8088
        //writes two registers within one instruction, before the devices are
        //clocked: the first one is applied now rather than overwritten
        if (register_delay > 0) {
            register_delay = 0;
            WriteRegister(register_to_write, value_to_write);
        }
        register_delay = m_register_delay;
        register_to_write = address;
        value_to_write = value;
    } else
        WriteRegister(address, value);
}

void WD1793::SetTypeIFlags(uint8_t T, uint8_t S)
{
    int seek_res;
    ClearFlag(wd1793_FLAG_BUSY);
    SetHLD((registers[4] & wd1793_PARAM_h) != 0);
    if (hld)
        SetFlag(wd1793_FLAG_HLD);
    else
        ClearFlag(wd1793_FLAG_HLD);

    const bool tr00 = m_physical ? (selected_drive >= 0 && m_head[selected_drive] == 0)
                                 : registers[wd1793_REG_TRACK] == 0;
    if (tr00)
        SetFlag(wd1793_FLAG_TR00);
    else
        ClearFlag(wd1793_FLAG_TR00);

    if (selected_drive >= 0)
    {
        //A type I command only positions the head, so an address the disk does
        //not have is not an error here - only a missing disk is
        seek_res = drives[selected_drive]->SeekSector(m_physical ? head_cylinder() : T, S);
        if (seek_res == FDD_SEEK_NO_DISK)
            SetFlag(wd1793_FLAG_NOT_READY);
        else
            ClearFlag(wd1793_FLAG_NOT_READY);

        if (drives[selected_drive]->is_protected())
            SetFlag(wd1793_FLAG_PROTECTED);
        else
            ClearFlag(wd1793_FLAG_PROTECTED);
        //Verify: the ID under the head must carry the track register
        ClearFlag(wd1793_FLAG_ERR_SEEK);
        if (m_physical && (registers[4] & wd1793_PARAM_V) != 0
            && (seek_res < 0 || head_cylinder() != registers[wd1793_REG_TRACK]))
            SetFlag(wd1793_FLAG_ERR_SEEK);
    } else {
        SetFlag(wd1793_FLAG_NOT_READY);
        SetFlag(wd1793_FLAG_PROTECTED);
    };
}

void WD1793::SetTypeIIFlags()
{
    ClearFlag(wd1793_FLAG_BUSY);
    ClearFlag(wd1793_FLAG_LOST_DATA);
    ClearFlag(wd1793_FLAG_BAD_CRC);
    //Record Not Found of an earlier command (a Read Address or a verify)
    //does not outlive the one that found its sector
    ClearFlag(wd1793_FLAG_ERR_SEEK);
}

// One step of the command state machine, called when the current delay has expired
void WD1793::ExecuteCommand()
{
    switch (command) {
    case wd1793_COMMAND_RESTORE:
        //The head steps out until TR00; with no drive there is no TR00, and
        //after 255 steps the chip gives up with the track register as it was
        move_head(-255);
        if (!m_physical || selected_drive >= 0) registers[wd1793_REG_TRACK] = 0;
        SetTypeIFlags(0, 1);
        if (m_physical && selected_drive < 0) SetFlag(wd1793_FLAG_ERR_SEEK);
        SetINTRQ();
        command = 0;
        break;

    case wd1793_COMMAND_SEEK:
        move_head((int)registers[wd1793_REG_DATA] - (int)registers[wd1793_REG_TRACK]);
        registers[wd1793_REG_TRACK] = registers[wd1793_REG_DATA];
        SetTypeIFlags(registers[wd1793_REG_TRACK], 1);
        SetINTRQ();
        command = 0;
        break;

    case wd1793_COMMAND_STEP:
        move_head(step_dir);
        if ((registers[4] & wd1793_PARAM_T) > 0)
            registers[wd1793_REG_TRACK] += step_dir;
        SetTypeIFlags(registers[wd1793_REG_TRACK], 1);
        SetINTRQ();
        command = 0;
        break;

    case wd1793_COMMAND_READ_SECTOR:
        sector_size = (selected_drive >= 0) ? drives[selected_drive]->SeekSector(head_cylinder(), registers[wd1793_REG_SECTOR]) : -1;
        if (sector_size > 0 && head_cylinder() != registers[wd1793_REG_TRACK]) sector_size = FDD_SEEK_NO_SECTOR;
        if (sector_size > 0)
        {
            delay = wd1793_DELAY_NEXT_BYTE;
            command = wd1793_COMMAND_READ_BYTE;
            bytes = 0;
        } else {
            // No drive or no disk: the command terminates at once. An address
            // outside the geometry finds no mark on a real drive - RNF
            SetTypeIIFlags();
            if (sector_size == FDD_SEEK_NO_SECTOR)
                SetFlag(wd1793_FLAG_ERR_SEEK);
            else
                SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            command = 0;
        }
        break;

    case wd1793_COMMAND_WRITE_SECTOR:
        sector_size = (selected_drive >= 0) ? drives[selected_drive]->SeekSector(head_cylinder(), registers[wd1793_REG_SECTOR]) : -1;
        if (sector_size > 0 && head_cylinder() != registers[wd1793_REG_TRACK]) sector_size = FDD_SEEK_NO_SECTOR;
        if ((sector_size > 0) && !drives[selected_drive]->is_protected())
        {
            delay = wd1793_DELAY_NEXT_BYTE;
            command = wd1793_COMMAND_WRITE_BYTE;
            bytes = 0;
            SetDRQ();
        } else {
            SetTypeIIFlags();
            if (sector_size == FDD_SEEK_NO_SECTOR)
                SetFlag(wd1793_FLAG_ERR_SEEK);
            else if (sector_size < 0)
                SetFlag(wd1793_FLAG_NOT_READY);
            else
                SetFlag(wd1793_FLAG_PROTECTED);
            SetINTRQ();
            command = 0;
        }
        break;

    case wd1793_COMMAND_READ_ADDRESS: {
        //The six bytes of the ID field that comes under the head: cylinder,
        //side, sector, size code and the CRC. The chip puts the cylinder into
        //the sector register
        const int cyl = head_cylinder();
        const int size = (selected_drive >= 0) ? drives[selected_drive]->SeekSector(cyl, 1) : FDD_SEEK_NO_DISK;
        if (size > 0) {
            const int ss = drives[selected_drive]->get_sector_size();
            m_id[0] = (uint8_t)cyl;
            m_id[1] = 0;
            m_id[2] = 1;
            m_id[3] = (uint8_t)((ss >= 1024) ? 3 : (ss >= 512) ? 2 : (ss >= 256) ? 1 : 0);
            m_id[4] = m_id[5] = 0;
            m_reading_id = true;
            sector_size = 6;
            bytes = 0;
            delay = wd1793_DELAY_NEXT_BYTE;
            command = wd1793_COMMAND_READ_BYTE;
        } else {
            SetTypeIIFlags();
            SetFlag((size == FDD_SEEK_NO_DISK) ? wd1793_FLAG_NOT_READY : wd1793_FLAG_ERR_SEEK);
            SetINTRQ();
            command = 0;
        }
        break;
    }

    case wd1793_COMMAND_WRITE_TRACK: {
        //From the index pulse on, a byte at each DRQ until the next one
        const int size = (selected_drive >= 0) ? drives[selected_drive]->SeekSector(head_cylinder(), 1) : FDD_SEEK_NO_DISK;
        if (size == FDD_SEEK_NO_DISK || drives[selected_drive]->is_protected()) {
            SetTypeIIFlags();
            SetFlag((size == FDD_SEEK_NO_DISK) ? wd1793_FLAG_NOT_READY : wd1793_FLAG_PROTECTED);
            SetINTRQ();
            command = 0;
            break;
        }
        ClearFlag(wd1793_FLAG_PROTECTED);
        m_wt_state = 0;
        m_wt_write = m_wt_have_id = false;
        bytes = 0;
        delay = wd1793_DELAY_NEXT_BYTE;
        command = wd1793_COMMAND_TRACK_BYTE;
        SetDRQ();
        break;
    }

    case wd1793_COMMAND_TRACK_BYTE:
        if (!GetDRQ()) {
            track_byte(registers[wd1793_REG_DATA]);
            if (++bytes < (int)m_track_length) {
                SetDRQ();
            } else {
                m_tracks_written++;
                SetTypeIIFlags();
                ClearFlag(wd1793_FLAG_ERR_WRITE);
                SetINTRQ();
                command = 0;
            }
        }
        break;

    case wd1793_COMMAND_READ_BYTE:
        if (!GetDRQ()) {
            if (bytes < sector_size)
            {
                //Reading bytes
                registers[wd1793_REG_DATA] = m_reading_id ? m_id[bytes] : drives[selected_drive]->ReadNextByte();
                bytes++;
                SetDRQ();
            } else if (m_reading_id) {
                m_reading_id = false;
                registers[wd1793_REG_SECTOR] = m_id[0];
                SetTypeIIFlags();
                SetINTRQ();
                command = 0;
            } else {
                //Reached sector's end
                sectors_read++;
                if ((registers[4] & wd1793_PARAM_m) != 0) {
                    //Multiple sectors: on to the next one, still busy. The
                    //one past the track is not found, and that ends it
                    registers[wd1793_REG_SECTOR]++;
                    delay = wd1793_DELAY_SECTOR;
                    command = wd1793_COMMAND_READ_SECTOR;
                    break;
                }
                SetTypeIIFlags();
                ClearFlag(wd1793_FLAG_PROTECTED);
                ClearFlag(wd1793_FLAG_DATA_TYPE);
                SetINTRQ();
                command = 0;
            };
        }
        break;

    case wd1793_COMMAND_WRITE_BYTE:
        if (!GetDRQ())
        {
            //Writing bytes
            drives[selected_drive]->WriteNextByte(registers[wd1793_REG_DATA]);
            bytes++;
            if (bytes < sector_size)
            {
                SetDRQ();
            } else {
                //Reached sector's end
                sectors_written++;
                if ((registers[4] & wd1793_PARAM_m) != 0) {
                    registers[wd1793_REG_SECTOR]++;
                    delay = wd1793_DELAY_SECTOR;
                    command = wd1793_COMMAND_WRITE_SECTOR;
                    break;
                }
                SetTypeIIFlags();
                ClearFlag(wd1793_FLAG_ERR_WRITE);
                SetINTRQ();
                command = 0;
            }
        }
        break;
    }
}

// A byte of Write Track. $F5-$F7 are the chip's own (the MFM sync $A1, $C2
// and the two CRC bytes), so they never start a field; the first byte of
// value $FE in the gaps opens an ID field, $FB or $F8 the data of the last ID.
// A sector the image has, at the cylinder of the head and of its size,
// receives the data; any other is counted as unsupported
void WD1793::track_byte(uint8_t value)
{
    switch (m_wt_state) {
    case 0:
        if (value == 0xFE) {
            m_wt_state = 1;
            m_wt_left = 4;
        } else if ((value == 0xFB || value == 0xF8) && m_wt_have_id) {
            m_wt_have_id = false;
            const unsigned size = 128u << (m_wt_id[3] & 3);
            const int image = drives[selected_drive]->SeekSector(head_cylinder(), m_wt_id[2]);
            m_wt_write = image == (int)size;
            if (!m_wt_write) note_unsupported("wd1793: Write Track, a sector the image has not");
            m_wt_state = 2;
            m_wt_left = size;
        }
        break;
    case 1:
        m_wt_id[4 - m_wt_left] = value;
        if (--m_wt_left == 0) {
            m_wt_state = 0;
            m_wt_have_id = true;
        }
        break;
    default:
        if (m_wt_write) drives[selected_drive]->WriteNextByte(value);
        if (--m_wt_left == 0) {
            if (m_wt_write) sectors_written++;
            m_wt_write = false;
            m_wt_state = 0;
        }
        break;
    }
}

void WD1793::clock(unsigned int counter)
{
    if (m_index_period > 0) {
        m_index_clock += counter;
        while (m_index_clock >= m_index_period) m_index_clock -= m_index_period;
    }

    //Delay before writing to a register
    if (register_delay > 0)
    {
        register_delay -= counter;
        if (register_delay <= 0)
            WriteRegister(register_to_write, value_to_write);
    }

    //Other delays
    if (delay > 0)
        delay -= counter;
    else
        ExecuteCommand();

    //Head unload some time after the last command
    if (hld && command == 0 && hld_timer > 0)
    {
        hld_timer -= counter;
        if (hld_timer <= 0) SetHLD(false);
    }
}

void WD1793::save_state(StateWriter &w)
{
    FDC::save_state(w);
    w.array("registers", registers, 5);
    w.n("selected_drive", static_cast<uint32_t>(selected_drive));
    //A command in flight: the controller counts out a step, a sector or a
    //head load between two accesses, and a snapshot lands in the middle of it
    w.u("command", command, 8);
    w.n("delay", static_cast<uint32_t>(delay));
    w.n("register_delay", static_cast<uint32_t>(register_delay));
    w.u("register_to_write", register_to_write, 8);
    w.u("value_to_write", value_to_write, 8);
    w.n("bytes", static_cast<uint32_t>(bytes));
    w.n("step_dir", static_cast<uint32_t>(step_dir));
    w.b("hld", hld);
    w.n("hld_timer", static_cast<uint32_t>(hld_timer));
    w.n("sectors_read", sectors_read);
    w.n("sectors_written", sectors_written);
    w.b("intrq_forced", intrq_forced);
    if (m_physical) w.array("head", m_head, 4);
    w.n("index_clock", m_index_clock);
    w.b("type1", m_type1);
    w.b("reading_id", m_reading_id);
    w.array("id", m_id, 6);
    w.n("wt_state", m_wt_state);
    w.n("wt_left", m_wt_left);
    w.b("wt_write", m_wt_write);
    w.b("wt_have_id", m_wt_have_id);
    w.array("wt_id", m_wt_id, 4);
    w.n("tracks_written", m_tracks_written);
}

emulator::Result WD1793::load_state(const StateReader &r)
{
    emulator::Result res = FDC::load_state(r);
    if (!res) return res;
    r.array("registers", registers, 5);
    r.u("selected_drive", selected_drive);
    r.u("command", command);
    r.u("delay", delay);
    r.u("register_delay", register_delay);
    r.u("register_to_write", register_to_write);
    r.u("value_to_write", value_to_write);
    r.u("bytes", bytes);
    r.u("step_dir", step_dir);
    r.b("hld", hld);
    r.u("hld_timer", hld_timer);
    r.u("sectors_read", sectors_read);
    r.u("sectors_written", sectors_written);
    r.b("intrq_forced", intrq_forced);
    if (m_physical) r.array("head", m_head, 4);
    r.u("index_clock", m_index_clock);
    r.b("type1", m_type1);
    r.b("reading_id", m_reading_id);
    r.array("id", m_id, 6);
    r.u("wt_state", m_wt_state);
    r.u("wt_left", m_wt_left);
    r.b("wt_write", m_wt_write);
    r.b("wt_have_id", m_wt_have_id);

    r.array("wt_id", m_wt_id, 4);
    r.u("tracks_written", m_tracks_written);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> WD1793::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"status",  "Status register",              false});
    r.push_back({"track",   "Track register",               false});
    r.push_back({"sector",  "Sector register",              false});
    r.push_back({"data",    "Data register",                false});
    r.push_back({"busy",    "1 while a command is running", false});
    r.push_back({"drive",   "Index of the selected drive",  false});
    r.push_back({"hld",     "State of the HLD output",      false});
    r.push_back({"command", "Last command byte",            false});
    r.push_back({"reads",   "Sectors read since start",     false});
    r.push_back({"writes",  "Sectors written since start",  false});
    r.push_back({"formats", "Tracks written (Write Track) since start", false});
    return r;
}

bool WD1793::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "status")  { out.values.push_back(registers[wd1793_REG_STATUS]);   return true; }
    if (field == "track")   { out.values.push_back(registers[wd1793_REG_TRACK]);    return true; }
    if (field == "sector")  { out.values.push_back(registers[wd1793_REG_SECTOR]);   return true; }
    if (field == "data")    { out.values.push_back(registers[wd1793_REG_DATA]);     return true; }
    if (field == "busy")    { out.values.push_back(get_busy()?1:0);                 return true; }
    if (field == "drive")   { out.values.push_back(get_selected_drive());           return true; }
    if (field == "hld")     { out.values.push_back(hld?1:0);                        return true; }
    if (field == "command") { out.values.push_back(registers[4]);                   return true; }
    out.width = 32;
    if (field == "reads")   { out.values.push_back(sectors_read);                   return true; }
    if (field == "writes")  { out.values.push_back(sectors_written);                return true; }
    if (field == "formats") { out.values.push_back(m_tracks_written);               return true; }

    out.width = 0;

    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new WD1793(im, cd);
}
