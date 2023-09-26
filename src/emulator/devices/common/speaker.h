#ifndef SPEAKER_H
#define SPEAKER_H

#include <SDL.h>
#include "emulator/core.h"

#define BUFFERS_COUNT   32 //Power of 2
#define BUFFER_SIZE     4096

typedef uint8_t SpeakerBuffer[BUFFER_SIZE];

struct SpeakerData{
    unsigned int ClockSampling;
    unsigned int ClockBuffering;
    unsigned int SamplesPerSec;
    unsigned int BitsPerSample;
    unsigned int BlocksFreq;
    unsigned int SamplingCount;
    unsigned int BufferingCount;
    unsigned int SamplesInBuffer;
    unsigned int BufferLen;
    //unsigned int BufferLen2;
    unsigned int BufferPtr;
    unsigned int BufferLatency;
    unsigned int CurrentBuffer;
    uint8_t * Buffers[BUFFERS_COUNT+1];
    uint8_t BuffersFlags[BUFFERS_COUNT];
    unsigned int EmptyCount;
};

class Speaker: public ComputerDevice
{
    //TODO: Speaker: Implement
private:
    Interface * i_input;
    CPU * cpu;
    SDL_AudioDeviceID SDLdev;
    SpeakerData SD;

    void init_sound(unsigned int clock_freq);
    unsigned int calc_sound_value();

public:
    Speaker(InterfaceManager *im, EmulatorConfigDevice *cd);
    ~Speaker();

    void callback(Uint8 *stream, int len);
    virtual void clock(unsigned int counter) override;
};

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // SPEAKER_H
