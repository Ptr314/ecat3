#ifndef SPEAKER_H
#define SPEAKER_H

#include <SDL.h>
#include "emulator/core.h"

#define BUFFER_SIZE     4096
#define SILENCE_SIZE    256

//typedef uint8_t SpeakerBuffer[BUFFER_SIZE];

struct SpeakerData{
    unsigned int ClockSampling;
    unsigned int ClockBuffering;
    unsigned int SamplesPerSec;
    unsigned int BitsPerSample;
    unsigned int BlocksFreq;
    unsigned int SamplingCount;
    unsigned int BufferingCount;
    unsigned int SamplesInBuffer;
    unsigned int BufferPtr;
    uint8_t buffer[BUFFER_SIZE];
    uint8_t silence[SILENCE_SIZE];
    unsigned int buffer_empty;
};

class Speaker: public Sound
{
private:
    Interface * i_input;
    Interface * i_mixer;
    CPU * cpu;
    SDL_AudioDeviceID SDLdev;
    SpeakerData SD;
    unsigned int InputWidth;
    unsigned int MixerWidth;
    unsigned int InputValue;

    void init_sound(unsigned int clock_freq);
    unsigned int calc_sound_value();

public:
    Speaker(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~Speaker();

    virtual void reset(bool cold) override;
    virtual void clock(unsigned int counter) override;
    virtual void load_config(SystemData *sd) override;
};

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SPEAKER_H
