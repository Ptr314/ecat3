#ifndef REGISTER_H
#define REGISTER_H


#include "emulator/core.h"

#define REGISTER_LATCH_POS  0
#define REGISTER_BUFFER     1

#define CHANGED_IN          1
#define CHANGED_C           2

class Register:public ComputerDevice
{
private:
    Interface * i_in;
    Interface * i_out;
    Interface * i_c;

    unsigned int register_value;
    unsigned int store_type;

public:
    Register(InterfaceManager *im, EmulatorConfigDevice *cd):
        ComputerDevice(im, cd),
        register_value(0),
        store_type(REGISTER_LATCH_POS)
    {
        i_in =  create_interface(16, "in", MODE_R, CHANGED_IN);
        i_out = create_interface(16, "out", MODE_W);
        i_c =   create_interface(1, "c", MODE_R, CHANGED_C);

        i_out->change(register_value);
    }

    virtual void reset(bool cold) override
    {
        register_value = 0;
        i_out->change(register_value);
    }

    virtual void load_config(SystemData *sd) override
    {
        ComputerDevice::load_config(sd);
        //TODO: Register - add other types
        QString type_string = cd->get_parameter("type", false).value.toLower();
        if (type_string.isEmpty() || type_string == "latch-pos")
            store_type = REGISTER_LATCH_POS;
        else if (type_string == "buffer")
            store_type = REGISTER_BUFFER;

    }

    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override
    {
        if (callback_id == CHANGED_C)
        {
            switch (store_type) {
            case REGISTER_LATCH_POS:
                if (i_c->pos_edge()){
                    register_value = i_in->value;
                    i_out->change(register_value);
                }
                break;
            }
        } else
        if (callback_id == CHANGED_IN && store_type == REGISTER_BUFFER) {
            register_value = i_in->value;
            i_out->change(register_value);
        }
    }

};

ComputerDevice * create_register(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Register(im, cd);
}


#endif // REGISTER_H
