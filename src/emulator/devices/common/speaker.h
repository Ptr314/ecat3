#ifndef SPEAKER_H
#define SPEAKER_H

#include "emulator/core.h"

//TODO: Implement
class Speaker: public ComputerDevice
{
public:
    Speaker(InterfaceManager *im, EmulatorConfigDevice *cd);
};

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SPEAKER_H
