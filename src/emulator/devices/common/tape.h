#ifndef TAPERECORDER_H
#define TAPERECORDER_H

#include "emulator/core.h"

class TapeRecorder: public ComputerDevice
{
    //TODO: TapeRecorder: Implement
private:
    Interface * i_input;
    Interface * i_output;

public:
    TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd);
};

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // TAPERECORDER_H
