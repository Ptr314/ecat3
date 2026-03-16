// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: SDL2-based audio driver (optional, behind USE_SDL_AUDIO)

#pragma once

#ifdef USE_SDL_AUDIO

#include "audio_driver.h"
#include <SDL.h>

class SDLAudioDriver : public AudioDriver {
public:
    SDLAudioDriver();
    ~SDLAudioDriver();

    bool open(int sampleRate, int channels, int bufferSamples,
              AudioCallback callback, void* userdata) override;
    int  getObtainedSampleRate() const override;
    int  getObtainedBufferSamples() const override;
    void start() override;
    void stop() override;
    void close() override;

private:
    SDL_AudioDeviceID m_device;
    int m_sampleRate;
    int m_bufferSamples;
    bool m_opened;
    bool m_subsystemInit;
};

#endif // USE_SDL_AUDIO
