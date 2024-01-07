#ifndef AGATDISPLAY_H
#define AGATDISPLAY_H

#include "emulator/core.h"

class AgatDisplay : public GenericDisplay
{
protected:
    virtual void render_all(bool force_render) override;
public:
    AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
};

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // AGATDISPLAY_H
