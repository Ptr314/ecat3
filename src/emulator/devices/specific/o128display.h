#ifndef O128DISPLAY_H
#define O128DISPLAY_H

#include "emulator/core.h"

class O128Display: public Display
{
public:
    O128Display(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void get_screen(bool required);
    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy);
};

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // O128DISPLAY_H
