//#define Z_COMPILER Z_COMPILER_MINGW
//#include <Z80.h>

#include "i8080.h"

I8080::I8080(InterfaceManager *im, EmulatorConfigDevice *cd):
    CPU(im, cd)
{
    this->i_address = this->create_interface(16, "address", MODE_R, 1);
    this->i_data =    this->create_interface(8, "data", MODE_RW);
    this->i_nmi =     this->create_interface(1, "nmi", MODE_R);
    this->i_int =     this->create_interface(1, "int", MODE_R);
    this->i_inte =    this->create_interface(1, "inte", MODE_W);
    this->i_m1 =      this->create_interface(1, "m1", MODE_W);
}

unsigned int I8080::get_pc()
{

}

unsigned int I8080::execute()
{
    //TODO: Implement
    return 10;
}

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8080(im, cd);
}
