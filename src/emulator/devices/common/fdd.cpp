// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: FDD device, source

#include <cstring>

#include "fdd.h"
#include "emulator/utils.h"
#include "libs/mfm_tools.h"
#include "dsk_tools/core.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"

#define FDD_MODE_LOGICAL    0
#define FDD_MODE_AGAT_140   1
#define FDD_MODE_AGAT_840   2
#define FDD_MODE_DVK_MX     3
#define FDD_MODE_IBM_MFM    4
#define FDD_MODE_IBM_FM     5

#define CALLBACK_SELECT     1
#define CALLBACK_MOTOR_ON   2

emulator::Result FDC::load_drives(unsigned int min, unsigned int max, std::vector<FDD*> &drives)
{
    drives.clear();
    const std::string list = cd->get_parameter("drives", false).value;
    const std::vector<std::string> parts = split_string(list, '|', true);
    if (list.empty() || parts.size() < min || parts.size() > max)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDC|" + std::string(QT_TRANSLATE_NOOP("FDC", "Incorrect fdd list for")) + "} " + name);
    for (size_t i = 0; i < parts.size(); i++)
    {
        //Not there at all: the lookup itself reports it, by name
        FDD * fdd = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));
        if (fdd == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDC|" + std::string(QT_TRANSLATE_NOOP("FDC", "Not a fdd device")) + "} " + parts[i]);
        drives.push_back(fdd);
    }
    return emulator::Result::ok();
}

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
    else if (s == "fm_dvk_mx")
        fdd_mode = FDD_MODE_DVK_MX;
    else if (s == "mfm_ibm")
        fdd_mode = FDD_MODE_IBM_MFM;
    else if (s == "fm_ibm")
        fdd_mode = FDD_MODE_IBM_FM;
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

    if (ext == ".hfe") {
        if (fdd_mode != FDD_MODE_IBM_MFM && fdd_mode != FDD_MODE_IBM_FM && fdd_mode != FDD_MODE_DVK_MX
            && fdd_mode != FDD_MODE_AGAT_840)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "HFE images are not supported for this drive")) + "}");
        emulator::Result r = load_hfe(file_name);
        if (!r) return r;
        this->file_name = base_name;
    } else
    if (ext == ".mfm") {
        if (fdd_mode == FDD_MODE_LOGICAL)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "FDD device is working in a logical mode, no physical formats are supported")) + "}");
        HXC_MFM_HEADER hxc_header;
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (file.is_open()){
            const emulator::Result bad_format = emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unrecognized MFM format")) + "}");

            //Every read below is checked against the length of the file
            //beforehand, so none of them can come back short
            const long long file_size = dsk_tools::utf8_file_size(file_name);
            if (file_size < (long long)sizeof(HXC_MFM_HEADER)) return bad_format;

            file.read(reinterpret_cast<char*>(&hxc_header), sizeof(HXC_MFM_HEADER));
            if (memcmp(hxc_header.headername, "HXCMFM", 6) != 0) return bad_format;

            //Everything below comes straight from the file. It is read into
            //locals and checked there; the drive keeps the disk it has until
            //the new one has been accepted as a whole
            const int new_sides = hxc_header.number_of_side;
            const int new_tracks = hxc_header.number_of_track;
            const size_t table_size = sizeof(track_indexes)/sizeof(track_indexes[0]);

            //The table holds an entry per track per side, and that is how the
            //drive indexes it (track*sides + side)
            if (new_sides <= 0 || new_tracks <= 0
                || (size_t)new_sides * (size_t)new_tracks > table_size)
                return bad_format;
            const size_t entries = (size_t)new_sides * (size_t)new_tracks;

            if ((uint64_t)hxc_header.mfmtracklistoffset + sizeof(HXC_MFM_TRACK_INFO)*entries > (uint64_t)file_size)
                return bad_format;
            std::vector<HXC_MFM_TRACK_INFO> table(entries);
            file.seekg(hxc_header.mfmtracklistoffset, std::ios::beg);
            file.read(reinterpret_cast<char*>(table.data()), sizeof(HXC_MFM_TRACK_INFO)*entries);

            //The tracks need not be equal, nor follow one another: the data is
            //everything between the first byte of the lowest track and the
            //last byte of the highest
            uint64_t data_begin = table[0].mfmtrackoffset;
            uint64_t data_end = 0;
            for (size_t i = 0; i < entries; i++) {
                const uint64_t from = table[i].mfmtrackoffset;
                const uint64_t to = from + table[i].mfmtracksize;
                if (table[i].mfmtracksize == 0) return bad_format;
                if (from < data_begin) data_begin = from;
                if (to > data_end) data_end = to;
            }
            if (data_end - data_begin > 64u * 1024 * 1024 || data_end > (uint64_t)file_size)
                return bad_format;

            const size_t new_size = (size_t)(data_end - data_begin);
            uint8_t * new_buffer = new uint8_t[new_size];
            file.seekg((std::streamoff)data_begin, std::ios::beg);
            file.read(reinterpret_cast<char*>(new_buffer), new_size);
            file.close();

            if (buffer != nullptr) delete [] buffer;
            buffer = new_buffer;
            disk_size = (int)new_size;
            sides = new_sides;
            tracks = new_tracks;
            for (size_t i = 0; i < entries; i++) {
                track_indexes[i] = table[i];
                track_indexes[i].mfmtrackoffset = (uint32_t)(table[i].mfmtrackoffset - data_begin);
            }

            track_mode = FDD_MODE_WHOLE_TRACK;
            position = 0;
            loaded = true;
            m_generation++;
            this->file_name = base_name;
            // Пометок синхро в .mfm нет: у IBM MFM их видно по самим байтам
            for (size_t i = 0; i < entries && i < sizeof(aim_codes) / sizeof(aim_codes[0]); i++) {
                aim_codes[i].clear();
                if (fdd_mode == FDD_MODE_IBM_MFM)
                    find_marks_ibm_mfm(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize, aim_codes[i]);
                else if (fdd_mode == FDD_MODE_IBM_FM)
                    find_marks_ibm_fm(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize, aim_codes[i]);
            }
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
            // Convert to MFM. The converters throw on a file they cannot take
            // (too short, unreadable); the drive keeps its disk then, and the
            // error is reported instead of leaving the drive silently empty
            uint8_t * converted = nullptr;
            // The converters set the geometry before they read the file: put
            // it back if they fail, it describes the disk that stays
            const int old_sides = sides, old_tracks = tracks, old_size = disk_size;
            try {
                switch (fdd_mode) {
                    case FDD_MODE_DVK_MX: {
                        // Плоский образ секторов - дорожки, какими их пишет
                        // RT-11. Образ короче геометрии - недостающие пустые
                        const long long file_size = dsk_tools::utf8_file_size(file_name);
                        const long long flat_size = (long long)sides * tracks * DVK_MX_SECTORS * DVK_MX_SECTOR_SIZE;
                        if (file_size <= 0 || file_size > flat_size)
                            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect disk image size for")) + "} " + file_name);
                        std::vector<uint8_t> flat((size_t)file_size, 0);
                        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
                        if (!file.is_open())
                            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
                        file.read(reinterpret_cast<char*>(flat.data()), (std::streamsize)flat.size());
                        converted = generate_tracks_dvk_mx(flat.data(), flat.size(), sides, tracks, disk_size, track_indexes);
                        break;
                    }
                    case FDD_MODE_IBM_FM:
                    case FDD_MODE_IBM_MFM: {
                        // Плоский образ секторов - дорожки, как их размечает
                        // ПЗУ. Образ короче геометрии - остаток пустой
                        const long long file_size = dsk_tools::utf8_file_size(file_name);
                        const long long flat_size = (long long)sides * tracks * sectors * sector_size;
                        if (file_size <= 0 || file_size > flat_size)
                            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect disk image size for")) + "} " + file_name);
                        std::vector<uint8_t> flat((size_t)file_size, 0);
                        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
                        if (!file.is_open())
                            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
                        file.read(reinterpret_cast<char*>(flat.data()), (std::streamsize)flat.size());
                        sides_layout = layout_for_file(file_name);
                        if (fdd_mode == FDD_MODE_IBM_FM)
                            converted = generate_tracks_ibm_fm(flat.data(), flat.size(), sides, tracks, sectors, sector_size,
                                                               sides_layout, nullptr, disk_size, track_indexes, aim_codes);
                        else
                            converted = generate_tracks_ibm_mfm(flat.data(), flat.size(), sides, tracks, sectors, sector_size,
                                                                sides_layout, nullptr, disk_size, track_indexes, aim_codes);
                        break;
                    }
                    case FDD_MODE_AGAT_140:
                        converted = generate_mfm_agat_140(file_name, sides, tracks, disk_size, track_indexes);
                        break;
                    case FDD_MODE_AGAT_840:
                        converted = generate_mfm_agat_840(file_name, sides, tracks, disk_size, track_indexes, aim_codes);
                        break;
                    default:
                        return emulator::Result::error(emulator::ErrorCode::ConfigError,
                            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Expected conversion from DSK to MFM is not supported yet.")) + "}");
                }
            } catch (const std::exception &e) {
                sides = old_sides;
                tracks = old_tracks;
                disk_size = old_size;
                return emulator::Result::error(emulator::ErrorCode::ConfigError, e.what());
            }
            if (buffer != nullptr) delete [] buffer;
            buffer = converted;
            track_mode = FDD_MODE_WHOLE_TRACK;
            position = 0;
            loaded = true;
            m_generation++;
            this->file_name = base_name;
        }
    }
    //Every branch that took the image ends by naming it; one that did not
    //(an unknown extension) leaves the previous image and its path alone
    if (loaded && this->file_name == base_name) image_path = file_name;
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
    //A sector image may have more tracks than the table of physical ones
    const unsigned int index = image_track(track, side, false);
    if (index >= sizeof(track_indexes)/sizeof(track_indexes[0])) {
        position = 0;
        return;
    }
    if (++position >= (int)track_indexes[index].mfmtracksize) position = 0;
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
            if (position >= (int)track_indexes[track*sides + side].mfmtracksize) {
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
        clear_aim_desync();
        buffer[track_indexes[track*sides + side].mfmtrackoffset + position++] = value;
        if (position >= (int)track_indexes[track*sides + side].mfmtracksize) position = 0;
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
        clear_aim_desync();
        buffer[track_indexes[track*sides + side].mfmtrackoffset + position] = value;
    }
}


//------------------------- HFE ---------------------------------------------//

// Режим дорожек привода - в разметку dsk_tools: скорость, обороты и
// кодирование HFE берутся оттуда (MX - 125 кбит/с, FM - 360 об/мин)
static dsk_tools::TrackLayout hfe_layout(int mode)
{
    switch (mode) {
    case FDD_MODE_IBM_MFM: return dsk_tools::TrackLayout::IbmMfm;
    case FDD_MODE_IBM_FM:  return dsk_tools::TrackLayout::IbmFm;
    case FDD_MODE_DVK_MX:  return dsk_tools::TrackLayout::DvkMx;
    default:               return dsk_tools::TrackLayout::None;
    }
}

// Байт дорожки в буфере привода: у MX целые слова, оборот же 1562,5 слова
static int hfe_buffer_bytes(int mode, const dsk_tools::DiskFormatParams &format)
{
    return (mode == FDD_MODE_DVK_MX) ? DVK_MX_TRACK_WORDS * 2 : dsk_tools::track_bytes(format);
}

// Дорожки в ячейках - как они проходят под головкой. Пометки синхро
// становятся байтами без синхроимпульса: у MFM вся серия A1 от пометки, у
// FM каждая метка. Слово MX уходит старшим разрядом вперёд
emulator::Result FDD::save_hfe(const std::string &file_name)
{
    dsk_tools::DiskFormatParams format;
    format.layout = hfe_layout(fdd_mode);
    const bool agat = (fdd_mode == FDD_MODE_AGAT_840);
    if (!whole_track() || (format.layout == dsk_tools::TrackLayout::None && !agat))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "HFE images are not supported for this drive")) + "}");
    format.tracks = tracks;
    format.heads = sides;
    dsk_tools::HfeImage img;
    if (agat) {
        // Заголовок - как у dsk_tools: MFM 250 кбит/с; дорожка - сколько
        // байт в ней у привода (6381 у построенной из образа, 6464 у AIM)
        img.tracks = tracks;
        img.sides = sides;
        img.bitrate = 250;
        img.rpm = AGAT_840_RPM;
        img.encoding = ISOIBM_MFM_ENCODING;
        img.interface_mode = GENERIC_SHUGGART_DD_FLOPPYMODE;
        img.cells.resize((size_t)tracks * sides);
    } else
        dsk_tools::track_hfe_params(format, img);
    img.write_allowed = !write_protect;
    const size_t turn = agat ? 0 : (size_t)dsk_tools::track_bytes(format);
    for (int t = 0; t < tracks; t++)
        for (int s = 0; s < sides; s++) {
            const unsigned int i = image_track(t, s, false);
            std::vector<uint8_t> data(buffer + track_indexes[i].mfmtrackoffset,
                                      buffer + track_indexes[i].mfmtrackoffset + track_indexes[i].mfmtracksize);
            if (agat) {
                agat_840_to_cells(data.data(), data.size(), aim_codes[i], img.cells[(size_t)t * sides + s]);
                continue;
            }
            std::vector<uint8_t> special(data.size(), 0);
            if (fdd_mode == FDD_MODE_DVK_MX) {
                for (size_t k = 0; k + 1 < data.size(); k += 2) std::swap(data[k], data[k + 1]);
                // Оборот - 1562,5 слова: последние полслова - нули
                data.resize(turn, 0);
                special.resize(turn, 0);
            } else
                special_from_marks(aim_codes[i], data.data(), data.size(), fdd_mode == FDD_MODE_IBM_MFM, special);
            if (fdd_mode == FDD_MODE_IBM_MFM)
                dsk_tools::mfm_encode(data.data(), special.data(), data.size(), img.cells[(size_t)t * sides + s]);
            else
                dsk_tools::fm_encode(data.data(), special.data(), data.size(), img.cells[(size_t)t * sides + s]);
        }
    dsk_tools::BYTES out;
    dsk_tools::hfe_write(img, out);
    dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
    if (!file.is_open())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
    file.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
    return emulator::Result::ok();
}

emulator::Result FDD::load_hfe(const std::string &file_name)
{
    const long long size = dsk_tools::utf8_file_size(file_name);
    if (size <= 0 || size > 64ll * 1024 * 1024)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Incorrect disk image size for")) + "} " + file_name);
    dsk_tools::BYTES in((size_t)size);
    {
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (!file.is_open())
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Error opening file")) + "} " + file_name);
        file.read(reinterpret_cast<char*>(in.data()), (std::streamsize)in.size());
    }
    dsk_tools::HfeImage img;
    const dsk_tools::Result res = dsk_tools::hfe_read(in, img);
    const size_t table = sizeof(track_indexes) / sizeof(track_indexes[0]);
    if (!res || (size_t)img.tracks * img.sides > table)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unrecognized MFM format")) + "} " + res.message);

    dsk_tools::DiskFormatParams format;
    format.layout = hfe_layout(fdd_mode);
    const bool agat = (fdd_mode == FDD_MODE_AGAT_840);
    // У Агата дорожка - сколько байт в ячейках (у образов dsk_tools 6464), у
    // всех одна, по самой длинной: контроллер длины не знает, от неё зависит
    // только оборот
    size_t agat_len = 0;
    if (agat)
        for (size_t i = 0; i < img.cells.size(); i++)
            if (img.cells[i].size() / 2 > agat_len) agat_len = img.cells[i].size() / 2;
    const size_t turn = agat ? agat_len : (size_t)dsk_tools::track_bytes(format);
    const int track_bytes = agat ? (int)agat_len : hfe_buffer_bytes(fdd_mode, format);
    if (track_bytes <= 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{FDD|" + std::string(QT_TRANSLATE_NOOP("FDD", "Unrecognized MFM format")) + "} " + file_name);
    const int new_size = img.tracks * img.sides * track_bytes;
    uint8_t * new_buffer = new uint8_t[new_size];
    for (size_t i = 0; i < table; i++) aim_codes[i].clear();
    for (int t = 0; t < img.tracks; t++)
        for (int s = 0; s < img.sides; s++) {
            const int i = t * img.sides + s;
            dsk_tools::BYTES data, special;
            const dsk_tools::BYTES &cells = img.cells[(size_t)i];
            if (agat) {
                // Короткая дорожка добивается щелью: ячейки 22h - это $AA
                std::vector<uint8_t> padded(cells);
                padded.resize(turn * 2, 0x22);
                std::vector<uint8_t> track;
                agat_840_from_cells(padded, track, aim_codes[i]);
                memcpy(new_buffer + (size_t)i * track_bytes, track.data(), track_bytes);
                track_indexes[i].track_number = (uint16_t)t;
                track_indexes[i].side_number = (uint8_t)s;
                track_indexes[i].mfmtracksize = track_bytes;
                track_indexes[i].mfmtrackoffset = (uint32_t)(i * track_bytes);
                continue;
            }
            if (fdd_mode == FDD_MODE_IBM_MFM) dsk_tools::mfm_decode(cells, track_bytes, data, special);
            else dsk_tools::fm_decode(cells, turn, data, special);
            if (fdd_mode == FDD_MODE_DVK_MX)
                for (size_t k = 0; k + 1 < (size_t)track_bytes; k += 2) std::swap(data[k], data[k + 1]);
            memcpy(new_buffer + (size_t)i * track_bytes, data.data(), track_bytes);
            // Пометка у MFM - первый байт серии синхро, у FM - каждая метка
            special.resize(track_bytes);
            marks_from_special(special, fdd_mode == FDD_MODE_IBM_MFM, aim_codes[i]);
            track_indexes[i].track_number = (uint16_t)t;
            track_indexes[i].side_number = (uint8_t)s;
            track_indexes[i].mfmtracksize = track_bytes;
            track_indexes[i].mfmtrackoffset = (uint32_t)(i * track_bytes);
        }
    if (buffer != nullptr) delete [] buffer;
    buffer = new_buffer;
    disk_size = new_size;
    sides = img.sides;
    tracks = img.tracks;
    track_mode = FDD_MODE_WHOLE_TRACK;
    position = 0;
    loaded = true;
    // Защищённый от записи образ (у Gotek - флаг в заголовке) защищает и
    // привод; снять защиту можно как обычно
    if (!img.write_allowed) write_protect = true;
    m_generation++;
    return emulator::Result::ok();
}

bool FDD::whole_track() const
{
    return loaded && buffer != nullptr && track_mode == FDD_MODE_WHOLE_TRACK;
}

// Дорожка целиком - для контроллера, который пишет и читает её от индекса до
// индекса сам. Сторона задана явно: у одностороннего привода её выбирает
// контроллер (head_side), линию стороны привод не слушает
bool FDD::read_track(int track, int side, std::vector<uint8_t> &out)
{
    out.clear();
    if (!whole_track() || track < 0 || track >= tracks || side < 0 || side >= sides) return false;
    const unsigned int i = image_track(track, side, false);
    if (i >= sizeof(track_indexes) / sizeof(track_indexes[0])) return false;
    const HXC_MFM_TRACK_INFO &ti = track_indexes[i];
    if ((uint64_t)ti.mfmtrackoffset + ti.mfmtracksize > (uint64_t)disk_size) return false;
    out.assign(buffer + ti.mfmtrackoffset, buffer + ti.mfmtrackoffset + ti.mfmtracksize);
    // Головка привода - над этой дорожкой (поля track и side)
    this->track = track;
    this->side = side;
    return true;
}

// Записанное ложится с начала дорожки (с индекса); что дальше - остаётся
bool FDD::write_track(int track, int side, const std::vector<uint8_t> &data)
{
    if (!whole_track() || track < 0 || track >= tracks || side < 0 || side >= sides) return false;
    const unsigned int i = image_track(track, side, false);
    if (i >= sizeof(track_indexes) / sizeof(track_indexes[0])) return false;
    const HXC_MFM_TRACK_INFO &ti = track_indexes[i];
    if ((uint64_t)ti.mfmtrackoffset + ti.mfmtracksize > (uint64_t)disk_size) return false;
    const size_t n = data.size() < ti.mfmtracksize ? data.size() : ti.mfmtracksize;
    if (n > 0) memcpy(buffer + ti.mfmtrackoffset, data.data(), n);
    this->track = track;
    this->side = side;
    return true;
}

// Пометки синхро дорожки - позиции байтов
void FDD::read_track_marks(int track, int side, std::vector<int> &positions)
{
    positions.clear();
    if (!whole_track() || track < 0 || track >= tracks || side < 0 || side >= sides) return;
    const unsigned int i = image_track(track, side, false);
    if (i >= sizeof(aim_codes) / sizeof(aim_codes[0])) return;
    for (const auto &e : aim_codes[i])
        if (aim_is_desync(e.second)) positions.push_back(e.first);
}

void FDD::write_track_marks(int track, int side, const std::vector<int> &positions)
{
    if (!whole_track() || track < 0 || track >= tracks || side < 0 || side >= sides) return;
    const unsigned int i = image_track(track, side, false);
    if (i >= sizeof(aim_codes) / sizeof(aim_codes[0])) return;
    aim_codes[i].clear();
    for (int p : positions) aim_codes[i][p] = AIM_CMD_DESYNC;
}

// Привод в режиме секторов (снимок или конфигурация до режима дорожек):
// плоский образ в памяти перестраивается в дорожки IBM MFM. deleted - метки
// удаления по физическим дорожкам, если они известны
void FDD::rebuild_ibm_mfm(const uint16_t * deleted)
{
    if (!loaded || buffer == nullptr || track_mode != FDD_MODE_SECTORS) return;
    int size = 0;
    uint8_t * converted = generate_tracks_ibm_mfm(buffer, (size_t)disk_size, sides, tracks, sectors, sector_size,
                                                  sides_layout, deleted, size, track_indexes, aim_codes);
    delete [] buffer;
    buffer = converted;
    disk_size = size;
    fdd_mode = FDD_MODE_IBM_MFM;
    track_mode = FDD_MODE_WHOLE_TRACK;
    position = 0;
}

// То же для дорожек IBM 3740 FM (RX01)
void FDD::rebuild_ibm_fm()
{
    if (!loaded || buffer == nullptr || track_mode != FDD_MODE_SECTORS) return;
    int size = 0;
    uint8_t * converted = generate_tracks_ibm_fm(buffer, (size_t)disk_size, sides, tracks, sectors, sector_size,
                                                 sides_layout, nullptr, size, track_indexes, aim_codes);
    delete [] buffer;
    buffer = converted;
    disk_size = size;
    fdd_mode = FDD_MODE_IBM_FM;
    track_mode = FDD_MODE_WHOLE_TRACK;
    position = 0;
}

// Привод, восстановленный из снимка, сделанного до режима дорожек: плоский
// образ в памяти перестраивается в дорожки MX
void FDD::rebuild_dvk_mx()
{
    if (!loaded || buffer == nullptr || track_mode != FDD_MODE_SECTORS) return;
    int size = 0;
    uint8_t * converted = generate_tracks_dvk_mx(buffer, (size_t)disk_size, sides, tracks, size, track_indexes);
    delete [] buffer;
    buffer = converted;
    disk_size = size;
    fdd_mode = FDD_MODE_DVK_MX;
    track_mode = FDD_MODE_WHOLE_TRACK;
    position = 0;
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
    image_path.clear();
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
            if ((fdd_mode == FDD_MODE_IBM_MFM || fdd_mode == FDD_MODE_IBM_FM) && track_mode == FDD_MODE_WHOLE_TRACK) {
                // Сектора по меткам; метки удаления и нестандартная
                // разметка в плоский образ не попадают
                std::vector<uint8_t> flat;
                if (fdd_mode == FDD_MODE_IBM_FM)
                    decode_tracks_ibm_fm(buffer, sides, tracks, sectors, sector_size, layout_for_file(file_name),
                                         track_indexes, flat);
                else
                    decode_tracks_ibm_mfm(buffer, sides, tracks, sectors, sector_size, layout_for_file(file_name),
                                          track_indexes, flat);
                dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
                if (file.is_open()) {
                    file.write(reinterpret_cast<char*>(flat.data()), (std::streamsize)flat.size());
                    file.close();
                }
            } else
            if (fdd_mode == FDD_MODE_DVK_MX && track_mode == FDD_MODE_WHOLE_TRACK) {
                // Сектора дорожек по синхрослову; служебные слова и
                // нестандартная разметка в плоский образ не попадают
                std::vector<uint8_t> flat;
                decode_tracks_dvk_mx(buffer, sides, tracks, track_indexes, flat);
                dsk_tools::UTF8_ofstream file(file_name, std::ios::binary);
                if (file.is_open()) {
                    file.write(reinterpret_cast<char*>(flat.data()), (std::streamsize)flat.size());
                    file.close();
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
        } else if (ext == ".hfe") {
            return save_hfe(file_name);
        } else if (ext == ".mfm") {
            if (fdd_mode == FDD_MODE_AGAT_140
                || ((fdd_mode == FDD_MODE_DVK_MX || fdd_mode == FDD_MODE_IBM_MFM || fdd_mode == FDD_MODE_IBM_FM)
                    && track_mode == FDD_MODE_WHOLE_TRACK)) {
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

// The controller has broken the bit stream at the byte under the head: that is
// what a later read locks on, so the mark belongs on the medium and not in the
// controller. Formatting a track lays these down one by one.
void FDD::mark_aim_desync()
{
    const unsigned int t = image_track(track, side, false);
    if (t >= sizeof(aim_codes)/sizeof(aim_codes[0])) return;
    aim_codes[t][position] = AIM_CMD_DESYNC;
}

// Writing a byte over a place that used to hold a sync mark destroys it, which
// is what lets a format replace the layout of a track instead of adding to it.
// An index pulse is a property of the disk, not of the data, and stays put.
void FDD::clear_aim_desync()
{
    const unsigned int t = image_track(track, side, false);
    if (t >= sizeof(aim_codes)/sizeof(aim_codes[0])) return;
    auto code = aim_codes[t].find(position);
    if (code != aim_codes[t].end() && aim_is_desync(code->second))
        aim_codes[t].erase(code);
}

//------------------- Introspection and control ----------------------------//

ConfigFields FDD::get_config_fields()
{
    ConfigField f;
    f.name = "image";
    f.title = QT_TRANSLATE_NOOP("ConfigFields", "Disk image");
    f.type = CONFIG_FIELD_FILE;
    f.files = cd->get_parameter("files", false).value;
    return {f};
}

void FDD::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);

    //Where the head stands, whatever is in the drive
    w.n("side", static_cast<uint32_t>(side));
    w.n("track", static_cast<uint32_t>(track));
    w.n("sector", static_cast<uint32_t>(sector));
    w.n("position", static_cast<uint32_t>(position));
    w.n("selector", selector);
    w.b("motor_on", motor_on);
    w.b("motor_was_on", m_motor_was_on);
    w.b("protected", write_protect);
    w.b("loaded", loaded);
    w.n("generation", m_generation);
    //The led is timed against the host clock and starts again from now

    if (!loaded || buffer == nullptr || disk_size <= 0) return;

    //The geometry the bytes are read with. A drive loads an image by its
    //extension, and the state carries neither the extension nor the loader
    w.s("file_name", file_name);
    w.n("sides", static_cast<uint32_t>(sides));
    w.n("tracks", static_cast<uint32_t>(tracks));
    w.n("sectors", static_cast<uint32_t>(sectors));
    w.n("sector_size", static_cast<uint32_t>(sector_size));
    w.n("disk_size", static_cast<uint32_t>(disk_size));
    w.n("fdd_mode", static_cast<uint32_t>(fdd_mode));
    w.n("track_mode", static_cast<uint32_t>(track_mode));
    w.n("stream_format", stream_format);
    w.b("sides_layout", sides_layout);
    w.n("physical_track_len", static_cast<uint32_t>(physical_track_len));

    //Raw, not through save_image(): that converts by extension - it reshuffles
    //tracks, rebuilds streams - and a round trip that is almost exact would
    //give a machine that cannot tell it was restarted except on one sector
    w.blob("data", dsk_tools::get_filename(file_name.empty() ? (name + ".raw") : file_name),
           buffer, static_cast<size_t>(disk_size));

    if (track_mode != FDD_MODE_SECTORS)
    {
        //The table that says where each physical track begins
        const size_t count = sizeof(track_indexes) / sizeof(track_indexes[0]);
        std::vector<uint32_t> number(count), side_no(count), size(count), offset(count);
        for (size_t i = 0; i < count; i++)
        {
            number[i]  = track_indexes[i].track_number;
            side_no[i] = track_indexes[i].side_number;
            size[i]    = track_indexes[i].mfmtracksize;
            offset[i]  = track_indexes[i].mfmtrackoffset;
        }
        w.array("track_number", number.data(), count);
        w.array("track_side", side_no.data(), count);
        w.array("track_size", size.data(), count);
        w.array("track_offset", offset.data(), count);

        //Sync marks and AIM codes: positions and codes of the marked bytes,
        //a pair per mark, for every track that has any
        const size_t codes = sizeof(aim_codes) / sizeof(aim_codes[0]);
        for (size_t t = 0; t < codes; t++)
        {
            if (aim_codes[t].empty()) continue;
            std::vector<uint32_t> pairs;
            for (const auto &e : aim_codes[t])
            {
                pairs.push_back(static_cast<uint32_t>(e.first));
                pairs.push_back(static_cast<uint32_t>(e.second));
            }
            w.n_at("marks", static_cast<unsigned int>(t), static_cast<unsigned int>(pairs.size() / 2));
            w.array(("mark" + std::to_string(t)).c_str(), pairs.data(), pairs.size());
        }
    }
}

emulator::Result FDD::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;

    r.u("side", side);
    r.u("track", track);
    r.u("sector", sector);
    r.u("position", position);
    r.u("selector", selector);
    r.b("motor_on", motor_on);
    r.b("motor_was_on", m_motor_was_on);
    r.b("protected", write_protect);

    bool was_loaded = false;
    if (!r.b("loaded", was_loaded) || !was_loaded) return emulator::Result::ok();
    r.u("generation", m_generation);

    uint32_t size = 0;
    if (!r.u("disk_size", size) || size == 0) return emulator::Result::ok();

    //An empty name is not written at all (StateWriter::s). The contents come
    //from the snapshot, so no file on disk is this image any more
    file_name.clear();
    image_path.clear();
    r.s("file_name", file_name);
    r.u("sides", sides);
    r.u("tracks", tracks);
    r.u("sectors", sectors);
    r.u("sector_size", sector_size);
    r.u("fdd_mode", fdd_mode);
    r.u("track_mode", track_mode);
    r.u("stream_format", stream_format);
    r.b("sides_layout", sides_layout);
    r.u("physical_track_len", physical_track_len);

    //A saved state is a file like any other and may come from anywhere (the
    //web page opens one by a link). The guest reaches the buffer through the
    //geometry, so the two are made to agree here, once: sector_in_range()
    //checks a request against the geometry and nothing checks it again
    //against the size of the buffer
    const uint32_t max_disk = 64u * 1024 * 1024;        //No more than an archive entry may hold
    const size_t table_size = sizeof(track_indexes) / sizeof(track_indexes[0]);
    const emulator::Result bad_geometry = emulator::Result::error(emulator::ErrorCode::FileError,
        "{MachineState|Saved state} " + name + ": the disk geometry does not fit the image");

    delete [] buffer;
    buffer = nullptr;
    loaded = false;
    disk_size = 0;

    if (size > max_disk || sides <= 0 || sides > 255 || tracks <= 0 || tracks > 65535)
        return bad_geometry;

    uint64_t alloc = size;
    if (track_mode == FDD_MODE_SECTORS)
    {
        //Of a drive that works with whole tracks these two mean nothing
        if (sectors < 0 || sectors > 65535 || sector_size < 0 || sector_size > 65536)
            return bad_geometry;
        //An image shorter than its geometry is a disk whose last tracks are
        //blank, the same as when it is loaded from a file
        const uint64_t need = static_cast<uint64_t>(sides) * tracks * sectors * sector_size;
        if (need > max_disk) return bad_geometry;
        if (need > alloc) alloc = need;
    } else {
        if (static_cast<uint64_t>(sides) * tracks > table_size) return bad_geometry;
    }

    buffer = new uint8_t[static_cast<size_t>(alloc)];
    memset(buffer, 0, static_cast<size_t>(alloc));
    if (!r.blob_into("data", buffer, static_cast<size_t>(size)))
    {
        delete [] buffer;
        buffer = nullptr;
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{MachineState|Saved state} " + name + ": " + r.error());
    }
    disk_size = static_cast<int>(alloc);

    if (track_mode != FDD_MODE_SECTORS)
    {
        const size_t count = table_size;
        std::vector<uint32_t> number(count, 0), side_no(count, 0), tsize(count, 0), offset(count, 0);
        r.array("track_number", number.data(), count);
        r.array("track_side", side_no.data(), count);
        r.array("track_size", tsize.data(), count);
        r.array("track_offset", offset.data(), count);

        const size_t used = static_cast<size_t>(sides) * static_cast<size_t>(tracks);
        for (size_t i = 0; i < count; i++)
        {
            //A track is read from its offset even when its length is zero
            const uint64_t end = static_cast<uint64_t>(offset[i]) + (tsize[i] != 0 ? tsize[i] : 1);
            if (end > alloc)
            {
                if (i < used)
                {
                    delete [] buffer;
                    buffer = nullptr;
                    disk_size = 0;
                    return bad_geometry;
                }
                //An entry no track uses holds whatever was there when the
                //state was taken; it is only made harmless
                tsize[i] = 0;
                offset[i] = 0;
            }
            track_indexes[i].track_number   = static_cast<uint16_t>(number[i]);
            track_indexes[i].side_number    = static_cast<uint8_t>(side_no[i]);
            track_indexes[i].mfmtracksize   = tsize[i];
            track_indexes[i].mfmtrackoffset = offset[i];
        }

        //Sync marks and AIM codes; a snapshot from before they were saved
        //has none, and the tracks keep what the reset left (nothing)
        for (size_t t = 0; t < sizeof(aim_codes) / sizeof(aim_codes[0]); t++)
        {
            aim_codes[t].clear();
            uint32_t n = 0;
            if (!r.u_at("marks", static_cast<unsigned int>(t), n) || n == 0 || n > 65536) continue;
            std::vector<uint32_t> pairs(n * 2, 0);
            if (!r.array(("mark" + std::to_string(t)).c_str(), pairs.data(), pairs.size())) continue;
            for (uint32_t k = 0; k < n; k++)
                aim_codes[t][static_cast<int>(pairs[k * 2])] = static_cast<int>(pairs[k * 2 + 1]);
        }

        //The head stands somewhere on the track it is over, or at its start
        if (position < 0 || !sector_in_range()
            || static_cast<uint32_t>(position) >= track_indexes[track*sides + side].mfmtracksize)
            position = 0;
    }
    loaded = true;
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> FDD::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"loaded",      "1 if an image is loaded",              false});
    r.push_back({"file",        "Name of the loaded image",             false});
    r.push_back({"path",        "Full path of the loaded image",        false});
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
    r.push_back({"raw",         "Bytes of the track under the head",    true});
    r.push_back({"marks",       "Sync marks on the track under the head", false});
    r.push_back({"deleted",     "IBM MFM/FM disk: sectors with the deleted data mark", false});
    r.push_back({"checksum",    "Whole-track disk: a hash of the tracks and their sync marks", false});
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
    if (field == "path") {
        out.text = image_path;
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

    //The track as it lies on the medium, sync marks and all: the only way to
    //see what a guest's formatter actually wrote
    if (field == "checksum")
    {
        if (!loaded || track_mode != FDD_MODE_WHOLE_TRACK) return false;
        //FNV-1a over every track of the geometry and its marks: equal after a
        //round trip through a file means the tracks came back as they were
        uint32_t h = 2166136261u;
        auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
        for (int t = 0; t < tracks; t++)
            for (int sd = 0; sd < sides; sd++)
            {
                const unsigned int i = image_track(t, sd, false);
                for (uint32_t k = 0; k < track_indexes[i].mfmtracksize; k++)
                    mix(buffer[track_indexes[i].mfmtrackoffset + k]);
                for (const auto &e : aim_codes[i]) { mix((uint32_t)e.first); mix((uint32_t)e.second); }
            }
        out.width = 32;
        out.values.push_back(h);
        return true;
    }

    if (field == "deleted")
    {
        if (!loaded || track_mode != FDD_MODE_WHOLE_TRACK
            || (fdd_mode != FDD_MODE_IBM_MFM && fdd_mode != FDD_MODE_IBM_FM)) return false;
        unsigned int count = 0;
        std::vector<uint8_t> tmp((size_t)sectors * sector_size);
        for (int t = 0; t < tracks; t++)
            for (int sd = 0; sd < sides; sd++)
            {
                const unsigned int i = image_track(t, sd, false);
                int n = 0;
                uint16_t del = 0;
                const bool ok = (fdd_mode == FDD_MODE_IBM_FM)
                    ? dsk_tools::ibm_fm_read_track(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize,
                                                   sectors, sector_size, tmp.data(), del, n)
                    : dsk_tools::ibm_mfm_read_track(buffer + track_indexes[i].mfmtrackoffset, track_indexes[i].mfmtracksize,
                                                    sectors, sector_size, tmp.data(), del, n);
                if (ok)
                    for (; del != 0; del &= (uint16_t)(del - 1)) count++;
            }
        out.values.push_back(count);
        return true;
    }

    if (field == "raw" || field == "marks")
    {
        const unsigned int t = image_track(track, side, false);
        if (!loaded || track_mode != FDD_MODE_WHOLE_TRACK
            || t >= sizeof(track_indexes)/sizeof(track_indexes[0])) return false;

        if (field == "marks")
        {
            out.numeric = false;
            for (std::map<int,int>::const_iterator i = aim_codes[t].begin(); i != aim_codes[t].end(); ++i)
            {
                if (!out.text.empty()) out.text += " ";
                out.text += std::to_string(i->first) + ":" + std::to_string(i->second);
            }
            return true;
        }

        const unsigned int len = track_indexes[t].mfmtracksize;
        if (len == 0) return false;
        if (to >= len) to = len - 1;
        if (from > to) from = to;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        for (unsigned int i = from; i <= to; i++)
            out.values.push_back(buffer[track_indexes[t].mfmtrackoffset + i]);
        return true;
    }

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
