#include "speaker.h"
#include "emulator/utils.h"

Speaker::Speaker(InterfaceManager *im, EmulatorConfigDevice *cd):
    Sound(im, cd)

{
    i_input = create_interface(1, "input", MODE_R);
    i_mixer = create_interface(8, "mixer", MODE_R);
    cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu"));
    init_sound(cpu->clock);
}

Speaker::~Speaker()
{
    SDL_CloseAudioDevice(SDLdev);
}

void Speaker::init_sound(unsigned int clock_freq)
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
    memset(&SD.silence, 128, SILENCE_SIZE);

    SDL_AudioSpec want, have;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = SD.SamplesPerSec;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = BUFFER_SIZE;

    SDLdev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    SDL_PauseAudioDevice(SDLdev, 0);
}

unsigned int Speaker::calc_sound_value()
{
    if (muted)
        return 128;
    else {
        //return ((i_input->value & 0x01) != 0)?(127 + 127*volume/100):128;
        unsigned int V = 0;
        if (InputWidth != 0)
            V += i_input->value & 0x01;
        for (unsigned int i=0; i < MixerWidth; i++)
            V += (i_mixer->value >> i) & 0x01;
        return V * InputValue * volume / 100 + 127;
    }
}

void Speaker::clock(unsigned int counter)
{
    ComputerDevice::clock(counter);

    SD.ClockSampling += counter << 4;
    SD.ClockBuffering += counter;
    if (SD.ClockSampling >= SD.SamplingCount)
    {
        SD.ClockSampling -= SD.SamplingCount;
        unsigned int v = calc_sound_value();
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

void Speaker::reset(bool cold)
{
    Sound::reset(cold);

    if (i_input->linked == 0)
        InputWidth = 0;
    else
        InputWidth = 1;

    if (i_mixer->linked == 0)
        MixerWidth = 0;
    else
        MixerWidth = CalcBits(i_mixer->linked_bits, 8);

    if (InputWidth + MixerWidth > 0)
        InputValue = 127 / (InputWidth + MixerWidth);
    else
        InputValue = 1;
}

void Speaker::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);
}

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Speaker(im, cd);
}
