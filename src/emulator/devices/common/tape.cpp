#include "tape.h"

TapeRecorder::TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    //TODO: Implement

}

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);

}
