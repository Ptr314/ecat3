#ifndef FILES_H
#define FILES_H

#include <QFileInfo>
#include <QMessageBox>

#include "emulator/emulator.h"
#include "emulator/utils.h"

#define MIN(a, b)   ((a<b)?a:b)

void ReadHeader(QString file_name, unsigned int bytes, uint8_t * buffer)
{
    QFile file(file_name);
    if (file.open(QIODevice::ReadOnly)){
        QByteArray data = file.read(bytes);
        memcpy(buffer, data.constData(), bytes);
        file.close();
    }
}

RAM * find_ram(Emulator * e)
{
    RAM * m;
    m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram", false));
    if (m==nullptr) m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram0"));

    if (m==nullptr)
        QMessageBox::warning(0, QMessageBox::tr("Error"), QMessageBox::tr("Unable to find a RAM page to store data"));

    return m;

}

void load_rk(Emulator * e, QString file_name)
{
    uint8_t header[16];
    ReadHeader(file_name, 16, header);
    unsigned int offset = 4;
    unsigned int delta = static_cast<unsigned int>(header[0])*256 + header[1];
    unsigned int len = static_cast<unsigned int>(header[2])*256 + header[3];

    RAM * m = find_ram(e);

    uint8_t * buffer = m->get_buffer();
    unsigned int page_size = m->get_size();

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("Error reading %1").arg(file_name));
        return;
    }

    file.skip(offset);
    QByteArray data = file.readAll();
    memcpy(&(buffer[delta]), data.constData(), MIN(len,page_size-delta));
    file.close();
}

void load_bin(Emulator * e, QString file_name)
{
    RAM * m = find_ram(e);

    uint8_t * buffer = m->get_buffer();
    unsigned int page_size = m->get_size();

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("Error reading %1").arg(file_name));
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
            QMessageBox::critical(0, ROM::tr("Error"), ROM::tr("Error reading HEX file %1").arg(file_name));
            return;
        }

        QTextStream in(&file);
        while (!in.atEnd())
        {
            QString line = in.readLine();
            unsigned int len = parse_numeric_value("$" + line.mid(1, 2));
            unsigned int addr = parse_numeric_value("$" + line.mid(3, 4));
            unsigned int type = parse_numeric_value("$" + line.mid(7, 2));
            qDebug() << Qt::hex << addr << Qt::hex << len;
            if (type == 0)
            {
                for (unsigned int j=0; j< len; j++)
                    buffer[addr+j] = parse_numeric_value("$" + line.mid(9+j*2, 2));
            }
        }
        file.close();
    }
}

void HandleExternalFile(Emulator * e, QString file_name)
{
    QStringList rk_files;
    rk_files << "rk" << "rkr" << "rkm" << "rka";

    QStringList bin_files;
    bin_files << "cim" << "bin";

    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();

    if (ext == "hex") load_hex(e, file_name);
    else
    if (rk_files.contains(ext)) load_rk(e, file_name);
    else
    if (bin_files.contains(ext)) load_bin(e, file_name);
    else
        QMessageBox::warning(NULL, "Error", "Unknown file type");
}

#endif // FILES_H
