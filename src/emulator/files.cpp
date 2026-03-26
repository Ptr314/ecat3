// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Loader for files, source

#include <QMessageBox>
#include <sstream>

#include "files.h"
#include "emulator/utils.h"
#include "libs/crc16.h"
#include "dsk_tools/dsk_tools.h"
#include "../libs/dsk_tools/src/utils.h"

#define MIN(a, b)   ((a<b)?a:b)

bool ReadHeader(const std::string &file_name, unsigned int bytes, uint8_t* buffer, bool has_start)
{
    dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
    if (file.is_open())
    {
        if (has_start)
        {
            uint8_t preamble;
            file.read(reinterpret_cast<char*>(&preamble), 1);
            if (preamble != 0xE6)
            {
                QMessageBox::warning(0, Emulator::tr("Error"),
                                     Emulator::tr("Unable to find an expected preamble byte 0xE6!"));
                return false;
            }
        }
        file.read(reinterpret_cast<char*>(buffer), bytes);
        file.close();
    }
    return true;
}

RAM* find_ram(Emulator* e)
{
    std::vector<std::string> ram_names{"ram", "ram0", "ram1"};
    RAM* m = nullptr;

    for (const auto& n : ram_names)
    {
        m = dynamic_cast<RAM*>(e->dm->get_device_by_name(n, false));
        if (m != nullptr) break;
    }

    if (m == nullptr)
        QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Unable to find a RAM page to store data"));

    return m;
}

void load_rk(Emulator* e, const std::string &file_name)
{
    bool has_start, has_finish;

    // Check if we need to process start/stop bytes (0xE6)
    std::string ext = dsk_tools::get_file_ext(file_name);
    if (ext == ".gam")
    {
        has_start = has_finish = true;
    }
    else if (ext == ".rkm")
    {
        has_start = has_finish = false;
    }
    else
    {
        has_start = false;
        has_finish = true;
    }


    uint8_t header[16];
    if (ReadHeader(file_name, 16, header, has_start))
    {
        unsigned int offset = (has_start) ? 5 : 4;
        unsigned int delta = static_cast<unsigned int>(header[0]) * 256 + header[1];
        unsigned int len = static_cast<unsigned int>(header[2]) * 256 + header[3] - delta;

        RAM* m = find_ram(e);

        uint8_t* buffer = m->get_buffer();
        unsigned int page_size = m->get_size();

        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (!file.is_open())
        {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(QString::fromStdString(file_name)));
            return;
        }

        // Reading the main data
        file.seekg(offset, std::ios::beg);
        std::vector<uint8_t> data(len);
        file.read(reinterpret_cast<char*>(data.data()), len);
        if (!file.good())
        {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("File is smaller than expected!"));
            return;
        }

        // Checking the finalization bytes if expected
        if (has_finish)
        {
            uint8_t final_byte;
            do
            {
                file.read(reinterpret_cast<char*>(&final_byte), 1);
                if (!file.good())
                {
                    QMessageBox::warning(0, Emulator::tr("Error"),
                                         Emulator::tr("Unable to find an expected finalization byte 0xE6!"));
                    return;
                }
            }
            while (final_byte == 0);
            if (final_byte != 0xE6)
            {
                QMessageBox::warning(0, Emulator::tr("Error"),
                                     Emulator::tr("Unable to find an expected finalization byte 0xE6!"));
                return;
            }
        }

        // Getting and checking a CRC
        uint16_t expected_crc;
        file.read(reinterpret_cast<char*>(&expected_crc), 2);
        if (!file.good())
        {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Error reading CRC bytes!"));
            return;
        }
        expected_crc = ((expected_crc & 0xFF) << 8) + (expected_crc >> 8);
        uint16_t crc = 0;
        for (unsigned int i = 0; i < len; i++)
        {
            uint8_t b = data[i];
            crc += (b << 8) + b;
        }

        if (crc != expected_crc)
        {
            QMessageBox::warning(0, Emulator::tr("Error"),
                                 Emulator::tr("The file was loaded, but its checksum is incorrect!"));
        }

        memcpy(&(buffer[delta]), data.data(), MIN(len, page_size-delta));
        file.close();
    }
}

void load_bin(Emulator* e, const std::string &file_name)
{
    RAM* m = find_ram(e);

    uint8_t* buffer = m->get_buffer();
    unsigned int page_size = m->get_size();

    long long fsize = dsk_tools::utf8_file_size(file_name);
    if (fsize < 0)
    {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(QString::fromStdString(file_name)));
        return;
    }

    dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
    if (!file.is_open())
    {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(QString::fromStdString(file_name)));
        return;
    }

    unsigned int read_size = MIN(static_cast<unsigned int>(fsize), page_size);
    file.read(reinterpret_cast<char*>(buffer), read_size);
    file.close();
}

void load_hex(Emulator* e, const std::string &file_name)
{
    RAM* m = find_ram(e);
    if (m != nullptr)
    {
        uint8_t* buffer = m->get_buffer();

        std::string content = dsk_tools::utf8_read_file(file_name);
        if (content.empty())
        {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading HEX file %1").arg(QString::fromStdString(file_name)));
            return;
        }

        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.size() < 9) continue;
            unsigned int len = parse_numeric_value("$" + line.substr(1, 2));
            unsigned int addr = parse_numeric_value("$" + line.substr(3, 4));
            unsigned int type = parse_numeric_value("$" + line.substr(7, 2));
            if (type == 0)
            {
                for (unsigned int j = 0; j < len; j++)
                    buffer[addr + j] = parse_numeric_value("$" + line.substr(9 + j * 2, 2));
            }
        }
    }
}

int get_ramdisk_position(uint8_t* buffer, unsigned int top)
{
    unsigned int address = 0;
    while (address < top)
    {
        if (buffer[address] == 0xFF)
            return address;
        else
        {
            uint16_t l = buffer[address + 10] + (buffer[address + 11] << 8);
            address += l + 16;
        }
    }
    return -1;
}

void load_rko(Emulator* e, const std::string &file_name)
{
    std::string ext = dsk_tools::get_file_ext(file_name);
    bool has_preamble = ext == ".rko";

    bool add_to_disk;
    ROM* bios = dynamic_cast<ROM*>(e->dm->get_device_by_name("bios", false));
    if (bios != nullptr)
    {
        uint16_t crc = CRC16(bios->get_buffer(), bios->get_size());
        add_to_disk = crc != 0xA85E; // Orion-128 M1
    }
    else
        add_to_disk = true;

    dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
    if (!file.is_open())
    {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(QString::fromStdString(file_name)));
        return;
    }

    uint16_t file_offset = 0;
    uint16_t read_length = 0;

    if (has_preamble)
    {
        uint8_t preamble_name[8];
        file.read(reinterpret_cast<char*>(&preamble_name), sizeof(preamble_name));
        if (!file.good())
        {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
            return;
        }
        file_offset += sizeof(preamble_name);
        uint8_t b;
        do
        {
            file.read(reinterpret_cast<char*>(&b), 1);
            if (!file.good())
            {
                QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
                return;
            }
            file_offset += 1;
        }
        while (b == 0);
        if (b != 0xE6)
        {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
            return;
        }
        uint8_t preamble_data[4];
        file.read(reinterpret_cast<char*>(&preamble_data), sizeof(preamble_data));
        if (!file.good())
        {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
            return;
        }
        file_offset += sizeof(preamble_data);
        uint16_t preamble_size = (preamble_data[2] << 8) + preamble_data[3];

        read_length = preamble_size + 16;
    }

    uint8_t header_name[8];
    file.read(reinterpret_cast<char*>(&header_name), sizeof(header_name));
    if (!file.good())
    {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading header data!"));
        return;
    }
    uint8_t header_data[8];
    file.read(reinterpret_cast<char*>(&header_data), sizeof(header_data));
    uint16_t header_start = (header_data[1] << 8) + header_data[0];
    uint16_t header_size = (header_data[3] << 8) + header_data[2];

    read_length = header_size + 16;

    uint8_t* buffer;
    unsigned int page_size;
    int address;
    RAM* m;
    if (add_to_disk)
    {
        m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram1", false));
        buffer = m->get_buffer();
        page_size = 48 * 1024;
        address = get_ramdisk_position(buffer, page_size);
        if (address < 0 || address + read_length > page_size)
        {
            m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram2", false));
            buffer = m->get_buffer();
            page_size = 60 * 1024;
            address = get_ramdisk_position(buffer, page_size);
            if (address < 0 || address + read_length > page_size)
            {
                m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram3", false));
                buffer = m->get_buffer();
                address = get_ramdisk_position(buffer, page_size);
                if (address < 0 || address + read_length > page_size)
                {
                    QMessageBox::critical(0, Emulator::tr("Error"),
                                          Emulator::tr("Can't load the file: all ramdisks are full!"));
                    return;
                }
            }
        }
    }
    else
    {
        // We will load to a memory
        RAM* m = find_ram(e);
        buffer = m->get_buffer();
        page_size = m->get_size();
        file_offset += 16;
        read_length -= 16;
        address = header_start;
    }

    file.seekg(file_offset, std::ios::beg);

    std::vector<uint8_t> data(read_length);
    file.read(reinterpret_cast<char*>(data.data()), read_length);
    memcpy(&(buffer[address]), data.data(), MIN(read_length, page_size-address));

    if (add_to_disk)
        buffer[address + read_length] = 0xFF;

    file.close();
}

void HandleExternalFile(Emulator* e, const std::string &file_name)
{
    std::string ext = dsk_tools::get_file_ext(file_name);

    if (ext == ".hex") load_hex(e, file_name);
    else if (ext == ".rk" || ext == ".rkr" || ext == ".rkm" || ext == ".rka" || ext == ".gam") load_rk(e, file_name);
    else if (ext == ".rko" || ext == ".ord" || ext == ".bru") load_rko(e, file_name);
    else if (ext == ".cim" || ext == ".bin") load_bin(e, file_name);
    else
        QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Unknown file type"));
}