#include <QException>
#include <QFileInfo>

#include "fdd.h"
#include "emulator/utils.h"

FDD::FDD(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    loaded(false),
    stream_format(FDD_STREAM_PLAIN),
    buffer(nullptr)
{
    i_select = create_interface(2, "select", MODE_R);
    i_side = create_interface(1, "side", MODE_R);
    i_density = create_interface(1, "density", MODE_R);

    memset(&buffer, 0, sizeof(buffer));
}

FDD::~FDD()
{
    if (buffer != nullptr) delete [] buffer;
    ComputerDevice::~ComputerDevice();
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
        QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Incorrect fdd parameters for '%1'").arg(this->name));
        throw QException();
    }

    try {
        file_name = cd->get_parameter("image").value;
        if (!file_name.isEmpty())
        {
            if (QFile::exists(sd->software_path + file_name))
                load_image(sd->software_path + file_name);
            else if(QFile::exists(sd->system_path + file_name))
                load_image(sd->system_path + file_name);
            else
                QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Disk image file '%1' not found").arg(file_name));
        }
    } catch (QException &e) {
    }
}

void FDD::load_image(QString file_name)
{
    QFileInfo fi(file_name);
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
            loaded = true;
            this->file_name =fi.fileName();
        }
    } else {
        QMessageBox::critical(0, FDD::tr("Error"), FDD::tr("Incorrect disk image size for '%1'").arg(file_name));
        this->file_name = "";
    }
}

int FDD::SeekSector(int track, int sector)
{
    int result = -1;
    if (buffer != nullptr)
    {
        this->side = ~(i_side->value) & 1;
        this->track = track;
        this->sector = sector;
        fbyte = 0;
        if (sector > 0) {
            result = sector_size;
        } else {
            //TODO: FDD recall purpose of this code
            if (stream_format == FDD_STREAM_MFM) {
                result = 128;
            } else {
                im->dm->error(this, FDD::tr("This mode is not supported"));
            }
        }
    }
    return result;
}

unsigned int FDD::translate_address()
{
    return ((track*sides + side)*sectors + sector-1)*sector_size + fbyte;
}

uint8_t FDD::ReadNextByte()
{
    if (fbyte >= sector_size)
        im->dm->error(this, FDD::tr("Reading outside of a sector"));

    if (sector==0)
    {
        //GAP
        return 0xFF;
    } else {
        //DAta
        uint8_t result = buffer[translate_address()];
        fbyte++;
        return result;
    }
}

void FDD::WriteNextByte(uint8_t value)
{
    if (fbyte >= sector_size)
        im->dm->error(this, FDD::tr("Writing outside of a sector"));

    if (sector != 0)
    {
        buffer[translate_address()] = value;
        fbyte++;
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
    //TODO: FDD Implement
}

void FDD::ConvertStreamFormat()
{
    //TODO: FDD Implement
}

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new FDD(im, cd);
}
