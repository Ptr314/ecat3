#include "agat_fdc140.h"

// https://github.com/allender/apple2emu/blob/df9eff703dd70b7dbc3b817734daf95133437143/src/disk_image.cpp#L263

Agat_FDC140::Agat_FDC140(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd)
{
    // TODO: implement
    i_select = create_interface(2, "select", MODE_W);
}

bool Agat_FDC140::get_busy()
{
    // TODO: implement
    return false;
}

unsigned int Agat_FDC140::get_selected_drive()
{
    // TODO: implement
    return 0;
}

unsigned int Agat_FDC140::get_value(unsigned int address)
{
    // TODO: implement
}

void Agat_FDC140::set_value(unsigned int address, unsigned int value, bool force)
{
    // TODO: implement
}

ComputerDevice * create_agat_fdc140(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Agat_FDC140(im, cd);
}
