// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Audio driver abstraction

#pragma once

#include <cstdint>

typedef void (*AudioCallback)(void* userdata, uint8_t* stream, int len);

class AudioDriver {
public:
    virtual ~AudioDriver() {}
    virtual bool open(int sampleRate, int channels, int bufferSamples,
                      AudioCallback callback, void* userdata) = 0;
    virtual int  getObtainedSampleRate() const = 0;
    virtual int  getObtainedBufferSamples() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
};
