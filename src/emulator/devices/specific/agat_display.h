#ifndef AGATDISPLAY_H
#define AGATDISPLAY_H

#include "emulator/core.h"

class AgatDisplay : public GenericDisplay
{
protected:
    unsigned int mode;
    unsigned int previous_mode;
    unsigned int module;
    unsigned int base_address;
    unsigned int page_size;
    bool blinker;
    unsigned int clock_counter;
    unsigned int system_clock;
    unsigned int blink_ticks;
    unsigned int screen_counter, screen_ticks;

    unsigned int screen_line;
    unsigned int field_num;
    bool odd_lines;

    unsigned int prev_nmi;
    unsigned int prev_irq;

    Interface i_50hz;
    Interface i_500hz;
    Interface i_ints_en;

    Port * port_mode;
    RAM * memory[2];
    ROM * font;

    void render_line(unsigned int line);
    virtual void render_all(bool force_render) override;

    void set_mode(unsigned int new_mode);

public:
    AgatDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void set_surface(SDL_Surface * surface) override;

    // virtual void memory_callback(unsigned int callback_id, unsigned int address) override;

    virtual void clock(unsigned int counter) override;
    virtual void load_config(SystemData *sd) override;

    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
};

ComputerDevice * create_agat_display(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // AGATDISPLAY_H
