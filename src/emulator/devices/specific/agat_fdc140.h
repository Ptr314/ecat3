#ifndef AGAT_FDC140_H
#define AGAT_FDC140_H

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

class Agat_FDC140 : public FDC
{
protected:
    Interface * i_select;
    int prev_phase;
    int current_phase;
    //uint8_t data;

    int current_track[2];
    int selected_drive;
    int drives_count;
    FDD * drives[2];

    void phase_on(int n);
    void phase_off(int n);
    void select_drive(int n);

public:
    Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void load_config(SystemData *sd) override;

    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
};

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // AGAT_FDC140_H
