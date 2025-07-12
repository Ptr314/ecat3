// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Renderer abstract class

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class VideoRenderer
{
protected:
    int screen_x = 0;
    int screen_y = 0;
    double screen_ss = 1;
    double screen_ps = 1;
public:
    VideoRenderer() {};
    virtual ~VideoRenderer() {};
    virtual std::string get_name() = 0;
    virtual void init_screen(void *p, int sx, int sy, double ss, double ps)
    {
        screen_x = sx;
        screen_y = sy;
        screen_ss = ss;
        screen_ps = ps;
    };
    virtual void stop() = 0;
    virtual void set_filtering(int value) {};
    virtual uint8_t * get_buffer() = 0;
    virtual int get_line_bytes() = 0;
    virtual void fill(uint32_t c) = 0;
    virtual void resize(int sx, int sy, double ss, double ps) = 0;
    virtual void render() = 0;
    virtual uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) = 0;
    virtual void FillRGB(const uint8_t colors[][3], uint32_t * RGBA, int len)
    {
        for (int i=0; i<len; i++)
            RGBA[i] = MapRGB(colors[i][0], colors[i][1], colors[i][2]);
    }
    virtual std::vector<uint8_t> get_screenshot() = 0;
};
