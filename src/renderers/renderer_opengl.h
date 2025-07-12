// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: OpenGL renderer

#pragma once

#include "emulator/renderer.h"
#include "GLWidget.h"

class OpenGLRenderer: public VideoRenderer
{
private:
    QImage * surface = nullptr;
    GLWidget * widget = nullptr;

public:
    OpenGLRenderer():
        VideoRenderer()
    {};

    std::string get_name() override
    {
        return "OpenGL";
    }

    virtual ~OpenGLRenderer() override
    {
        if (surface != nullptr) delete surface;
    };

    void init_screen(void *p, int sx, int sy, double ss, double ps) override
    {
        VideoRenderer::init_screen(p, sx, sy, ss, ps);
        widget = reinterpret_cast<GLWidget *>(p);
        resize(sx, sy, ss, ps);
    }

    void stop() override
    {
        if (surface != nullptr) delete surface;
        surface = nullptr;
    }

    uint8_t * get_buffer() override
    {
        return reinterpret_cast<uint8_t *>(surface->bits());
    }

    int get_line_bytes() override
    {
        return screen_x * 4;
    }

    void fill(uint32_t c) override
    {
        surface->fill(Qt::black);
    }

    void resize(int sx, int sy, double ss, double ps) override
    {
        if (surface != nullptr) delete surface;
        screen_x = sx;
        screen_y = sy;
        int render_w = sx * ss * ps;
        int render_h = sy * ss;
        surface = new QImage(sx, sy, QImage::Format_RGB32);
        surface->fill(Qt::black);
        widget->setImageSize(QSize(render_w, render_h));
        widget->setAspectRatioScale(ps);
    }

    void render() override
    {
        widget->updateTexture(*surface);
    }

    uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) override
    {
        return 0xFF000000 | (R << 16) | (G << 8) | B;
    }

    std::vector<uint8_t> get_screenshot() override
    {
        std::vector<uint8_t> image;
        int image_size = screen_x * screen_y * 4;
        uint8_t * pixels = surface->bits();
        image.insert(image.end(), pixels, pixels + image_size);
        // QImage has a reversed RGB order, so we need to swap R and B parts
        for (int i=0; i < image_size; i+=4) {
            uint8_t R = image[i + 2];
            image[i+2] = image[i];
            image[i] = R;
        }
        return image;
    }


};
