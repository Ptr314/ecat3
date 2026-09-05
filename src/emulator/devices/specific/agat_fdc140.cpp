// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Agat 140K floppy disk controller device

#include <cstring>

#include "agat_fdc140.h"
#include "emulator/utils.h"

// https://github.com/allender/apple2emu/blob/df9eff703dd70b7dbc3b817734daf95133437143/src/disk_image.cpp#L263

#define AGAT_FDC_READ   0;
#define AGAT_FDC_WRITE  1;

Agat_FDC140::Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd):
      FDC(im, cd)
    , prev_phase(-1)
    , current_phase(-1)
    , motor_on(false)
    , write_mode(false)
    , speed_mode(true)
    , data_ready(false)
    , i_select(this, im, 2, "select", MODE_W)
    , i_motor_on(this, im, 1, "motor_on", MODE_W)
{
    selected_drive = -1;

    memset(&current_track, 0, sizeof(current_track));
}

emulator::Result Agat_FDC140::load_config(SystemData *sd)
{
    emulator::Result res = FDC::load_config(sd);
    if (!res) return res;

    std::string mode_string = str_tolower(cd->get_parameter("speed_mode", false).value);

    if (mode_string.empty() || mode_string == "fast")
        speed_mode = true;
    else if (mode_string == "syncro")
        speed_mode = false;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{Agat_FDC140|" + std::string(QT_TRANSLATE_NOOP("Agat_FDC140", "Unknown speed mode")) + "} " + mode_string);

    clock_divider = 32; // Byte timer for the syncro mode

    std::string s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (std::exception &e) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{Agat_FDC140|" + std::string(QT_TRANSLATE_NOOP("Agat_FDC140", "Incorrect fdd list for")) + "} " + name);
    }

    memset(&drives, 0, sizeof(drives));
    std::vector<std::string> parts = split_string(s, '|', true);
    drives_count = parts.size();
    for (unsigned int i = 0; i < drives_count; i++)
        drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

    selected_drive = 0;

    return emulator::Result::ok();
}


bool Agat_FDC140::get_busy()
{
    // TODO: implement
    return motor_on;
}

unsigned int Agat_FDC140::get_selected_drive()
{
    // TODO: how is it selected?
    return selected_drive;
}

void Agat_FDC140::phase_on(int n)
{
    if (prev_phase >= 0) {
        if ( ((prev_phase-1) & 0x03) == n ) {
            //Step down
            if (current_track[selected_drive] > 0) {
                current_track[selected_drive]--;
                drives[selected_drive]->SeekSector(current_track[selected_drive] / 2, 0);
            }
        } else
        if ( ((prev_phase+1) & 0x03) == n ) {
            //Step up
            if (current_track[selected_drive] < 68) {
                current_track[selected_drive]++;
                drives[selected_drive]->SeekSector(current_track[selected_drive] / 2, 0);
            }
        }
    }
    prev_phase = n;
}

void Agat_FDC140::phase_off(int n)
{
    //prev_phase = current_phase;
    //current_phase = n;
}

void Agat_FDC140::select_drive(int n)
{
    // TODO: check selection on a drive
    selected_drive = n;
    i_select.change(n);
    current_phase = -1;
}

unsigned int Agat_FDC140::get_value(unsigned int address)
{
    unsigned int A = address & 0x0f;
    switch (A) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6:
            phase_off(A >> 1);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7:
            phase_on(A >> 1);
            break;
        case 0x8:
            motor_on = false;
            i_motor_on.change(0);
            break;
        case 0x9:
            motor_on = true;
            i_motor_on.change(1);
            break;
        case 0xA:
        case 0xB:
            select_drive(A & 0x01);
            break;
        case 0xC:
            // TODO: timings imitation
            if (write_mode) {
                // Writing
                if (motor_on) {
                    if (speed_mode) {
                        // Speed mode
                        drives[selected_drive]->WriteNextByte(write_register);
                    } else {
                        // Syncro mode
                        drives[selected_drive]->WriteByte(write_register);
                    }
                }
            } else {
                // Reading
                if (motor_on) {
                    if (speed_mode) {
                        // Speed mode
                        if (drives[selected_drive] != nullptr)
                            return drives[selected_drive]->ReadNextByte();
                        else
                            return 0xFF;
                    } else {
                        // Syncro mode
                        if (data_ready) {
                            data_ready = false;
                            return data;
                        } else {
                            return 0;
                        }
                    }
                } else {
                    return std::rand() & 0xFF;
                }
            }
            break;
        case 0xD:
            break;
        case 0xE:
            write_mode = false;
            if (drives[selected_drive] != nullptr)
                return drives[selected_drive]->is_protected()?0x80:0;
            break;
        default: // 0x0F
            write_mode = true;
            break;
    }
    return 0xFF;
}

void Agat_FDC140::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int A = address & 0x0f;
    switch (A) {
        case 0x0:
        case 0x2:
        case 0x4:
        case 0x6:
            phase_off(A >> 1);
            break;
        case 0x1:
        case 0x3:
        case 0x5:
        case 0x7:
            phase_on(A >> 1);
            break;
        case 0x8:
            break;
        case 0x9:
            break;
        case 0xA:
        case 0xB:
            select_drive(A & 0x01);
            break;
        case 0xC:
            if (motor_on) {
                if (speed_mode) {
                    // Speed mode
                    drives[selected_drive]->WriteNextByte(write_register);
                } else {
                    // Syncro mode
                    drives[selected_drive]->WriteByte(write_register);
                }
            }
        case 0xD:
            write_register = value;
            break;
        case 0xE:
            write_mode = false;
            break;
        default: // 0x0F
            write_mode = true;
            write_register = value;
            break;
    }
}

void Agat_FDC140::clock(unsigned int counter)
{
    if (!speed_mode) {
        // Syncro mode
        if (motor_on) {
            if (write_mode) {
                if (drives[selected_drive] != nullptr) drives[selected_drive]->NextPosition(); // Just move the pointer
            } else {
                data_ready = true;
                data = (drives[selected_drive] != nullptr)? drives[selected_drive]->ReadNextByte() : 0xFF;
            }
        }
    }
}

std::vector<DeviceFieldInfo> Agat_FDC140::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = FDC::get_device_fields();
    r.push_back({"track",  "Track every drive stands on",              true});
    r.push_back({"phase",  "Stepper phase the head is being pulled to", false});
    r.push_back({"motor",  "1 while the motor runs",                   false});
    r.push_back({"write",  "1 in the write mode",                      false});
    r.push_back({"data",   "Byte in the data register",                false});
    r.push_back({"ready",  "1 when that byte is valid",                false});
    r.push_back({"drives", "How many drives are attached",             false});
    return r;
}

bool Agat_FDC140::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    //Signed, and -1 means no phase is energised, which as an unsigned value
    //would print as a very large number
    if (field == "phase")
    {
        out.numeric = false;
        out.text = (current_phase < 0) ? "none" : std::to_string(current_phase);
        return true;
    }

    if (field == "motor" || field == "write" ||
        field == "data" || field == "ready" || field == "drives")
    {
        out.numeric = true;
        if (field == "motor")       out.values.push_back(motor_on ? 1 : 0);
        else if (field == "write")  out.values.push_back(write_mode ? 1 : 0);
        else if (field == "data")   out.values.push_back(data);
        else if (field == "ready")  out.values.push_back(data_ready ? 1 : 0);
        else                        out.values.push_back(static_cast<unsigned int>(drives_count));
        return true;
    }

    //A head that is not where the software thinks it is explains most of the
    //read errors this controller produces
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

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC140(im, cd);
}
