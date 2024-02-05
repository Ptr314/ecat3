#include "sound.h"

#define SILENCE_VALUE   128

GenericSound::GenericSound(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd),
    volume(100),
    muted(false)
{
    cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu"));
    init_sound(cpu->clock);
}

void GenericSound::init_sound(unsigned int clock_freq)
{
    SD.ClockSampling = 0;
    SD.ClockBuffering = 0;
    SD.SamplesPerSec = 44100;
    SD.BitsPerSample = 8;
    SD.BlocksFreq = 50;                                     //Blocks frequency

    SD.SamplingCount = clock_freq * 16 / SD.SamplesPerSec;  //CPU clocks per sample, 16x
    SD.BufferingCount = clock_freq / SD.BlocksFreq;         //CPU clocks per block
    SD.SamplesInBuffer = SD.SamplesPerSec / SD.BlocksFreq;  //Samples in a block

    SD.BufferPtr = 0;
    SD.buffer_empty = 0;

    memset(&SD.buffer, 0, BUFFER_SIZE);
    memset(&SD.silence, SILENCE_VALUE, SILENCE_SIZE);

    SDL_AudioSpec want, have;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = SD.SamplesPerSec;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = BUFFER_SIZE;

    SDLdev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    SDL_PauseAudioDevice(SDLdev, 0);
}

GenericSound::~GenericSound()
{
    SDL_CloseAudioDevice(SDLdev);
}

void GenericSound::set_volume(unsigned int volume)
{
    this->volume = volume;
}

void GenericSound::set_muted(bool muted)
{
    this->muted = muted;
}

void GenericSound::clock(unsigned int counter)
{
    ComputerDevice::clock(counter);

    SD.ClockSampling += counter << 4;
    SD.ClockBuffering += counter;
    if (SD.ClockSampling >= SD.SamplingCount)
    {
        SD.ClockSampling -= SD.SamplingCount;
        unsigned int v = muted?SILENCE_VALUE:calc_sound_value();
        SD.buffer[SD.BufferPtr] = v;

        //Non-zero value shows when the buffer contains varying sound
        //if (SD.BufferPtr>0)
        //    SD.buffer_empty |= SD.buffer[SD.BufferPtr-1] ^ v;
        //Not working :( so do not stop the stream all the time
        //TODO: solve it
        SD.buffer_empty = 1;

        SD.BufferPtr++;
    }
    if (SD.ClockBuffering >= SD.BufferingCount)
    {
        SD.ClockBuffering -= SD.BufferingCount;

        if (SD.buffer_empty != 0)
        {
            if (SDL_GetQueuedAudioSize(SDLdev) == 0)
                SDL_QueueAudio(SDLdev, SD.silence, sizeof(SD.silence));

            SDL_QueueAudio(SDLdev, SD.buffer, SD.BufferPtr);
        };

        SD.BufferPtr = 0;
        SD.buffer_empty = 0;
    }
}
