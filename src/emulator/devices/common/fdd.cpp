#include "fdd.h"

FDD::FDD(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{

}

ComputerDevice * create_FDD(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new FDD(im, cd);
}
