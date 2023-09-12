#include "speaker.h"

Speaker::Speaker(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)

{
    //TODO: Implement
}

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Speaker(im, cd);
}
