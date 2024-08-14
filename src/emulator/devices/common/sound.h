#ifndef GENERICSOUND_H
#define GENERICSOUND_H

#include <SDL.h>
#include "emulator/core.h"

#define BUFFER_SIZE     4096
#define SILENCE_SIZE    256

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

class GenericSound: public ComputerDevice
{
private:
    CPU * cpu;
    SDL_AudioDeviceID SDLdev;
    SpeakerData SD;

protected:
    unsigned int volume;
    bool muted;
    void init_sound(unsigned int clock_freq);
    virtual unsigned int calc_sound_value() = 0;

public:
    GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~GenericSound();

    virtual void clock(unsigned int counter) override;
    virtual void set_volume(unsigned int volume);
    virtual void set_muted(bool muted);
};

#endif // GENERICSOUND_H
