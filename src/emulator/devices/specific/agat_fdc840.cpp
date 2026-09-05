// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat 840K floppy disk controller device

#include <cstring>

#include "agat_fdc840.h"
#include "emulator/utils.h"
#include "libs/mfm_tools.h"

Agat_FDC840::Agat_FDC840(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd)
    , dd14(im, cd)
    , dd15(im, cd)
    , motor_on(false)
    , write_mode(false)
    , sector_sync(false)
    , write_sync(false)
    , data_ready(false)
    , i_select(this, im, 2, "select", MODE_W)
    , i_side(this, im, 1, "side", MODE_W)
    , i_motor_on(this, im, 1, "motor_on", MODE_W)
    , side(0)
{
    selected_drive = -1;
    memset(&current_track, 0, sizeof(current_track));
}

emulator::Result Agat_FDC840::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    clock_divider = 64;

    std::string s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{Agat_FDC840|" + std::string(QT_TRANSLATE_NOOP("Agat_FDC840", "Incorrect fdd list for")) + "} " + name);
    }

    memset(&drives, 0, sizeof(drives));
    std::vector<std::string> parts = split_string(s, '|', true);
    drives_count = parts.size();

    LinkData ld;
    ld.s.i = &i_side;
    ld.s.shift = 0;
    ld.s.mask = create_mask(1, 0);

    for (unsigned int i = 0; i < drives_count; i++) {
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

        ld.d.i = im->get_interface_by_name(parts[i], "side");
        if (ld.d.i == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{Agat_FDC840|" + std::string(QT_TRANSLATE_NOOP("Agat_FDC840", "Interface not found")) + "} " + parts[i] + ":side");
        ld.d.shift = 0;
        ld.d.mask = create_mask(1, 0);
        ld.s.i->connect(ld.s, ld.d, false);
    }

    selected_drive = 0;
    i_select.change(selected_drive);
    i_side.change(~side);

    return emulator::Result::ok();
}


bool Agat_FDC840::get_busy()
{
    return motor_on;
}

unsigned int Agat_FDC840::get_selected_drive()
{
    return selected_drive;
}

void Agat_FDC840::reset(bool cold)
{
    FDC::reset(cold);

    dd14.reset(cold);
    dd15.reset(cold);

    i_select.change(selected_drive);
    i_side.change(~side);
}

void Agat_FDC840::update_status()
{
    // Collect status signals and put them to the register for reading

    const bool loaded = (selected_drive < drives_count) ? drives[selected_drive]->get_loaded() : false;

    uint8_t status_fdd =   ((drives_count>1)?0b00:0b11                   << 0)    //FDD2 type
                         + (0b00                                         << 2)    //FDD1 type
                         + (((selected_drive < drives_count)?
                                (drives[selected_drive]->is_index()?0:1)
                                :1
                            )                                            << 4)    // Index hole
                         + (((selected_drive < drives_count)?
                                 (drives[selected_drive]->is_protected()?0:1)
                                                             :1
                             )                                           << 5)    // Write protection
                         //+ (0 << 5)
                         + (((selected_drive < drives_count)?
                                (current_track[selected_drive]==0?0:1)
                                :1
                            )                                            << 6)    // Track 00
                         + ((motor_on?0:1)                               << 7);   // Ready // TODO: check

    if (!loaded) status_fdd &= ~0x80;
    dd14.set_value(1, status_fdd, true);

    uint8_t status_fdc =   ((sector_sync?0:1) << 6)                               // sector sync detected (active - 0)
                         + ((data_ready?1:0) << 7);                               // data is ready to be read or written (active - 1)

    if (!loaded) status_fdc &= ~0x80;
    dd15.set_value(2, status_fdc, true);
}

void Agat_FDC840::update_state()
{
    // Check control signals after setting them
    uint8_t state = dd14.get_value(2);

    step_dir       = (state >> 2) & 0x1;
    int new_selected = (state >> 3) & 0x1;
    int new_side = (state >> 4) & 0x1;
    int new_write_mode     = ((state >> 6) & 0x1) == 1;
    int motor_bit = ((state >> 7) & 0x1);
    motor_on       = motor_bit == 1;

    i_motor_on.change(~motor_bit);


    write_mode = new_write_mode;

    if ((side != new_side) || (selected_drive != new_selected)) {
        selected_drive = new_selected;
        side = new_side;
        i_select.change(selected_drive);
        i_side.change(~side);
        if (selected_drive < drives_count)
            drives[selected_drive]->SeekSector(current_track[selected_drive], 0);
    }

    update_status();
}

void Agat_FDC840::read_next_byte()
{
    if (selected_drive < drives_count) {
        int pos = drives[selected_drive]->get_position();
        int aim_code = drives[selected_drive]->aim_code();
        uint8_t data = drives[selected_drive]->ReadNextByte();
        if (aim_code == 1) {
            // if (data == 0) data = drives[selected_drive]->ReadNextByte();
            sector_sync = true;
            // qDebug() << "-- SYNC " << pos << ":" << hex << data;
        } else {
            // qDebug() << hex << data;
        }

        dd15.set_value(0, data, true);
        data_ready = true;
        update_status();
    }
}

void Agat_FDC840::write_next_byte()
{
    if (selected_drive < drives_count) {
        uint8_t data = dd15.get_value(1);

        drives[selected_drive]->WriteByte(data);
    }
    data_ready = false;
    update_status();
}


unsigned int Agat_FDC840::get_value(unsigned int address)
{
    //TODO: check if reading 8255(3) works
    unsigned int A = address & 0x0f;
    uint8_t value;
    switch (A) {
        case 0x0:
            value = 0x10;
            break;
        case 0x1:
        case 0x2:
            update_status();
            value = dd14.get_value(A & 0x03);
            break;
        case 0x4:
            if (drives[selected_drive]->get_loaded() && motor_on) {
                // if (data_ready) {
                    value = dd15.get_value(A & 0x03);
                // } else {
                //     value = 0;
                // }
            } else {
                value = std::rand() & 0xFF;
            }
            data_ready = false;
            update_status();
            break;
        case 0x6:
        case 0x7:
            value = dd15.get_value(A & 0x03);
            break;
        default:
            value = 0xFF;
            break;
    }
    return value;
}

void Agat_FDC840::set_value(unsigned int address, unsigned int value, bool force)
{
    data_ready = false;
    unsigned int A = address & 0x0f;

    switch (A) {
        case 0x2:
        case 0x3:
            dd14.set_value(A & 0x03, value);
            update_state();
            break;
        case 0x5:
            dd15.set_value(A & 0x03, value);
            if (write_sync && value==0x6A) {
                // A dirty trick. As data desync is fixed, we have to force setting its position
                if (selected_drive < drives_count) {
                    int position = drives[selected_drive]->get_position();
                    if (position % 282 > 20)
                        drives[selected_drive]->set_position((position / 282) * 282 + 22);
                }
            }
            write_next_byte();
            break;
        case 0x7:
            dd15.set_value(A & 0x03, value);
            break;
        case 0x8:
            // SYNC WRITE
            // TODO: writing
            write_sync = true;
            break;
        case 0x9:
            // STEP
            if (selected_drive < drives_count) {
                if (step_dir == 1) {
                    if (current_track[selected_drive] < AGAT_840_TRACK_COUNT-1) current_track[selected_drive]++;
                } else {
                    if (current_track[selected_drive] > 0) current_track[selected_drive]--;
                }
                drives[selected_drive]->SeekSector(current_track[selected_drive], 0);
            }
            break;
        case 0xA:
            // SYNC RESET
            sector_sync = false;
            update_status();
            break;
        default:
            break;
    }
}

void Agat_FDC840::clock(unsigned int counter)
{
    if (motor_on) {
        if (write_mode) {
            if (selected_drive < drives_count)
                drives[selected_drive]->NextPosition();
            data_ready = true;
            update_status();
        } else
            read_next_byte();
    }
}

std::vector<DeviceFieldInfo> Agat_FDC840::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = FDC::get_device_fields();
    r.push_back({"track",   "Track every drive stands on",             true});
    r.push_back({"side",    "Side of the disk being addressed",        false});
    r.push_back({"step",    "Direction the last step went",            false});
    r.push_back({"motor",   "1 while the motor runs",                  false});
    r.push_back({"write",   "1 in the write mode",                     false});
    r.push_back({"ready",   "1 when a data byte is valid",             false});
    r.push_back({"sync",    "State of the sector and write sync flags", false});
    r.push_back({"drives",  "How many drives are attached",            false});
    return r;
}

bool Agat_FDC840::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //Signed: a step outwards is negative
    if (field == "step")
    {
        out.numeric = false;
        out.text = std::to_string(step_dir);
        return true;
    }

    if (field == "side" || field == "motor" ||
        field == "write" || field == "ready" || field == "drives")
    {
        out.numeric = true;
        if (field == "side")        out.values.push_back(static_cast<unsigned int>(side));
        else if (field == "motor")  out.values.push_back(motor_on ? 1 : 0);
        else if (field == "write")  out.values.push_back(write_mode ? 1 : 0);
        else if (field == "ready")  out.values.push_back(data_ready ? 1 : 0);
        else                        out.values.push_back(static_cast<unsigned int>(drives_count));
        return true;
    }

    if (field == "sync")
    {
        out.numeric = false;
        out.text = std::string("sector=") + (sector_sync ? "1" : "0")
                 + " write=" + (write_sync ? "1" : "0");
        return true;
    }

    if (field == "track")
    {
        if (to > 1) to = 1;
        if (from > to) from = to;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        for (unsigned int i = from; i <= to; i++)
            out.values.push_back(static_cast<unsigned int>(current_track[i]));
        return true;
    }

    return FDC::get_field(field, from, to, out);
}

ComputerDevice * create_agat_fdc840(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC840(im, cd);
}
