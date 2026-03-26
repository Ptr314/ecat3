// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: FDD device, source

#include "fdd.h"
#include "emulator/utils.h"
#include "libs/mfm_tools.h"
#include "dsk_tools/dsk_tools.h"

#define FDD_MODE_LOGICAL    0
#define FDD_MODE_AGAT_140   1
#define FDD_MODE_AGAT_840   2

#define CALLBACK_SELECT     1
#define CALLBACK_MOTOR_ON   2

FDD::FDD(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , loaded(false)
    , stream_format(FDD_STREAM_PLAIN)
    , buffer(nullptr)
    , side(0)
    , track(0)
    , fdd_mode(FDD_MODE_LOGICAL)
    , led_timer(this)
    , i_select(this, im, 2, "select", MODE_R, CALLBACK_SELECT)
    , i_side(this, im, 1, "side", MODE_R)
    , i_density(this, im, 1, "density", MODE_R)
    , i_motor_on(this, im, 1, "motor_on", MODE_R, CALLBACK_MOTOR_ON)

{
    device_class = "fdd";

    //memset(&buffer, 0, sizeof(buffer));
    led_timer.setSingleShot(true);
}

FDD::~FDD()
{
    if (buffer != nullptr) delete [] buffer;
}

dsk_tools::Result FDD::load_config(SystemData *sd)
{
    dsk_tools::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    try {
        sides = parse_numeric_value(cd->get_parameter("sides").value);
        tracks = parse_numeric_value(cd->get_parameter("tracks").value);
        sectors = parse_numeric_value(cd->get_parameter("sectors").value);
        sector_size = parse_numeric_value(cd->get_parameter("sector_size").value);
        selector = parse_numeric_value(cd->get_parameter("selector_value").value);
        files = cd->get_parameter("files").value;
        disk_size = sides*tracks*sectors*sector_size;
        write_protect = false;
    } catch (std::exception &e) {
        return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect fdd parameters for")) + "} " + name);
    }

    files_save = cd->get_parameter("files_save", false).value;
    if (files_save.empty()) files_save = files;

    std::string s = read_confg_value(cd, "mode", false, std::string("logical"));
    if (s == "logical")
        fdd_mode = FDD_MODE_LOGICAL;
    else if (s == "mfm_agat_140")
        fdd_mode = FDD_MODE_AGAT_140;
    else if (s == "mfm_agat_840")
        fdd_mode = FDD_MODE_AGAT_840;
    else
        return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unknown fdd mode")) + "} " + s);

    try {
        file_name = find_file_location(sd, cd->get_parameter("image").value);
        if (!file_name.empty()) {
            dsk_tools::Result img_res = load_image(file_name);
            if (!img_res) return img_res;
        } else
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Disk image file not found")) + "} " + file_name);
    } catch (std::exception &e) {
    }

    return dsk_tools::Result::ok();
}

dsk_tools::Result FDD::load_image(const std::string &file_name)
{
    std::string ext = dsk_tools::get_file_ext(file_name);  // returns ".ext" lowercase
    std::string base_name = dsk_tools::get_filename(file_name);

    if (ext == ".mfm" || ext == ".hfe") {
        if (fdd_mode == FDD_MODE_LOGICAL)
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "FDD device is working in a logical mode, no physical formats are supported")) + "}");
        HXC_MFM_HEADER hxc_header;
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (file.is_open()){
            file.read(reinterpret_cast<char*>(&hxc_header), sizeof(HXC_MFM_HEADER));
            if (memcmp(hxc_header.headername, "HXCMFM", 6) == 0) {
                sides = hxc_header.number_of_side;
                tracks = hxc_header.number_of_track;

                file.seekg(hxc_header.mfmtracklistoffset, std::ios::beg);
                file.read(reinterpret_cast<char*>(&track_indexes), sizeof(HXC_MFM_TRACK_INFO)*tracks);

                disk_size = track_indexes[0].mfmtracksize * tracks;
                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];

                int data_begin = track_indexes[0].mfmtrackoffset;
                for (int i=0; i < tracks; i++) track_indexes[i].mfmtrackoffset -= data_begin;

                file.seekg(data_begin, std::ios::beg);
                file.read(reinterpret_cast<char*>(buffer), disk_size);

                track_mode = FDD_MODE_WHOLE_TRACK;
                position = 0;
                loaded = true;
                this->file_name = base_name;
            } else {
                file.close();
                return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unrecognized MFM format")) + "}");
            }
            file.close();
        } else {
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
        }
    } else
    if (ext == ".nib" || ext == ".nic") {
        if (fdd_mode == FDD_MODE_LOGICAL)
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "FDD device is working in a logical mode, no physical formats are supported")) + "}");
        int expected_size = (ext == ".nib")?232960:286720;
        long long actual_size = dsk_tools::utf8_file_size(file_name);

        if (actual_size == expected_size) {
            dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
            if (file.is_open()) {
                sides = 1;
                tracks = 35;
                disk_size = expected_size;
                physical_track_len = disk_size / tracks;

                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];

                for (int i=0; i < tracks; i++) {
                    track_indexes[i].track_number = i;
                    track_indexes[i].side_number = 0;
                    track_indexes[i].mfmtracksize = physical_track_len;
                    track_indexes[i].mfmtrackoffset = i * physical_track_len;
                }

                file.read(reinterpret_cast<char*>(buffer), disk_size);

                track_mode = FDD_MODE_WHOLE_TRACK;
                position = 0;
                loaded = true;
                this->file_name = base_name;
            } else {
                return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
            }
            file.close();
        } else {
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "File is in unknown format")) + "} " + file_name);
        }
    } else
    if (ext == ".aim") {
        if (fdd_mode != FDD_MODE_AGAT_840)
            return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "AIM files supported on Agat 840k drives only!")) + "}");
        if (buffer != nullptr) delete [] buffer;
        buffer = load_aim_image(file_name, sides, tracks, disk_size, track_indexes, aim_codes);
        track_mode = FDD_MODE_WHOLE_TRACK;
        position = 0;
        loaded = true;
        this->file_name = base_name;
    } else {
        if (fdd_mode == FDD_MODE_LOGICAL) {
            long long file_size = dsk_tools::utf8_file_size(file_name);

            if (file_size == static_cast<long long>(disk_size))
            {
                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];

                dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
                if (file.is_open()) {
                    file.read(reinterpret_cast<char*>(buffer), file_size);
                    file.close();

                    track_mode = FDD_MODE_SECTORS;
                    loaded = true;
                    this->file_name = base_name;
                } else {
                    return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
                }
            } else {
                this->file_name = "";
                return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect disk image size for")) + "} " + file_name);
            }
        } else {
            // Convert to MFM
            if (buffer != nullptr) delete [] buffer;
            switch (fdd_mode) {
                case FDD_MODE_AGAT_140:
                    buffer = generate_mfm_agat_140(file_name, sides, tracks, disk_size, track_indexes);
                    break;
                case FDD_MODE_AGAT_840:
                    buffer = generate_mfm_agat_840(file_name, sides, tracks, disk_size, track_indexes, aim_codes);
                    break;
                default:
                    return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Expected conversion from DSK to MFM is not supported yet.")) + "}");
            }
            track_mode = FDD_MODE_WHOLE_TRACK;
            position = 0;
            loaded = true;
            this->file_name = base_name;
        }
    }
    return dsk_tools::Result::ok();
}

int FDD::SeekSector(int track, int sector)
{
    int result = -1;
    if (buffer != nullptr)
    {
        if (sides > 1)
            this->side = ~(i_side.value) & 1;
        this->track = track;
        this->sector = sector;
        // qDebug() << "SEEK " << this->side << this->track << this->sector;
#ifdef LOG_FDD
        static int prev_track = -1;
        if (prev_track != track) {
            logs(QString("ROT %1 SEEK side:%2 track:%3 sector:%4").arg(log_rotations).arg(side).arg(track).arg(sector).toStdString());
            log_rotations = 0;
            prev_track = track;
        }
#endif
        position = 0;
        if (track_mode == FDD_MODE_SECTORS) {
            result = sector_size;
        } else {
            result = 256;
        }
    }
    return result;
}

unsigned int FDD::translate_address()
{
    return ((track*sides + side)*sectors + sector-1)*sector_size + position;
}

void FDD::NextPosition()
{
    if (++position >= track_indexes[track*sides + side].mfmtracksize) position = 0;
}

uint8_t FDD::ReadNextByte()
{
    if (loaded) {
        if (track_mode == FDD_MODE_SECTORS) {
            if (position >= sector_size)
                im->dm->error(this, FDD::tr("Reading outside of a sector").toStdString());

            if (sector==0)
            {
                //GAP
                return 0xFF;
            } else {
                //DAta
                uint8_t result = buffer[translate_address()];
                position++;
                return result;
            }
        } else {
            uint8_t result = buffer[track_indexes[track*sides + side].mfmtrackoffset + position++];
            if (position >= track_indexes[track*sides + side].mfmtracksize) {
    #ifdef LOG_FDD
                log_rotations++;
    #endif
                position = 0;
            }
    #ifdef LOG_FDD
            //logs(QString("R %1 %2").arg(position).arg(result, 2, 16, QChar('0')));
    #endif
            return result;
        }
    } else
        return 0xFF;
}

void FDD::WriteNextByte(uint8_t value)
{
    if (track_mode == FDD_MODE_SECTORS) {
        if (position >= sector_size)
            im->dm->error(this, FDD::tr("Writing outside of a sector").toStdString());

        if (sector != 0)
        {
            buffer[translate_address()] = value;
            position++;
        }
    } else {
        buffer[track_indexes[track*sides + side].mfmtrackoffset + position++] = value;
        if (position >= track_indexes[track*sides + side].mfmtracksize) position = 0;
    }
}

void FDD::WriteByte(uint8_t value)
{
    if (track_mode == FDD_MODE_SECTORS) {
        if (position >= sector_size)
            im->dm->error(this, FDD::tr("Writing outside of a sector").toStdString());

        if (sector != 0)
        {
            buffer[translate_address()] = value;
        }
    } else {
        buffer[track_indexes[track*sides + side].mfmtrackoffset + position] = value;
    }
}


bool FDD::is_selected()
{
    return (i_select.value & 0x03) == selector;
}

bool FDD::is_protected()
{
    return write_protect;
}

bool FDD::is_index()
{
    //TODO: tune conditions
    if (track_mode == FDD_MODE_SECTORS) {
        return sector==0 && position < 100;
    } else {
        return position < 100;
    }
}

bool FDD::is_track_00()
{
    return track == 0;
}

void FDD::unload(){
    if (buffer != nullptr) delete [] buffer;
    buffer = nullptr;
    loaded = false;
    file_name = "";
}

int FDD::get_sector_size()
{
    return sector_size;
}

int FDD::get_loaded()
{
    return loaded;
}

int FDD::get_position()
{
    return position;
}

void FDD::set_position(int value)
{
    position = value;
}

void FDD::change_protection()
{
    write_protect = !write_protect;
}

dsk_tools::Result FDD::save_image(const std::string &file_name)
{
    if (loaded)
    {
        std::string ext = dsk_tools::get_file_ext(file_name); // returns ".ext" lowercase

        bool is_raw = (ext == ".dsk" || ext == ".gmd" || ext == ".cpm");

        if (is_raw) {
            if (fdd_mode == FDD_MODE_AGAT_840) {
                dsk_tools::BYTES encoded_data(buffer, buffer+disk_size);
                dsk_tools::BYTES raw_data;
                const dsk_tools::Result decode_res = dsk_tools::decode_agat_840_image(raw_data, encoded_data);
                if (decode_res) {
                    dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
                    if (file.is_open()){
                        file.write(reinterpret_cast<char*>(raw_data.data()), raw_data.size());
                        file.close();
                    }
                } else {
                    return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error exporting disk.")) + "} " + dsk_tools::decode_error(decode_res) + " : " + decode_res.message);
                }
            } else
            if (fdd_mode == FDD_MODE_AGAT_140) {
                dsk_tools::BYTES encoded_data(buffer, buffer+disk_size);
                dsk_tools::BYTES raw_data;
                const dsk_tools::Result decode_res = dsk_tools::decode_agat_140_image(raw_data, encoded_data, track_indexes[0].mfmtracksize);
                if (decode_res) {
                    dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
                    if (file.is_open()){
                        file.write(reinterpret_cast<char*>(raw_data.data()), raw_data.size());
                        file.close();
                    }
                } else {
                    return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error exporting disk.")) + "} " + dsk_tools::decode_error(decode_res) + " : " + decode_res.message);
                }
            } else
            if (fdd_mode == FDD_MODE_LOGICAL) {
                dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
                if (file.is_open()){
                    file.write(reinterpret_cast<char*>(buffer), disk_size);
                    file.close();
                }
            } else {
                return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "FDD is working in a physical mode now, generating of DSK images is not supported yet.")) + "}");
            }
        } else if (ext == ".mfm") {
            if (fdd_mode == FDD_MODE_AGAT_140) {
                save_mfm_file(file_name, sides, tracks, track_indexes[0].mfmtracksize, track_indexes, buffer);
            } else {
                return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Saving images for this type of drive is not supported yet.")) + "}");
            }
        }
    }
    return dsk_tools::Result::ok();
}

void FDD::ConvertStreamFormat()
{
    //TODO: FDD Implement
}

void FDD::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    // if (callback_id == CALLBACK_MOTOR_ON) {
        motor_on = ((i_motor_on.value & 1) == 0) && is_selected();
        if (motor_on) m_motor_was_on = true;
        // if (is_selected())
        // qDebug() << "MOT" << selector << motor_on << (i_motor_on.value & 1);
    // }
}

bool FDD::is_led_on()
{
    // qDebug() << motor_on << selector;
    if (motor_on || m_motor_was_on) {
        m_motor_was_on = false;
        led_timer.start(5000);
    }
    return led_timer.isActive();
}

// Returns the AIM control code at the current position, or 0 if none exists
int FDD::aim_code()
{
    auto code = aim_codes[track].find(get_position());
    if (code != aim_codes[track].end())
        return code->second;
    else
        return 0;
}

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new FDD(im, cd);
}
