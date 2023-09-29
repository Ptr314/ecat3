#ifndef FDD_H
#define FDD_H

#include "emulator/core.h"

class FDD : public ComputerDevice
{
public:
    FDD(InterfaceManager *im, EmulatorConfigDevice *cd);
};

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // FDD_H
