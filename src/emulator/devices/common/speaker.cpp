#include "speaker.h"

Speaker::Speaker(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)

{
    //TODO: Speaker: Implement
    i_input = this->create_interface(1, "input", MODE_R);
    cpu = dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu"));
    init_sound(cpu->clock);
}

Speaker::~Speaker()
{
    SDL_CloseAudioDevice(SDLdev);
    //TODO: free buffers
}

//void audio_callback(void *userdata, Uint8 *stream, int len)
//{
//    static_cast<Speaker*>(userdata)->callback(stream, len);
//}

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

    SD.BufferLen = SD.SamplesInBuffer * (SD.BitsPerSample >> 3);

    SD.CurrentBuffer = 0;
    SD.BufferPtr = 0;
    SD.BufferLatency = 0;
    SD.EmptyCount = 1000;

    for (int i = 0; i<BUFFERS_COUNT+1; i++)
    {
        SD.Buffers[i] = new uint8_t[BUFFER_SIZE];
        memset(SD.Buffers[i], 0, BUFFER_SIZE);
        SD.BuffersFlags[i] = 0;
    }

    SDL_AudioSpec want, have;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = SD.SamplesPerSec;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = BUFFER_SIZE;
    want.userdata = this;
    want.callback = nullptr; //audio_callback;
    SDLdev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
}

//void Speaker::callback(Uint8 *stream, int len)
//{
//    //TODO: Implement
//    //SDL_memcpy (stream, , len);
//}

unsigned int Speaker::calc_sound_value()
{
    //TODO: Implement mixed values
    return i_input->value & 0x01;
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
        SD.Buffers[SD.CurrentBuffer][SD.BufferPtr] = v;

        //Non-zero flags show if a buffer has non-constant values
        if (SD.BufferPtr>0)
            SD.BuffersFlags[SD.CurrentBuffer] |= SD.Buffers[SD.CurrentBuffer][SD.BufferPtr-1] ^ v;

        SD.BufferPtr++;
    }
    if (SD.ClockBuffering >= SD.BufferingCount)
    {
        SD.ClockBuffering -= SD.BufferingCount;

        //SDL_QueueAudio
        //TODO: Continue here
    }
}

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Speaker(im, cd);
}
