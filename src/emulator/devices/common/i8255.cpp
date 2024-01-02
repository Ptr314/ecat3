#include "i8255.h"

#define PORT_A   1
#define PORT_B   2
#define PORT_CH  3
#define PORT_CL  4

I8255::I8255(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    i_address = create_interface(2, "address", MODE_R);
    i_data =    create_interface(8, "data", MODE_R);
    i_port_a =  create_interface(8, "A", MODE_R, PORT_A);
    i_port_b =  create_interface(8, "B", MODE_R, PORT_B);
    i_port_ch = create_interface(4, "CH", MODE_R, PORT_CH);
    i_port_cl = create_interface(4, "CL", MODE_R, PORT_CL);
}

void I8255::reset(bool cold)
{
    if (cold) memset(&registers, 0, sizeof(registers));
    registers[3] = 0;
    i_port_a->set_mode(MODE_R);
    i_port_b->set_mode(MODE_R);
    i_port_ch->set_mode(MODE_R);
    i_port_cl->set_mode(MODE_R);
}

unsigned int I8255::get_value(unsigned int address)
{
    uint8_t data = registers[address & 0b11];
#ifdef LOG_8255
    if (log_available()) logs(QString("R %1:%2").arg(address & 0b11).arg(data, 2, 16, QChar('0')));
#endif
    return data;
}

void I8255::set_value(unsigned int address, unsigned int value)
{
    unsigned int n = address & 0b11;
    switch (n) {
    case 0:
        if ((registers[3] & 0x60) == 0)
        {
            if ((registers[3] & 0x10) == 0)
            {
                registers[n] = (uint8_t)value;
                i_port_a->change(value);
            }
        } else {
            im->dm->error(this, I8255::tr("i8255:A is in an unsupported mode"));
        }
        break;
    case 1:
        if ((registers[3] & 4) == 0)
        {
            if ((registers[3] & 2) == 0)
            {
                registers[n] = (uint8_t)value;
                i_port_b->change(value);
            }
        } else {
            im->dm->error(this, I8255::tr("i8255:B is in an unsupported mode"));
        }
        break;
    case 2:
        if ((registers[3] & 8) == 0)
        {
            registers[n] &= 0x0F;
            registers[n] |= (uint8_t)value & 0xF0;
            i_port_ch->change(registers[n] >> 4);
        }
        if ((registers[3] & 1) == 0)
        {
            registers[n] &= 0xF0;
            registers[n] |= (uint8_t)value & 0x0F;
            i_port_cl->change(registers[n]);
        }
        break;
    default: //3 - control register
        if ((value & 0x80) != 0)
        {
            //Set mode
            registers[n] = (uint8_t)value;
            i_port_a-> set_mode( ((value & 0x10) == 0)?MODE_W:MODE_R );
            i_port_b-> set_mode( ((value & 0x02) == 0)?MODE_W:MODE_R );
            i_port_ch->set_mode( ((value & 0x08) == 0)?MODE_W:MODE_R );
            i_port_cl->set_mode( ((value & 0x01) == 0)?MODE_W:MODE_R );

            if (i_port_a->get_mode() == MODE_R) interface_callback(PORT_A, i_port_a->value, registers[0]);
            if (i_port_b->get_mode() == MODE_R) interface_callback(PORT_B, i_port_b->value, registers[1]);
            if (i_port_ch->get_mode() == MODE_R) interface_callback(PORT_CH, i_port_ch->value, registers[2] >> 4);
            if (i_port_cl->get_mode() == MODE_R) interface_callback(PORT_CL, i_port_cl->value, registers[2] & 0xF);
        } else {
            //Bitwise operations on C
            unsigned int bn = value >> 1;
            unsigned int bv = (value & 1) << bn;
            unsigned int bm = 1 << bn;
            unsigned int v = (registers[2] & !bm) | bv;
            set_value(2, v);
        }
        break;
    }
#ifdef LOG_8255
    if (log_available()) logs(QString("W %1:%2").arg(n).arg(value, 2, 16, QChar('0')));
#endif

}

void I8255::interface_callback(unsigned int callback_id, unsigned int new_value, [[maybe_unused]] unsigned int old_value)
{
    switch (callback_id) {
    case PORT_A:
        if ((registers[3] & 0x60) == 0) {
            if ((registers[3] & 0x10) != 0) registers[0] = (uint8_t)new_value;
        } else
            im->dm->error(this, I8255::tr("i8255:A is in an unsupported mode"));
        break;
    case PORT_B:
        if ((registers[3] & 0x04) == 0) {
            if ((registers[3] & 0x02) != 0) registers[1] = (uint8_t)new_value;
        } else
            im->dm->error(this, I8255::tr("i8255:B is in an unsupported mode"));
        break;
    case PORT_CH:
        if ((registers[3] & 0x08) != 0)
        {
            registers[2] &= 0x0F;
            registers[2] |= (uint8_t)new_value & 0xF0;
        }
        break;
    case PORT_CL:
        if ((registers[3] & 0x01) != 0)
        {
            registers[2] &= 0xF0;
            registers[2] |= (uint8_t)new_value & 0x0F;
        }
        break;
    default:
        im->dm->error(this, I8255::tr("i8255:unknown interface called"));
        break;
    }

#ifdef LOG_8255
    logs(QString("C %1:%2").arg(callback_id).arg(new_value, 2, 16, QChar('0')));
#endif
}

ComputerDevice * create_i8255(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new I8255(im, cd);
}
