#ifndef GENERATOR_H
#define GENERATOR_H

#include "emulator/core.h"

class Generator:public ComputerDevice
{
private:
    Interface * i_out;
    Interface * i_enable;

    bool positive;
    unsigned int total_counts;
    unsigned int pulse_counts;
    unsigned int pulse_stored;
    bool in_pulse;
    bool enabled;

public:
    Generator(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd) override;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    virtual void system_clock(unsigned int counter) override;
};

ComputerDevice * create_generator(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // GENERATOR_H
