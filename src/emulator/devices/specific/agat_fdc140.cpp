#include "agat_fdc140.h"

Agat_FDC140::Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd)
{
    // TODO: implement
}

bool Agat_FDC140::get_busy()
{
    // TODO: implement
}

unsigned int Agat_FDC140::get_selected_drive()
{
    // TODO: implement
}

unsigned int Agat_FDC140::get_value(unsigned int address)
{
    // TODO: implement
}

void Agat_FDC140::set_value(unsigned int address, unsigned int value)
{
    // TODO: implement
}

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC140(im, cd);
}
