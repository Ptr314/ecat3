#include <QFileInfo>
#include <QMessageBox>

#include "files.h"
#include "emulator/utils.h"
#include "libs/crc16.h"

#define MIN(a, b)   ((a<b)?a:b)

bool ReadHeader(QString file_name, unsigned int bytes, uint8_t * buffer, bool has_start)
{
    QFile file(file_name);
    if (file.open(QIODevice::ReadOnly)){
        if (has_start) {
            uint8_t preamble;
            file.read(reinterpret_cast<char*>(&preamble), 1);
            if (preamble != 0xE6) {
                QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Unable to find an expected preamble byte 0xE6!"));
                return false;
            }
        }
        QByteArray data = file.read(bytes);
        memcpy(buffer, data.constData(), bytes);
        file.close();
    }
    return true;
}

RAM * find_ram(Emulator * e)
{
    QStringList ram_names {"ram", "ram0", "ram1"};
    RAM * m = nullptr;

    for ( const auto& n : ram_names )
    {
        m = dynamic_cast<RAM*>(e->dm->get_device_by_name(n, false));
        if (m != nullptr) break;
    }

    if (m==nullptr)
        QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Unable to find a RAM page to store data"));

    return m;

}

void load_rk(Emulator * e, QString file_name)
{
    bool has_start, has_finish;

    // Check if we need to process start/stop bytes (0xE6)
    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();
    if (ext == "gam") {
        has_start = has_finish = true;
    } else
        if (ext == "rkm") {
            has_start = has_finish = false;
        } else {
            has_start = false;
            has_finish = true;
        }


    uint8_t header[16];
    if (ReadHeader(file_name, 16, header, has_start))
    {
        unsigned int offset = (has_start)?5:4;
        unsigned int delta = static_cast<unsigned int>(header[0])*256 + header[1];
        unsigned int len = static_cast<unsigned int>(header[2])*256 + header[3] - delta;

        RAM * m = find_ram(e);

        uint8_t * buffer = m->get_buffer();
        unsigned int page_size = m->get_size();

        QFile file(file_name);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(file_name));
            return;
        }

        // Reading the main data
        file.skip(offset);
        QByteArray data = file.read(len);
        if (data.size() < len) {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("File is smaller than expected!"));
            return;
        }

        // Checking the finalization bytes if expected
        if (has_finish){
            uint8_t final;
            do {
                int c = file.read(reinterpret_cast<char*>(&final), 1);
                if (c==0) {
                    QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Unable to find an expected finalization byte 0xE6!"));
                    return;
                }
            } while(final==0);
            if (final != 0xE6) {
                QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Unable to find an expected finalization byte 0xE6!"));
                return;
            }
        }

        // Getting and checking a CRC
        uint16_t expected_crc;
        int c = file.read(reinterpret_cast<char*>(&expected_crc), 2);
        if (c < 2) {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Error reading CRC bytes!"));
            return;
        }
        expected_crc = ((expected_crc & 0xFF) << 8) + (expected_crc >> 8);
        uint16_t crc = 0;
        for (int i = 0; i < len; i++) {
            uint8_t b = static_cast<uint8_t>(data[i]);
            crc += (b << 8) + b;
        }

        if (crc != expected_crc) {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("The file was loaded, but its checksum is incorrect!"));
        }

        memcpy(&(buffer[delta]), data.constData(), MIN(len,page_size-delta));
        file.close();
    }
}

void load_bin(Emulator * e, QString file_name)
{
    RAM * m = find_ram(e);

    uint8_t * buffer = m->get_buffer();
    unsigned int page_size = m->get_size();

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(file_name));
        return;
    }

    QByteArray data = file.readAll();
    memcpy(buffer, data.constData(), MIN(file.size(),page_size));
    file.close();
}

void load_hex(Emulator * e, QString file_name)
{
    RAM * m = find_ram(e);
    if (m != nullptr)
    {
        uint8_t * buffer = m->get_buffer();

        QFile file(file_name);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading HEX file %1").arg(file_name));
            return;
        }

        QTextStream in(&file);
        while (!in.atEnd())
        {
            QString line = in.readLine();
            unsigned int len = parse_numeric_value("$" + line.mid(1, 2));
            unsigned int addr = parse_numeric_value("$" + line.mid(3, 4));
            unsigned int type = parse_numeric_value("$" + line.mid(7, 2));
            //qDebug() << Qt::hex << addr << Qt::hex << len;
            if (type == 0)
            {
                for (unsigned int j=0; j< len; j++)
                    buffer[addr+j] = parse_numeric_value("$" + line.mid(9+j*2, 2));
            }
        }
        file.close();
    }
}

int get_ramdisk_position(uint8_t * buffer, unsigned int top)
{
    unsigned int address = 0;
    while (address < top)
    {
        if (buffer[address] == 0xFF)
            return address;
        else {
            uint16_t l = buffer[address+10] + (buffer[address+11] << 8);
            address += l + 16;
        }
    }
    return -1;
}

void load_rko(Emulator * e, QString file_name)
{
    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();
    bool has_preamble = ext=="rko";

    bool add_to_disk;
    ROM * bios = dynamic_cast<ROM*>(e->dm->get_device_by_name("bios", false));
    if (bios != nullptr){
        uint16_t crc = CRC16(bios->get_buffer(), bios->get_size());
        add_to_disk = crc != 0xA85E; // Orion-128 M1
    } else
        add_to_disk = true;

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading %1").arg(file_name));
        return;
    }

    int c;
    uint16_t preamble_start = 0;
    uint16_t preamble_size = 0;
    uint16_t file_offset = 0;
    uint16_t read_length = 0;

    if (has_preamble)
    {
        uint8_t preamble_name[8];
        c = file.read(reinterpret_cast<char*>(&preamble_name), sizeof(preamble_name));
        if (c < sizeof(preamble_name)) {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
            return;
        }
        file_offset += c;
        uint8_t b;
        do {
            c = file.read(reinterpret_cast<char*>(&b), 1);
            file_offset += c;
            if (c==0) {
                QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
                return;
            }
        } while(b==0);
        if (b != 0xE6) {
            QMessageBox::warning(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
            return;
        }
        uint8_t preamble_data[4];
        c = file.read(reinterpret_cast<char*>(&preamble_data), sizeof(preamble_data));
        if (c < sizeof(preamble_data)) {
            QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading preamble data!"));
            return;
        }
        file_offset += c;
        preamble_start = (preamble_data[0] << 8) + preamble_data[1];
        preamble_size = (preamble_data[2] << 8) + preamble_data[3];

        read_length = preamble_size + 16;
        qDebug() << "read_length (preamble) " << Qt::hex << read_length;
    }

    uint8_t header_name[8];
    c = file.read(reinterpret_cast<char*>(&header_name), sizeof(header_name));
    if (c < sizeof(header_name)) {
        QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Error reading header data!"));
        return;
    }
    uint8_t header_data[8];
    c = file.read(reinterpret_cast<char*>(&header_data), sizeof(header_data));
    uint16_t header_start = (header_data[1] << 8) + header_data[0];
    uint16_t header_size = (header_data[3] << 8) + header_data[2];

    read_length = header_size + 16;

    uint8_t * buffer;
    unsigned int page_size;
    int address;
    RAM * m;
    if (add_to_disk)
    {
        m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram1", false));
        buffer = m->get_buffer();
        page_size = 48*1024;
        address = get_ramdisk_position(buffer, page_size);
        if (address < 0 || address + read_length > page_size)
        {
            m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram2", false));
            buffer = m->get_buffer();
            page_size = 60*1024;
            address = get_ramdisk_position(buffer, page_size);
            if (address < 0 || address + read_length > page_size)
            {
                m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram3", false));
                buffer = m->get_buffer();
                address = get_ramdisk_position(buffer, page_size);
                if (address < 0 || address + read_length > page_size)
                {
                    QMessageBox::critical(0, Emulator::tr("Error"), Emulator::tr("Can't load the file: all ramdisks are full!"));
                    return;
                }
            }
        }
    } else {
        // We will load to a memory
        RAM * m = find_ram(e);
        buffer = m->get_buffer();
        page_size = m->get_size();
        file_offset += 16;
        read_length -= 16;
        address = header_start;
    }

    qDebug() << "file_offset" << Qt::hex << file_offset;
    qDebug() << "read_length" << Qt::hex << read_length;
    qDebug() << "preamble_start" << Qt::hex << preamble_start;
    qDebug() << "preamble_size" << Qt::hex << preamble_size;
    qDebug() << "header_start" << Qt::hex << header_start;
    qDebug() << "header_size" << Qt::hex << header_size;

    file.seek(file_offset);

    QByteArray data = file.read(read_length);
    memcpy(&(buffer[address]), data.constData(), MIN(read_length,page_size-address));

    if (add_to_disk)
        buffer[address+read_length] = 0xFF;

    file.close();

}

void HandleExternalFile(Emulator * e, QString file_name)
{
    QStringList rk_files;
    rk_files << "rk" << "rkr" << "rkm" << "rka" << "gam";

    QStringList rko_files;
    rko_files << "rko" << "ord" << "bru";

    QStringList bin_files;
    bin_files << "cim" << "bin";

    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();

    if (ext == "hex") load_hex(e, file_name);
    else
    if (rk_files.contains(ext)) load_rk(e, file_name);
    else
    if (rko_files.contains(ext)) load_rko(e, file_name);
    else
    if (bin_files.contains(ext)) load_bin(e, file_name);
    else
        QMessageBox::warning(NULL, Emulator::tr("Error"), Emulator::tr("Unknown file type"));
}
