#ifndef O128DISPLAY_H
#define O128DISPLAY_H

#include "emulator/core.h"

class O128Display: public Display
{
private:
    unsigned int mode;
    unsigned int frame;
    unsigned int base_address;

    Port * port_mode;
    Port * port_frame;
    RAM * page_main;
    RAM * page_color;

    void render_byte(unsigned int address);

protected:
    virtual void render_all();

public:
    O128Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void clock(unsigned int counter);
    virtual void load_config(SystemData *sd);

    virtual void memory_callback(unsigned int callback_id, unsigned int address);

    virtual void get_screen(bool required);
    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy);
};

ComputerDevice * create_o128display(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // O128DISPLAY_H
