// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: SDL2-based audio driver (optional, behind USE_SDL_AUDIO)

#ifdef USE_SDL_AUDIO

#include "audio_driver_sdl.h"

#include <iostream>

SDLAudioDriver::SDLAudioDriver()
    : m_device(0)
    , m_sampleRate(0)
    , m_bufferSamples(0)
    , m_opened(false)
    , m_subsystemInit(false)
{
}

SDLAudioDriver::~SDLAudioDriver()
{
    close();
}

bool SDLAudioDriver::open(int sampleRate, int channels, int bufferSamples,
                          AudioCallback callback, void* userdata)
{
    if (m_opened) return false;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL audio init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    m_subsystemInit = true;

    SDL_AudioSpec desired_spec, obtained_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = sampleRate;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.channels = static_cast<Uint8>(channels);
    desired_spec.samples = static_cast<Uint16>(bufferSamples);
    desired_spec.userdata = userdata;
    desired_spec.callback = callback;

    m_device = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &obtained_spec, 0);
    if (m_device == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_subsystemInit = false;
        return false;
    }

    m_sampleRate = obtained_spec.freq;
    m_bufferSamples = obtained_spec.samples;
    m_opened = true;
    return true;
}

int SDLAudioDriver::getObtainedSampleRate() const
{
    return m_sampleRate;
}

int SDLAudioDriver::getObtainedBufferSamples() const
{
    return m_bufferSamples;
}

void SDLAudioDriver::start()
{
    if (m_opened && m_device != 0) {
        SDL_PauseAudioDevice(m_device, 0);
    }
}

void SDLAudioDriver::stop()
{
    if (m_opened && m_device != 0) {
        SDL_PauseAudioDevice(m_device, 1);
    }
}

void SDLAudioDriver::close()
{
    if (m_opened && m_device != 0) {
        SDL_PauseAudioDevice(m_device, 1);
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
        m_opened = false;
    }
    if (m_subsystemInit) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_subsystemInit = false;
    }
}

#endif // USE_SDL_AUDIO
