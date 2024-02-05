#ifndef SPEAKER_H
#define SPEAKER_H

#include "emulator/core.h"
#include "emulator/devices/common/sound.h"

class Speaker: public GenericSound
{
private:
    Interface * i_input;
    Interface * i_mixer;
    unsigned int mode;
    unsigned int InputWidth;
    unsigned int MixerWidth;
    unsigned int InputValue;
    unsigned int flip_value;
    bool shorts;
    bool is_delayed;
    unsigned int delayed_value;
    unsigned int input;

    virtual unsigned int calc_sound_value() override;

public:
    Speaker(InterfaceManager *im, EmulatorConfigDevice *cd);

    virtual void reset(bool cold) override;
    virtual void load_config(SystemData *sd) override;
    virtual void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
};

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SPEAKER_H
