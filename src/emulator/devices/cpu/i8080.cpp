#include "i8080.h"

I8080::I8080(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    //TODO: Implement
}

unsigned int I8080::get_pc()
{

}

void I8080::execute()
{
    //TODO: Implement
}

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8080(im, cd);
}
