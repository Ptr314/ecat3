// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: FDD device, source

#include <cstring>

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
    , sides_layout(false)
    , default_sides_layout(false)
    , i_select(this, im, 2, "select", MODE_R, CALLBACK_SELECT)
    , i_side(this, im, 1, "side", MODE_R)
    , i_density(this, im, 1, "density", MODE_R)
    , i_motor_on(this, im, 1, "motor_on", MODE_R, CALLBACK_MOTOR_ON)

{
    device_class = "fdd";
}

FDD::~FDD()
{
    if (buffer != nullptr) delete [] buffer;
}

emulator::Result FDD::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
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
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect fdd parameters for")) + "} " + name);
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
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unknown fdd mode")) + "} " + s);

    // Track order of sector images: a single word applies to every file,
    // "ext:order" entries choose it by the extension of the file being loaded
    // (Irisha keeps .cpm images side by side and .dsk dumps cylinder by cylinder)
    s = read_confg_value(cd, "layout", false, std::string("cylinders"));
    layout_by_ext.clear();
    default_sides_layout = false;
    std::vector<std::string> items = split_string(s, '|', true);
    for (unsigned int i = 0; i < items.size(); i++) {
        std::string item = str_trim(items[i]);
        size_t colon = item.find(':');
        std::string ext  = (colon == std::string::npos) ? "" : str_tolower(str_trim(item.substr(0, colon)));
        std::string word = str_tolower(str_trim((colon == std::string::npos) ? item : item.substr(colon + 1)));
        bool v;
        if (!parse_layout_word(word, v) || (colon != std::string::npos && ext.empty()))
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unknown fdd layout")) + "} " + item);
        if (ext.empty()) {
            default_sides_layout = v;
        } else {
            if (ext[0] != '.') ext = "." + ext;
            layout_by_ext.push_back(std::make_pair(ext, v));
        }
    }
    sides_layout = default_sides_layout;

    try {
        file_name = find_file_location(sd, cd->get_parameter("image").value);
        if (!file_name.empty()) {
            emulator::Result img_res = load_image(file_name);
            if (!img_res) return img_res;
        } else {
#ifdef WASM_BUILD
            // In WASM, missing disk images are non-fatal (drive starts empty, user can load via file picker)
#else
            return emulator::Result::error(emulator::ErrorCode::ConfigError, "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Disk image file not found")) + "} " + file_name);
#endif
        }
    } catch (std::exception &e) {
    }

    return emulator::Result::ok();
}

emulator::Result FDD::load_image(const std::string &file_name)
{
    std::string ext = dsk_tools::get_file_ext(file_name);  // returns ".ext" lowercase
    std::string base_name = dsk_tools::get_filename(file_name);

    if (ext == ".mfm" || ext == ".hfe") {
        if (fdd_mode == FDD_MODE_LOGICAL)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "FDD device is working in a logical mode, no physical formats are supported")) + "}");
        HXC_MFM_HEADER hxc_header;
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (file.is_open()){
            file.read(reinterpret_cast<char*>(&hxc_header), sizeof(HXC_MFM_HEADER));
            if (memcmp(hxc_header.headername, "HXCMFM", 6) == 0) {
                sides = hxc_header.number_of_side;
                tracks = hxc_header.number_of_track;

                //The track table comes straight from the file, so its length
                //has to fit ours before anything is read into it
                if (tracks <= 0 || tracks > (int)(sizeof(track_indexes)/sizeof(track_indexes[0]))) {
                    file.close();
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unrecognized MFM format")) + "}");
                }

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
                m_generation++;
                this->file_name = base_name;
            } else {
                file.close();
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unrecognized MFM format")) + "}");
            }
            file.close();
        } else {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
        }
    } else
    if (ext == ".nib" || ext == ".nic") {
        if (fdd_mode == FDD_MODE_LOGICAL)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
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
                m_generation++;
                this->file_name = base_name;
            } else {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
            }
            file.close();
        } else {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "File is in unknown format")) + "} " + file_name);
        }
    } else
    if (ext == ".aim") {
        if (fdd_mode != FDD_MODE_AGAT_840)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "AIM files supported on Agat 840k drives only!")) + "}");
        if (buffer != nullptr) delete [] buffer;
        buffer = load_aim_image(file_name, sides, tracks, disk_size, track_indexes, aim_codes);
        track_mode = FDD_MODE_WHOLE_TRACK;
        position = 0;
        loaded = true;
        m_generation++;
        this->file_name = base_name;
    } else {
        if (fdd_mode == FDD_MODE_LOGICAL) {
            long long file_size = dsk_tools::utf8_file_size(file_name);

            // A shorter image is accepted as a disk whose remaining tracks
            // are blank: БК images come as 40- and 80-track dumps of the
            // same geometry, and the drive itself does not care
            if (file_size > 0 && file_size <= static_cast<long long>(disk_size))
            {
                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];
                memset(buffer, 0, disk_size);

                dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
                if (file.is_open()) {
                    file.read(reinterpret_cast<char*>(buffer), file_size);
                    file.close();

                    track_mode = FDD_MODE_SECTORS;
                    sides_layout = layout_for_file(file_name);
                    loaded = true;
                    m_generation++;
                    this->file_name = base_name;
                } else {
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
                }
            } else {
                this->file_name = "";
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
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
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Expected conversion from DSK to MFM is not supported yet.")) + "}");
            }
            track_mode = FDD_MODE_WHOLE_TRACK;
            position = 0;
            loaded = true;
            m_generation++;
            this->file_name = base_name;
        }
    }
    return emulator::Result::ok();
}

// True while track/side/sector name a place that exists on the loaded image.
// A controller hands over the values the guest wrote into its registers, so
// they are not trustworthy: a wrong one must answer "no such sector", never
// translate into an offset outside the buffer.
bool FDD::sector_in_range()
{
    if (side < 0 || side >= sides || track < 0 || track >= tracks) return false;
    if (track_mode == FDD_MODE_SECTORS)
        //sector 0 is the gap before the data, hence <= and not <
        return sector >= 0 && sector <= sectors;
    else
        return image_track(track, side, false) < (unsigned int)(sizeof(track_indexes)/sizeof(track_indexes[0]));
}

int FDD::SeekSector(int track, int sector)
{
    int result = FDD_SEEK_NO_DISK;
    if (buffer != nullptr)
    {
        if (sides > 1)
            this->side = ~(i_side.value) & 1;
        this->track = track;
        this->sector = sector;
        // qDebug() << "SEEK " << this->side << this->track << this->sector;
        position = 0;
        //Outside the geometry a real drive finds no address mark and the
        //controller reports RNF; here the request is simply refused
        if (!sector_in_range()) return FDD_SEEK_NO_SECTOR;
        if (track_mode == FDD_MODE_SECTORS) {
            result = sector_size;
        } else {
            result = 256;
        }
    }
    return result;
}

bool FDD::parse_layout_word(const std::string &word, bool &sides_out)
{
    if (word == "cylinders") { sides_out = false; return true; }
    if (word == "sides")     { sides_out = true;  return true; }
    return false;
}

// Track order for a file name: by its extension if listed, otherwise the default
bool FDD::layout_for_file(const std::string &file_name)
{
    std::string ext = dsk_tools::get_file_ext(file_name);   // ".ext" lower case, "" if none
    for (unsigned int i = 0; i < layout_by_ext.size(); i++)
        if (layout_by_ext[i].first == ext) return layout_by_ext[i].second;
    return default_sides_layout;
}

// Index of a track inside a sector image: either both sides of a cylinder
// go together, or the whole first side is followed by the second one
unsigned int FDD::image_track(int track, int side, bool sides_order)
{
    return sides_order ? (side*tracks + track) : (track*sides + side);
}

unsigned int FDD::translate_address()
{
    return (image_track(track, side, sides_layout)*sectors + sector-1)*sector_size + position;
}

void FDD::NextPosition()
{
    if (!sector_in_range()) {
        position = 0;
        return;
    }
    if (++position >= track_indexes[track*sides + side].mfmtracksize) position = 0;
}

uint8_t FDD::ReadNextByte()
{
    if (loaded) {
        if (track_mode == FDD_MODE_SECTORS) {
            if (position >= sector_size || !sector_in_range()) {
                //Nothing under the head: the byte the drive returns is the
                //idle state of the data line, and the buffer is left alone
                im->dm->error(this, "Reading outside of a sector");
                return 0xFF;
            }

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
            if (!sector_in_range()) return 0xFF;
            uint8_t result = buffer[track_indexes[track*sides + side].mfmtrackoffset + position++];
            if (position >= track_indexes[track*sides + side].mfmtracksize) {
                position = 0;
            }
            return result;
        }
    } else
        return 0xFF;
}

void FDD::WriteNextByte(uint8_t value)
{
    if (track_mode == FDD_MODE_SECTORS) {
        //An out of range address is not written anywhere: the byte would land
        //in whatever follows the image in the heap
        if (position >= sector_size || !sector_in_range()) {
            im->dm->error(this, "Writing outside of a sector");
            return;
        }

        if (sector != 0)
        {
            buffer[translate_address()] = value;
            position++;
        }
    } else {
        if (!sector_in_range()) return;
        buffer[track_indexes[track*sides + side].mfmtrackoffset + position++] = value;
        if (position >= track_indexes[track*sides + side].mfmtracksize) position = 0;
    }
}

void FDD::WriteByte(uint8_t value)
{
    if (track_mode == FDD_MODE_SECTORS) {
        if (position >= sector_size || !sector_in_range()) {
            im->dm->error(this, "Writing outside of a sector");
            return;
        }

        if (sector != 0)
        {
            buffer[translate_address()] = value;
        }
    } else {
        if (!sector_in_range()) return;
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
    sides_layout = default_sides_layout;
    m_generation++;
    file_name = "";
}

int FDD::get_sector_size()
{
    return sector_size;
}

int FDD::get_sides()
{
    return sides;
}

int FDD::get_tracks()
{
    return tracks;
}

int FDD::get_sectors()
{
    return sectors;
}

int FDD::get_loaded()
{
    return loaded;
}

unsigned int FDD::get_generation()
{
    return m_generation;
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

emulator::Result FDD::save_image(const std::string &file_name)
{
    if (loaded)
    {
        std::string ext = dsk_tools::get_file_ext(file_name); // returns ".ext" lowercase

        bool is_raw = (ext == ".dsk" || ext == ".gmd" || ext == ".cpm" || ext == ".img" || ext == ".bkd");

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
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
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
                    return emulator::Result::error(emulator::ErrorCode::ConfigError,
                        "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error exporting disk.")) + "} " + dsk_tools::decode_error(decode_res) + " : " + decode_res.message);
                }
            } else
            if (fdd_mode == FDD_MODE_LOGICAL) {
                dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
                if (file.is_open()){
                    bool target_layout = layout_for_file(file_name);
                    if (target_layout == sides_layout || sides < 2 || track_mode != FDD_MODE_SECTORS) {
                        file.write(reinterpret_cast<char*>(buffer), disk_size);
                    } else {
                        // The target extension keeps its tracks in the other
                        // order: reshuffle them on the way out, the image in
                        // memory stays as it is
                        std::vector<uint8_t> out(disk_size, 0);
                        const size_t track_bytes = static_cast<size_t>(sectors) * sector_size;
                        for (int t = 0; t < tracks; t++)
                            for (int s = 0; s < sides; s++)
                                memcpy(&out[image_track(t, s, target_layout) * track_bytes],
                                       buffer + image_track(t, s, sides_layout) * track_bytes,
                                       track_bytes);
                        file.write(reinterpret_cast<char*>(out.data()), disk_size);
                    }
                    file.close();
                }
            } else {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "FDD is working in a physical mode now, generating of DSK images is not supported yet.")) + "}");
            }
        } else if (ext == ".mfm") {
            if (fdd_mode == FDD_MODE_AGAT_140) {
                save_mfm_file(file_name, sides, tracks, track_indexes[0].mfmtracksize, track_indexes, buffer);
            } else {
                return emulator::Result::error(emulator::ErrorCode::ConfigError,
                    "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Saving images for this type of drive is not supported yet.")) + "}");
            }
        }
    }
    return emulator::Result::ok();
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
    if (motor_on || m_motor_was_on) {
        m_motor_was_on = false;
        led_start = std::chrono::steady_clock::now();
        led_active = true;
    }
    if (led_active) {
        auto elapsed = std::chrono::steady_clock::now() - led_start;
        if (elapsed > std::chrono::milliseconds(5000))
            led_active = false;
    }
    return led_active;
}

// Returns the AIM control code at the current position, or 0 if none exists
int FDD::aim_code()
{
    //The codes are stored per physical track, the same index the data uses:
    //taking them by cylinder alone hands side 1 the marks of another track
    const unsigned int t = image_track(track, side, false);
    if (t >= sizeof(aim_codes)/sizeof(aim_codes[0])) return 0;
    auto code = aim_codes[t].find(get_position());
    if (code != aim_codes[t].end())
        return code->second;
    else
        return 0;
}

//------------------- Introspection and control ----------------------------//

std::vector<DeviceFieldInfo> FDD::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"loaded",      "1 if an image is loaded",              false});
    r.push_back({"file",        "Name of the loaded image",             false});
    r.push_back({"layout",      "Track order of the loaded image",      false});
    r.push_back({"protected",   "1 if the image is write protected",    false});
    r.push_back({"selected",    "1 if the drive is selected",           false});
    r.push_back({"motor",       "1 if the motor is on",                 false});
    r.push_back({"led",         "1 if the activity led is on",          false});
    r.push_back({"track",       "Current track",                        false});
    r.push_back({"sector",      "Current sector",                       false});
    r.push_back({"side",        "Current side",                         false});
    r.push_back({"position",    "Current position on a track",          false});
    r.push_back({"generation",  "Incremented on every load and eject",  false});
    return r;
}

std::vector<DeviceCommandInfo> FDD::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"load",    "\"file\"", "Loads a disk image"});
    r.push_back({"save",    "\"file\"", "Writes the image to a file"});
    r.push_back({"eject",   "",         "Ejects the image"});
    r.push_back({"protect", "[0|1]",    "Sets or toggles write protection"});
    return r;
}

bool FDD::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "file") {
        out.text = file_name;
        return true;
    }
    if (field == "layout") {
        out.text = sides_layout ? "sides" : "cylinders";
        return true;
    }

    out.numeric = true;
    if (field == "loaded")      { out.values.push_back(loaded?1:0);         return true; }
    if (field == "protected")   { out.values.push_back(write_protect?1:0);  return true; }
    if (field == "selected")    { out.values.push_back(is_selected()?1:0);  return true; }
    if (field == "motor")       { out.values.push_back(motor_on?1:0);       return true; }
    if (field == "led")         { out.values.push_back(is_led_on()?1:0);    return true; }
    if (field == "track")       { out.values.push_back(track);              return true; }
    if (field == "sector")      { out.values.push_back(sector);             return true; }
    if (field == "side")        { out.values.push_back(side);               return true; }

    //A position within a track does not fit into a byte
    out.width = 32;
    if (field == "position")    { out.values.push_back(position);           return true; }
    if (field == "generation")  { out.values.push_back(m_generation);       return true; }
    out.width = 0;

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

emulator::Result FDD::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "load") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Command 'load' expects a file name")) + "}");
        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];
        return load_image(file);
    }

    if (command == "save") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Command 'save' expects a file name")) + "}");
        return save_image(resolve_output_path(sd, p[0]));
    }

    if (command == "eject") {
        unload();
        return emulator::Result::ok();
    }

    if (command == "protect") {
        //Without a parameter the flag is toggled, as the menu item does
        if (p.empty() || p[0].empty())
            change_protection();
        else {
            bool wanted = parse_numeric_value(p[0]) != 0;
            if (wanted != write_protect) change_protection();
        }
        return emulator::Result::ok();
    }

    return ComputerDevice::send_command(command, parameters);
}

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new FDD(im, cd);
}
