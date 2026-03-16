// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: miniaudio-based audio driver

#pragma once

#include "audio_driver.h"

struct ma_device;

class MiniaudioDriver : public AudioDriver {
public:
    MiniaudioDriver();
    ~MiniaudioDriver();

    bool open(int sampleRate, int channels, int bufferSamples,
              AudioCallback callback, void* userdata) override;
    int  getObtainedSampleRate() const override;
    int  getObtainedBufferSamples() const override;
    void start() override;
    void stop() override;
    void close() override;

private:
    ma_device* m_device;
    int m_sampleRate;
    int m_bufferSamples;
    AudioCallback m_callback;
    void* m_userdata;
    bool m_opened;
};
