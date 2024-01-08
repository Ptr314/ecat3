#ifndef AGAT_FDC140_H
#define AGAT_FDC140_H

#include "emulator/core.h"
#include "emulator/devices/common/fdd.h"

class Agat_FDC140 : public FDC
{
private:
    Interface * i_select;

public:
    Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value) override;
};

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // AGAT_FDC140_H
