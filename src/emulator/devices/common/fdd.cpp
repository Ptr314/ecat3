#include <QException>
#include <QFileInfo>

#include "fdd.h"
#include "emulator/utils.h"
#include "libs/mfm_tools.h"

#define FDD_MODE_LOGICAL    0
#define FDD_MODE_AGAT_140   1
#define FDD_MODE_AGAT_840   2

FDD::FDD(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    loaded(false),
    stream_format(FDD_STREAM_PLAIN),
    buffer(nullptr),
    side(0),
    track(0),
    fdd_mode(FDD_MODE_LOGICAL)
{
    i_select = create_interface(2, "select", MODE_R);
    i_side = create_interface(1, "side", MODE_R);
    i_density = create_interface(1, "density", MODE_R);

    //memset(&buffer, 0, sizeof(buffer));
}

FDD::~FDD()
{
    if (buffer != nullptr) delete [] buffer;
}

void FDD::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);
    try {
        sides = parse_numeric_value(cd->get_parameter("sides").value);
        tracks = parse_numeric_value(cd->get_parameter("tracks").value);
        sectors = parse_numeric_value(cd->get_parameter("sectors").value);
        sector_size = parse_numeric_value(cd->get_parameter("sector_size").value);
        selector = parse_numeric_value(cd->get_parameter("selector_value").value);
        files = cd->get_parameter("files").value;
        disk_size = sides*tracks*sectors*sector_size;
        write_protect = false;
    } catch (QException &e) {
        QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Incorrect fdd parameters for '%1'").arg(name));
        throw QException();
    }

    files_save = cd->get_parameter("files_save", false).value;
    if (files_save.isEmpty()) files_save = files;

    QString s = read_confg_value(cd, "mode", false, "logical");
    if (s == "logical")
        fdd_mode = FDD_MODE_LOGICAL;
    else if (s == "mfm_agat_140")
        fdd_mode = FDD_MODE_AGAT_140;
    else if (s == "mfm_agat_840")
        fdd_mode = FDD_MODE_AGAT_840;
    else
        QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Unknown fdd mode '%1'").arg(s));

    try {
        file_name = find_file_location(sd->system_path, sd->software_path, cd->get_parameter("image").value);
        if (!file_name.isEmpty())
            load_image(file_name);
        else
            QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Disk image file '%1' not found").arg(file_name));
    } catch (QException &e) {
    }
}

void FDD::load_image(QString file_name)
{
    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();

    if (ext == "mfm" || ext == "hfe") {
        if (fdd_mode == FDD_MODE_LOGICAL) {
            QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("FDD device is working in a logical mode, no physical formats are supported"));
            return;
        }
        HXC_MFM_HEADER hxc_header;
        QFile file(file_name);
        if (file.open(QIODevice::ReadOnly)){
            QByteArray header_data = file.read(sizeof(HXC_MFM_HEADER));
            memcpy(&hxc_header, header_data.constData(), sizeof(HXC_MFM_HEADER));
            if (QString::fromLatin1(reinterpret_cast<char*>(hxc_header.headername), 6) == "HXCMFM") {
                sides = hxc_header.number_of_side;
                tracks = hxc_header.number_of_track;

                file.seek(hxc_header.mfmtracklistoffset);
                HXC_MFM_TRACK_INFO track_info;
                QByteArray track_info_data = file.read(sizeof(HXC_MFM_TRACK_INFO)*tracks);
                memcpy(&track_indexes, track_info_data.constData(), sizeof(HXC_MFM_TRACK_INFO)*tracks);

                disk_size = track_indexes[0].mfmtracksize * tracks;
                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];

                int data_begin = track_indexes[0].mfmtrackoffset;
                for (int i=0; i < tracks; i++) track_indexes[i].mfmtrackoffset -= data_begin;

                file.seek(data_begin);
                QByteArray disk_data = file.read(disk_size);
                memcpy(buffer, disk_data.constData(), disk_size);

                track_mode = FDD_MODE_WHOLE_TRACK;
                position = 0;
                loaded = true;
                this->file_name =fi.fileName();
            } else {
                QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Unrecognized MFM format"));
            }
            file.close();
        } else {
            QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Error opening file '%1'").arg(file_name));
        }
    } else
    if (ext == "nib") {
        if (fdd_mode == FDD_MODE_LOGICAL) {
            QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("FDD device is working in a logical mode, no physical formats are supported"));
            return;
        }
        QFile file(file_name);
        if (file.size() == 232960) {
            if (file.open(QIODevice::ReadOnly)){
                sides = 1;
                tracks = 35;
                disk_size = 232960;
                physical_track_len = disk_size / tracks;

                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];

                for (int i=0; i < tracks; i++) {
                    track_indexes[i].track_number = i;
                    track_indexes[i].side_number = 0;
                    track_indexes[i].mfmtracksize = physical_track_len;
                    track_indexes[i].mfmtrackoffset = i * physical_track_len;
                }

                QByteArray disk_data = file.read(disk_size);
                memcpy(buffer, disk_data.constData(), disk_size);

                track_mode = FDD_MODE_WHOLE_TRACK;
                position = 0;
                loaded = true;
                this->file_name =fi.fileName();
            } else {
                QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Error opening file '%1'").arg(file_name));
            }
        } else {
            QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("File '%1' is in unknown format").arg(file_name));
        }
    } else {
        if (fdd_mode == FDD_MODE_LOGICAL) {
            unsigned int file_size = fi.size();
            if (file_size == disk_size)
            {
                if (buffer != nullptr) delete [] buffer;
                buffer = new uint8_t[disk_size];

                QFile file(file_name);
                if (file.open(QIODevice::ReadOnly)){
                    QByteArray data = file.readAll();
                    memcpy(buffer, data.constData(), file_size);
                    file.close();

                    track_mode = FDD_MODE_SECTORS;
                    loaded = true;
                    this->file_name =fi.fileName();
                } else {
                    QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Error opening file '%1'").arg(file_name));
                }
            } else {
                QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Incorrect disk image size for '%1'").arg(file_name));
                this->file_name = "";
            }
        } else {
            // Convert to MFM
            if (buffer != nullptr) delete [] buffer;
            switch (fdd_mode) {
                case FDD_MODE_AGAT_140:
                    buffer = generate_mfm_agat_140(file_name, sides, tracks, disk_size, track_indexes);
                    break;
                default:
                    QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Expected conversion from DSK to MFM is not supported yet."));
                    break;
            }
            track_mode = FDD_MODE_WHOLE_TRACK;
            position = 0;
            loaded = true;
            this->file_name =fi.fileName();
        }
    }
}

int FDD::SeekSector(int track, int sector)
{
    int result = -1;
    if (buffer != nullptr)
    {
        if (sides > 1)
            this->side = ~(i_side->value) & 1;
        this->track = track;
        this->sector = sector;
        //qDebug() << "SEEK " << this->side << this->track << this->sector;
#ifdef LOG_FDD
        logs(QString("SEEK %1").arg(track));
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

uint8_t FDD::ReadNextByte()
{
    if (track_mode == FDD_MODE_SECTORS) {
        if (position >= sector_size)
            im->dm->error(this, FDD::tr("Reading outside of a sector"));

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
        if (position >= track_indexes[track*sides + side].mfmtracksize)
            position = 0;
#ifdef LOG_FDD
        //logs(QString("R %1 %2").arg(position).arg(result, 2, 16, QChar('0')));
#endif
        return result;
    }
}

void FDD::WriteNextByte(uint8_t value)
{
    if (track_mode == FDD_MODE_SECTORS) {
        if (position >= sector_size)
            im->dm->error(this, FDD::tr("Writing outside of a sector"));

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

bool FDD::is_selected()
{
    return (i_select->value & 0x03) == selector;
}

bool FDD::is_protected()
{
    return write_protect;
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

void FDD::change_protection()
{
    write_protect = !write_protect;
}

void FDD::save_image(QString file_name)
{
    if (loaded)
    {
        QFileInfo fi(file_name);
        QString ext = fi.suffix().toLower();

        if (ext == "dsk") {
            if (fdd_mode == FDD_MODE_LOGICAL) {
                QFile file(file_name);
                if (file.open(QIODevice::WriteOnly)){
                    file.write(reinterpret_cast<char*>(buffer), disk_size);
                    file.close();
                }
            } else {
                QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("FDD is working in a physical mode now, generating of DSK images is not supported yet."));
            }
        } else if (ext == "mfm") {
            if (fdd_mode == FDD_MODE_AGAT_140) {
                save_mfm_file(file_name, sides, tracks, track_indexes[0].mfmtracksize, track_indexes, buffer);
            } else {
                QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Saving images for this type of drive is not supported yet."));
            }

        }
    }
}

void FDD::ConvertStreamFormat()
{
    //TODO: FDD Implement
}

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new FDD(im, cd);
}
