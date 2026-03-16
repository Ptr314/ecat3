// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: miniaudio-based audio driver

#include "audio_driver_miniaudio.h"

#include <cstring>
#include <iostream>

// Windows XP compatibility: disable backends that require Vista+
#if defined(_WIN32) && defined(__GNUC__) && (__GNUC__ < 5)
    #define MA_NO_WASAPI
    #define MA_NO_DSOUND
#endif

#include "libs/miniaudio/miniaudio.h"

// Helper struct to pass both callback and userdata through miniaudio's single pUserData pointer
struct MiniaudioCallbackData {
    AudioCallback callback;
    void* userdata;
    int channels;
};

static void ma_data_callback_bridge(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    MiniaudioCallbackData* cbd = static_cast<MiniaudioCallbackData*>(device->pUserData);
    int len = static_cast<int>(frameCount) * cbd->channels * static_cast<int>(sizeof(int16_t));
    // Zero the buffer first (miniaudio doesn't guarantee zeroed output)
    std::memset(output, 0, static_cast<size_t>(len));
    cbd->callback(cbd->userdata, static_cast<uint8_t*>(output), len);
}

MiniaudioDriver::MiniaudioDriver()
    : m_device(NULL)
    , m_sampleRate(0)
    , m_bufferSamples(0)
    , m_callback(NULL)
    , m_userdata(NULL)
    , m_opened(false)
{
}

MiniaudioDriver::~MiniaudioDriver()
{
    close();
}

bool MiniaudioDriver::open(int sampleRate, int channels, int bufferSamples,
                           AudioCallback callback, void* userdata)
{
    if (m_opened) return false;

    m_callback = callback;
    m_userdata = userdata;

    // Allocate callback data struct (persists for device lifetime)
    MiniaudioCallbackData* cbd = new MiniaudioCallbackData;
    cbd->callback = callback;
    cbd->userdata = userdata;
    cbd->channels = channels;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_s16;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate        = static_cast<ma_uint32>(sampleRate);
    config.periodSizeInFrames = static_cast<ma_uint32>(bufferSamples);
    config.dataCallback      = ma_data_callback_bridge;
    config.pUserData         = cbd;

    m_device = new ma_device;

    if (ma_device_init(NULL, &config, m_device) != MA_SUCCESS) {
        std::cerr << "miniaudio: failed to init device" << std::endl;
        delete m_device;
        m_device = NULL;
        delete cbd;
        return false;
    }

    m_sampleRate = static_cast<int>(m_device->sampleRate);
    m_bufferSamples = bufferSamples;
    m_opened = true;
    return true;
}

int MiniaudioDriver::getObtainedSampleRate() const
{
    return m_sampleRate;
}

int MiniaudioDriver::getObtainedBufferSamples() const
{
    return m_bufferSamples;
}

void MiniaudioDriver::start()
{
    if (m_opened && m_device) {
        if (ma_device_start(m_device) != MA_SUCCESS) {
            std::cerr << "miniaudio: failed to start device" << std::endl;
        }
    }
}

void MiniaudioDriver::stop()
{
    if (m_opened && m_device) {
        ma_device_stop(m_device);
    }
}

void MiniaudioDriver::close()
{
    if (m_opened && m_device) {
        // Save pUserData before uninit so we can free it
        MiniaudioCallbackData* cbd = static_cast<MiniaudioCallbackData*>(m_device->pUserData);
        ma_device_uninit(m_device);
        delete cbd;
        delete m_device;
        m_device = NULL;
        m_opened = false;
    }
}
