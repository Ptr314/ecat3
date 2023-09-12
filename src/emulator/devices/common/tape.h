#ifndef TAPERECORDER_H
#define TAPERECORDER_H

#include "emulator/core.h"

class TapeRecorder: public ComputerDevice
{
public:
    TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd);
};

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // TAPERECORDER_H
