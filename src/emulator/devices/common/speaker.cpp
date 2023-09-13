#include "speaker.h"

Speaker::Speaker(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)

{
    //TODO: Implement
    this->i_input = this->create_interface(1, "input", MODE_R);
}

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Speaker(im, cd);
}
