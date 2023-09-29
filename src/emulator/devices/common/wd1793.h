#ifndef WD1793_H
#define WD1793_H

#include "emulator/core.h"

class WD1793 : public FDC
{
public:
    WD1793(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual bool get_busy() override;
    virtual unsigned int get_selected_drive() override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value) override;
};

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // WD1793_H
