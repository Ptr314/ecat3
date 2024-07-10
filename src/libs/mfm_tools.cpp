#include <QFileInfo>
#include <QMessageBox>
#include "libs/mfm_tools.h"

const uint8_t gcr62_encode_table[64] =
    {
        0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,
        0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
        0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,
        0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
        0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,
        0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
        0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6,
        0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};

static const uint8_t agat_sector_translate[]={
    0x00,0x0D,0x0B,0x09,0x07,0x05,0x03,0x01,0x0E,0x0C,0x0A,0x08,0x06,0x04,0x02,0x0F
};

// Idea: https://tulip-house.ddo.jp/digital/SDISK2/english.html (dsk2nic.cpp)
static const unsigned char FlipBit1[4] = { 0, 2,  1,  3  };
static const unsigned char FlipBit2[4] = { 0, 8,  4,  12 };
static const unsigned char FlipBit3[4] = { 0, 32, 16, 48 };

#define GAP0    48
#define GAP1    6
#define GAP2    27

uint8_t * load_image(QString file_name, int image_size)
{

    QFileInfo fi(file_name);
    unsigned int file_size = fi.size();
    if (file_size == image_size)
    {
        QFile file(file_name);
        if (file.open(QIODevice::ReadOnly)){
            uint8_t * image = new uint8_t[image_size];

            QByteArray data = file.readAll();
            memcpy(image, data.constData(), file_size);
            file.close();

            return image;
        } else {
            QMessageBox::critical(0, QObject::tr("Error"), QObject::tr("Error opening file '%1'").arg(file_name));
        }
    } else {
        QMessageBox::critical(0, QObject::tr("Error"), QObject::tr("Incorrect disk image size for '%1'").arg(file_name));
    }
    return nullptr;
}

QByteArray code44(const uint8_t buffer[], const int len)
{
    QByteArray result;
    for (int i=0; i<len; i++) {
        result.append(static_cast<char>( (buffer[i] >> 1) | 0xaa));
        result.append(static_cast<char>(  buffer[i]       | 0xaa));
    }

    return result;
}

void encode_gcr62(const uint8_t data_in[], uint8_t * data_out)
{

    // First 86 bytes are combined lower 2 bits of input data
    for (int i = 0; i < 86; i++) {
        data_out[i] = FlipBit1[data_in[i]&3] | FlipBit2[data_in[i+86]&3] | FlipBit3[data_in[(i+172) & 0xFF]&3];
                                                                                                    // ^^ 2 extra bytes are wrapped to the beginning
    }

    // Next 256 bytes are upper 6 bits
    for (int i = 0; i < 256; i++) {
        data_out[i+86] = data_in[i] >> 2;
    }

    // Then, encoding 6 bits to 8 bits using a table and calculating a crc
    uint8_t crc = 0;
    for (int i = 0; i < 342; i++) {
        uint8_t v = data_out[i];
        data_out[i] = gcr62_encode_table[v ^ crc];
        crc = v;
    }

    // And finally adding a crc byte
    data_out[342] = gcr62_encode_table[crc];
}


uint8_t * generate_mfm_agat_140(QString file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[])
{
    int track_len = 6400;
    sides = 1;
    tracks = 35;
    int sectors = 16;
    int sector_size = 256;
    disk_size = tracks * track_len;
    int image_size = tracks * sectors * sector_size;
    uint8_t * image = load_image(file_name, image_size);

    uint8_t * buffer = new uint8_t[disk_size];
    uint8_t * out = buffer;

    char gap_bytes[256];
    memset(&gap_bytes, 0xFF, sizeof(gap_bytes));

    uint8_t encoded_sector[344];

    for (uint8_t track = 0; track < tracks; track++){
        // GAP 0
        memcpy(out, &gap_bytes, GAP0); out += GAP0;
        for (uint8_t sector = 0; sector < sectors; sector++) {
            // Prologue
            memcpy(out, QByteArray("\xD5\xAA\x96").constData(), 3); out += 3;
            // Address
            uint8_t volume = 0xFE;
            uint8_t sector_t = agat_sector_translate[sector];
            uint8_t address_field[4] = {volume, track, sector_t, static_cast<uint8_t>(volume ^ track ^ sector_t)};
            memcpy(out, code44(address_field, 4).constData(), 8); out += 8;
            // Epilogue
            memcpy(out, QByteArray("\xDE\xAA\xEB").constData(), 3); out += 3;
            // GAP 1
            memcpy(out, &gap_bytes, GAP1); out += GAP1;
            // Data field
            // Prologue
            memcpy(out, QByteArray("\xD5\xAA\xAD").constData(), 3); out += 3;
            uint8_t * data = &image[track * sectors * sector_size + sector * sector_size];
            encode_gcr62(data, encoded_sector);
            memcpy(out, &encoded_sector, 343); out += 343;
            // Epilogue
            memcpy(out, QByteArray("\xDE\xAA\xEB").constData(), 3); out += 3;
            // GAP 2
            memcpy(out, &gap_bytes, GAP2); out += GAP2;
        }
        // GAP 3
        int gap3 = track_len - (GAP0 + sectors*(
                                                  3 +                                       // Address prologue
                                                  8 +                                       // Address
                                                  3 +                                       // Address epilogue
                                                  GAP1 +
                                                  3 +                                       // Data prologue
                                                  343 +                                     // Data
                                                  3 +                                       // Data epilogue
                                                  GAP2
                                        )
                                );

        memcpy(out, &gap_bytes, gap3); out += gap3;
    }

    delete [] image;

    for (int i=0; i < tracks; i++) {
        track_indexes[i].track_number = i;
        track_indexes[i].side_number = 0;
        track_indexes[i].mfmtracksize = track_len;
        track_indexes[i].mfmtrackoffset = i * track_len;
    }

    return buffer;
}

uint8_t * generate_mfm_agat_840(QString file_name, int & sides, int & tracks, int & disk_size, HXC_MFM_TRACK_INFO track_indexes[])
{
    //TODO: implement
    int track_len =;
    sides = 2;
    tracks = 80;
    int sectors = 21;
    int sector_size = 256;
    disk_size = tracks * track_len;
    int image_size = tracks * sectors * sector_size;
    uint8_t * image = load_image(file_name, image_size);

    uint8_t * buffer = new uint8_t[disk_size];
    uint8_t * out = buffer;

    char gap_bytes[256];
    memset(&gap_bytes, 0xFF, sizeof(gap_bytes));

    uint8_t encoded_sector[344];
}

void save_mfm_file(QString file_name, int sides, int tracks, int track_size, HXC_MFM_TRACK_INFO track_indexes[], uint8_t * data)
{
    HXC_MFM_HEADER      hxc_mfm_header;
    HXC_MFM_TRACK_INFO  hxc_mfm_track_info;

    QFile file(file_name);
    if (file.open(QIODevice::WriteOnly)){
        //header
        strcpy((char*)&hxc_mfm_header.headername, "HXCMFM");
        hxc_mfm_header.number_of_track = tracks;
        hxc_mfm_header.number_of_side = sides;
        int track_offset_mult = (hxc_mfm_header.number_of_side==2)?2:1;
        hxc_mfm_header.floppyRPM = 300;
        hxc_mfm_header.floppyBitRate = 250;
        hxc_mfm_header.floppyiftype = 0;
        hxc_mfm_header.mfmtracklistoffset = sizeof(HXC_MFM_HEADER);

        file.write((char*)(&hxc_mfm_header), sizeof(HXC_MFM_HEADER));

        //track list
        for (uint8_t track = 0; track < tracks; track++){
            for (uint8_t head = 0; head < sides; head++){
                hxc_mfm_track_info.track_number = track;
                hxc_mfm_track_info.side_number = head;
                hxc_mfm_track_info.mfmtracksize = track_size;
                hxc_mfm_track_info.mfmtrackoffset = 0x800 + (track*track_offset_mult + head)*track_size;
                file.write((char*)(&hxc_mfm_track_info), sizeof(HXC_MFM_TRACK_INFO));
            }
        }

        uint8_t fill = 0;
        int fill_size = 0x800 - sizeof(HXC_MFM_HEADER) - sizeof(HXC_MFM_TRACK_INFO) * tracks * sides;
        for (int i=0; i < fill_size; i++){
            file.write((char*)(&fill), 1);
        }

        file.write((char*)data, sides*tracks*track_size);

        file.close();
    }

}
