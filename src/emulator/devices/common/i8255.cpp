#include "i8255.h"

I8255::I8255(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    this->i_address = this->create_interface(2, "address", MODE_R);
    this->i_data = this->create_interface(8, "data", MODE_R);
    this->i_port_a = this->create_interface(8, "A", MODE_R, PORT_A);
    this->i_port_b = this->create_interface(8, "B", MODE_R, PORT_B);
    this->i_port_ch = this->create_interface(4, "CH", MODE_R, PORT_CH);
    this->i_port_cl = this->create_interface(4, "CL", MODE_R, PORT_CL);
}

void I8255::reset(bool cold)
{
    registers[3] = 0;
    i_port_a->set_mode(MODE_R);
    i_port_b->set_mode(MODE_R);
    i_port_ch->set_mode(MODE_R);
    i_port_cl->set_mode(MODE_R);
}

unsigned int I8255::get_value(unsigned int address)
{
    return this->registers[address & 0b11];
}

void I8255::set_value(unsigned int address, unsigned int value)
{
    unsigned int n = address & 0b11;
    switch (n) {
    case 0:
        if ((this->registers[3] & 0x60) == 0)
        {
            if ((this->registers[3] & 0x10) == 0)
            {
                this->registers[n] = (uint8_t)value;
                this->i_port_a->change(value);
            }
        } else {
            this->im->dm->error(this, I8255::tr("i8255:A is in an unsupported mode"));
        }
        break;
    case 1:
        if ((this->registers[3] & 4) == 0)
        {
            if ((this->registers[3] & 2) == 0)
            {
                this->registers[n] = (uint8_t)value;
                this->i_port_b->change(value);
            }
        } else {
            this->im->dm->error(this, I8255::tr("i8255:B is in an unsupported mode"));
        }
        break;
    case 2:
        if ((this->registers[3] & 8) == 0)
        {
            this->registers[n] &= 0x0F;
            this->registers[n] |= (uint8_t)value & 0xF0;
            this->i_port_ch->change(this->registers[n] >> 4);
        }
        if ((this->registers[3] & 1) == 0)
        {
            this->registers[n] &= 0xF0;
            this->registers[n] |= (uint8_t)value & 0x0F;
            this->i_port_ch->change(this->registers[n]);
        }
        break;
    default: //3 - control register
        if ((value & 0x80) != 0)
        {
            //Set mode
            this->registers[n] = (uint8_t)value;
            this->i_port_a-> set_mode( ((value & 0x10) == 0)?MODE_W:MODE_R );
            this->i_port_b-> set_mode( ((value & 0x02) == 0)?MODE_W:MODE_R );
            this->i_port_ch->set_mode( ((value & 0x08) == 0)?MODE_W:MODE_R );
            this->i_port_cl->set_mode( ((value & 0x01) == 0)?MODE_W:MODE_R );
        } else {
            //Bitwise operations on C
            unsigned int bn = value >> 1;
            unsigned int bv = (value & 1) << bn;
            unsigned int bm = 1 << bn;
            unsigned int v = (this->registers[2] & !bm) | bv;
            this->set_value(2, v);
        }
        break;
    }
}

void I8255::interface_callback(unsigned int callback_id, unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    switch (callback_id) {
    case PORT_A:
        if ((this->registers[3] & 0x60) == 0) {
            if ((this->registers[3] & 0x10) != 0) this->registers[0] = (uint8_t)new_value;
        } else
            this->im->dm->error(this, I8255::tr("i8255:A is in an unsupported mode"));
        break;
    case PORT_B:
        if ((this->registers[3] & 0x04) == 0) {
            if ((this->registers[3] & 0x02) != 0) this->registers[1] = (uint8_t)new_value;
        } else
            this->im->dm->error(this, I8255::tr("i8255:B is in an unsupported mode"));
        break;
    case PORT_CH:
        if ((this->registers[3] & 0x08) != 0)
        {
            this->registers[2] &= 0x0F;
            this->registers[2] |= (uint8_t)new_value & 0xF0;
        }
        break;
    case PORT_CL:
        if ((this->registers[3] & 0x01) != 0)
        {
            this->registers[2] &= 0xF0;
            this->registers[2] |= (uint8_t)new_value & 0x0F;
        }
        break;
    default:
        this->im->dm->error(this, I8255::tr("i8255:unknown interface called"));
        break;
    }
}

ComputerDevice * create_i8255(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8255(im, cd);
}
