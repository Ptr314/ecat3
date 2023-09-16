#include "emulator/utils.h"
#include "scankeyboard.h"

ScanKeyboard::ScanKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    Keyboard(im, cd)
{
    //TODO: ScanKeyboard: Impelement
    i_scan =   this->create_interface(8, "scan", MODE_R, SCAN_CALLBACK);
    i_output = this->create_interface(8, "output", MODE_W);
    i_shift =  this->create_interface(1, "shift", MODE_W);
    i_ctrl =   this->create_interface(1, "ctrl", MODE_W);
    i_ruslat = this->create_interface(1, "ruslat", MODE_W);

    calculate_out();
}

void ScanKeyboard::key_down(unsigned int key)
{
    //TODO: ScanKeyboard: Impelement
}

void ScanKeyboard::key_up(unsigned int key)
{
    //TODO: ScanKeyboard: Impelement
}

void ScanKeyboard::calculate_out()
{
    //TODO: ScanKeyboard: Impelement
    i_output->change(_FFFF);
}


ComputerDevice * create_scankeyboard(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new ScanKeyboard(im, cd);
}
