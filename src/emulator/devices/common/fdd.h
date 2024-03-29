#ifndef FDD_H
#define FDD_H

#include "emulator/core.h"

#define FDD_STREAM_PLAIN    0
#define FDD_STREAM_MFM      1
#define FDD_STREAM_HEADERS  2

class FDD : public ComputerDevice
{
private:
    uint8_t * buffer;
    int sides;
    int tracks;
    int sectors;
    int sector_size;
    int disk_size;

    unsigned int selector;

    Interface * i_select;
    Interface * i_side;
    Interface * i_density;

    bool write_protect;
    bool loaded;


    int side;
    int track;
    int sector;
    int fbyte;

    unsigned int translate_address();
    void ConvertStreamFormat();

public:
    QString files;
    QString file_name;
    unsigned int stream_format;

    FDD(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~FDD();

    bool is_selected();
    bool is_protected();
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
