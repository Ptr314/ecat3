#ifndef FDD_H
#define FDD_H

#include "emulator/core.h"
#include "libs/mfm_formats.h"

#define FDD_STREAM_PLAIN    0
#define FDD_STREAM_MFM      1
#define FDD_STREAM_HEADERS  2

#define FDD_MODE_SECTORS        0
#define FDD_MODE_WHOLE_TRACK    1

class FDD : public ComputerDevice
{
private:
    uint8_t * buffer;
    int sides;
    int tracks;
    int sectors;
    int sector_size;
    int disk_size;

    // MFM tracks info
    HXC_MFM_TRACK_INFO track_indexes[100];
    int physical_track_len;

    unsigned int selector;

    Interface * i_select;
    Interface * i_side;
    Interface * i_density;

    bool write_protect;
    bool loaded;


    int side;
    int track;
    int sector;
    int position;

    int fdd_mode;
    int track_mode;

    unsigned int translate_address();
    void ConvertStreamFormat();

public:
    QString files;
    QString files_save;
    QString file_name;
    unsigned int stream_format;

    FDD(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~FDD();

    bool is_selected();
    bool is_protected();
    bool is_index();
    bool is_track_00();

    int get_sector_size();
    int get_loaded();
    int SeekSector(int track, int sector);
    uint8_t  ReadNextByte();
    void WriteNextByte(uint8_t value);
    virtual void load_config(SystemData *sd) override;
    void load_image(QString file_name);
    void save_image(QString file_name);
    void unload();
    void change_protection();
};

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // FDD_H
