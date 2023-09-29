#include "wd1793.h"

WD1793::WD1793(InterfaceManager *im, EmulatorConfigDevice *cd):
    FDC(im, cd)
{
//    this->i_address = this->create_interface(2, "address", MODE_R);
//    this->i_data = this->create_interface(8, "data", MODE_R);
//    this->i_port_a = this->create_interface(8, "A", MODE_R, PORT_A);
//    this->i_port_b = this->create_interface(8, "B", MODE_R, PORT_B);
//    this->i_port_ch = this->create_interface(4, "CH", MODE_R, PORT_CH);
//    this->i_port_cl = this->create_interface(4, "CL", MODE_R, PORT_CL);
}

bool WD1793::get_busy()
{
    //TODO: WD1793 Implement
}

unsigned int WD1793::get_selected_drive()
{
    //TODO: WD1793 Implement
}

unsigned int WD1793::get_value(unsigned int address)
{
    //TODO: WD1793 Implement
}

void WD1793::set_value(unsigned int address, unsigned int value)
{
    //TODO: WD1793 Implement
}

ComputerDevice * create_WD1793(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new WD1793(im, cd);
}
