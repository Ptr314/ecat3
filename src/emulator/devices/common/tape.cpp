#include "tape.h"

TapeRecorder::TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    //TODO: Implement
    this->i_input = this->create_interface(1, "input", MODE_R);
    this->i_output = this->create_interface(1, "output", MODE_W);
}

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);
}
