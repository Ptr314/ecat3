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

    std::string s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{WD1793|" + std::string(QT_TRANSLATE_NOOP("WD1793", "Incorrect fdd list for")) + "} " + name);
    }

    std::vector<std::string> parts = split_string(s, '|', true);
    drives_count = parts.size();
    for (unsigned int i = 0; i < drives_count; i++)
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

    // Address bits selecting a copy of the data register whose access waits
    // for DRQ/INTRQ (the Irisha KNGMD holds READY low on port 37 this way)
    sync_mask = read_confg_value(cd, "sync", false, (unsigned int)0);

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
        ClearINTRQ();
        FindSelectedDrive();
        unsigned int c = (value & 0xF0) >> 4;
        //Execute commands
        switch (c) {
        case 0x00: //Restore
            delay = wd1793_DELAY_RESTORE;
            command = wd1793_COMMAND_RESTORE;
            SetFlag(wd1793_FLAG_BUSY);
            break;

        case 0x01: //Seek
            delay = wd1793_DELAY_STEP * abs((int)registers[wd1793_REG_TRACK] - (int)registers[wd1793_REG_DATA]);
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

        case 0x08:
        case 0x09: //Read sector
            if ((value & wd1793_PARAM_m) > 0)
            {
                im->dm->error(this, "Reading of more than one sector at once is not supported!");
            } else {
                delay = wd1793_DELAY_SECTOR;
                command = wd1793_COMMAND_READ_SECTOR;
                SetFlag(wd1793_FLAG_BUSY);
                SetHLD(true);
            }
            break;

        case 0x0A:
        case 0x0B: //Write sector
            if ((value & wd1793_PARAM_m) > 0)
            {
                im->dm->error(this, "Writing of more than one sector at once is not supported!");
            } else {
                delay = wd1793_DELAY_SECTOR;
                command = wd1793_COMMAND_WRITE_SECTOR;
                SetFlag(wd1793_FLAG_BUSY);
                SetHLD(true);
            }
            break;

        case 0x0C: //Read address
            SetHLD(true);
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0E: //Read track
            SetHLD(true);
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0F: //Write track
            SetHLD(true);
            SetFlag(wd1793_FLAG_NOT_READY);
            SetINTRQ();
            break;

        case 0x0D: //Force interrupt
            ClearFlag(wd1793_FLAG_BUSY);
            command = 0;
            delay = 0;
            ClearDRQ();
            if ((value & 0x0F) !=0)
                im->dm->error(this, "Force Interrupt command with parameters is not supported!");
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
    if (a==wd1793_REG_STATUS) ClearINTRQ();
    return registers[a];
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
    if (wd1793_DELAY_REGISTER > 0){
        register_delay = wd1793_DELAY_REGISTER;
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

    if (registers[wd1793_REG_TRACK]==0)
        SetFlag(wd1793_FLAG_TR00);
    else
        ClearFlag(wd1793_FLAG_TR00);

    if (selected_drive >= 0)
    {
        //A type I command only positions the head, so an address the disk does
        //not have is not an error here - only a missing disk is
        seek_res = drives[selected_drive]->SeekSector(T, S);
        if (seek_res == FDD_SEEK_NO_DISK)
            SetFlag(wd1793_FLAG_NOT_READY);
        else
            ClearFlag(wd1793_FLAG_NOT_READY);

        if (drives[selected_drive]->is_protected())
            SetFlag(wd1793_FLAG_PROTECTED);
        else
            ClearFlag(wd1793_FLAG_PROTECTED);
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
}

// One step of the command state machine, called when the current delay has expired
void WD1793::ExecuteCommand()
{
    switch (command) {
    case wd1793_COMMAND_RESTORE:
        registers[wd1793_REG_TRACK] = 0;
        SetTypeIFlags(0, 1);
        SetINTRQ();
        command = 0;
        break;

    case wd1793_COMMAND_SEEK:
        registers[wd1793_REG_TRACK] = registers[wd1793_REG_DATA];
        SetTypeIFlags(registers[wd1793_REG_TRACK], 1);
        SetINTRQ();
        command = 0;
        break;

    case wd1793_COMMAND_STEP:
        if ((registers[4] & wd1793_PARAM_T) > 0)
            registers[wd1793_REG_TRACK] += step_dir;
        SetTypeIFlags(registers[wd1793_REG_TRACK], 1);
        SetINTRQ();
        command = 0;
        break;

    case wd1793_COMMAND_READ_SECTOR:
        sector_size = (selected_drive >= 0) ? drives[selected_drive]->SeekSector(registers[wd1793_REG_TRACK], registers[wd1793_REG_SECTOR]) : -1;
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
        sector_size = (selected_drive >= 0) ? drives[selected_drive]->SeekSector(registers[wd1793_REG_TRACK], registers[wd1793_REG_SECTOR]) : -1;
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

    case wd1793_COMMAND_READ_BYTE:
        if (!GetDRQ()) {
            if (bytes < sector_size)
            {
                //Reading bytes
                registers[wd1793_REG_DATA] = drives[selected_drive]->ReadNextByte();
                bytes++;
                SetDRQ();
            } else {
                //Reached sector's end
                sectors_read++;
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
                SetTypeIIFlags();
                ClearFlag(wd1793_FLAG_ERR_WRITE);
                SetINTRQ();
                command = 0;
            }
        }
        break;
    }
}

void WD1793::clock(unsigned int counter)
{
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
    out.width = 0;

    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new WD1793(im, cd);
}
