#ifndef AGAT_FDC840_H
#define AGAT_FDC840_H

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/i8255.h"

#define AGAT_840_TRACK_COUNT 80

class Agat_FDC840 : public FDC
{
protected:
    Interface * i_select;
    Interface * i_side;
    int current_track[2];
    int selected_drive;
    int drives_count;
    FDD * drives[2];
    bool motor_on;
    bool write_mode;
    int step_dir;
    int side;
    bool sector_sync;
    bool write_sync;
    bool data_ready;

    I8255 dd14;
    I8255 dd15;

    void update_status();
    void update_state();

    void read_next_byte();
    void write_next_byte();

#ifdef LOG_FDD
    RAM * ram0;
#endif

public:
    Agat_FDC840(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd) override;

    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
    virtual void reset(bool cold) override;
    virtual void clock(unsigned int counter) override;
};

ComputerDevice * create_agat_fdc840(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // AGAT_FDC840_H
