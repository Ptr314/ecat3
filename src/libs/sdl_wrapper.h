#ifndef SDL_WRAPPER_H
#define SDL_WRAPPER_H

#include <QImage>
#include <QPixelFormat>

#define SDL_INIT_VIDEO 1
#define SDL_INIT_AUDIO 2

using SDL_Surface=QImage;
using SDL_PixelFormat=QPixelFormat;
using SDL_Texture=QImage;
using Uint8=uint8_t;
using Uint16=uint16_t;
using Uint32=uint32_t;
using SDL_AudioFormat=Uint16;

#define SDLCALL __cdecl
#define SDL_memset memset
#define AUDIO_U8 0

typedef void (SDLCALL * SDL_AudioCallback) (void *userdata, Uint8 * stream, int len);

struct SDL_Rect
{
    int x, y;
    int w, h;
};

struct SDL_AudioSpec
{
    int freq;                   /**< DSP frequency -- samples per second */
    SDL_AudioFormat format;     /**< Audio data format */
    Uint8 channels;             /**< Number of channels: 1 mono, 2 stereo */
    Uint8 silence;              /**< Audio buffer silence value (calculated) */
    Uint16 samples;             /**< Audio buffer size in sample FRAMES (total samples divided by channel count) */
    Uint16 padding;             /**< Necessary for some compile environments */
    Uint32 size;                /**< Audio buffer size in bytes (calculated) */
    SDL_AudioCallback callback; /**< Callback that feeds the audio device (NULL to use SDL_QueueAudio()). */
    void *userdata;             /**< Userdata passed to callback (ignored for NULL callbacks). */
};

void SDL_Init(int flags){}
void SDL_Quit(){}

Uint32 SDL_MapRGB(const SDL_PixelFormat * format, Uint8 r, Uint8 g, Uint8 b)
{
    return (r<<16) + (g<<8) + b;
}

int SDL_FillRect(SDL_Surface * dst, const SDL_Rect * rect, Uint32 color)
{
    dst->fill(color);
    return 0;
}

#endif // SDL_WRAPPER_H
