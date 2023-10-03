#include "tape.h"

TapeRecorder::TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    //TODO: TapeRecorder: Implement
    i_input =  create_interface(1, "input", MODE_R);
    i_output = create_interface(1, "output", MODE_W);
}

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);
}
