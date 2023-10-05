#ifndef FILES_H
#define FILES_H

#include <QFileInfo>
#include <QMessageBox>

#include "emulator/emulator.h"

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

void load_rk(Emulator * e, QString file_name)
{
    uint8_t header[16];
    ReadHeader(file_name, 16, header);
    unsigned int offset = 4;
    unsigned int delta = static_cast<unsigned int>(header[0])*256 + header[1];
    unsigned int len = static_cast<unsigned int>(header[2])*256 + header[3];

    RAM * m;
    m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram", false));
    if (m==nullptr) m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram0"));

    uint8_t * buffer = m->get_buffer();
    unsigned int page_size = m->get_size();

    QFile file(file_name);
    if (file.open(QIODevice::ReadOnly)){
        file.skip(offset);
        QByteArray data = file.readAll();
        memcpy(&(buffer[delta]), data.constData(), MIN(len,page_size-delta));
        file.close();
    }
}

void HandleExternalFile(Emulator * e, QString file_name)
{
    QStringList rk_files;
    rk_files << "rk" << "rkr" << "rkm" << "rka";

    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();
    if (rk_files.contains(ext)) load_rk(e, file_name);
    else
        QMessageBox::warning(NULL, "Error", "Unknown file type");
}

#endif // FILES_H
