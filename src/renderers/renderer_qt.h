// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Software Qt renderer

#pragma once

#include <QLabel>
#include "emulator/renderer.h"

class QtRenderer: public VideoRenderer
{
private:
    QImage * surface = nullptr;
    QLabel * widget = nullptr;
    int render_w;
    int render_h;
    int filtering;

public:
    QtRenderer():
        VideoRenderer()
    {};

    std::string get_name() override
    {
        return "Qt";
    }

    virtual ~QtRenderer() override
    {
        if (surface != nullptr) delete surface;
    };

    void init_screen(void *p, int sx, int sy, double ss, double ps) override
    {
        VideoRenderer::init_screen(p, sx, sy, ss, ps);
        widget = reinterpret_cast<QLabel *>(p);
        resize(sx, sy, ss, ps);
    }

    void stop() override
    {
        if (surface != nullptr) delete surface;
        surface = nullptr;
    }

    void set_filtering(int value) override
    {
        filtering = value;
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
        screen_ss = ss;
        screen_ps = ps;
        surface = new QImage(sx, sy, QImage::Format_RGB32);
        surface->fill(Qt::black);
    }

    void render() override
    {
        int rx = widget->width();
        int ry = widget->height();
        if (screen_ss == 0) {
            int border = 20;
            int rx2 = rx - 2*border;
            int ry2 = ry - 2*border;
            float screen_aspect = (float)rx2 / ry2;
            float image_aspect = (float)screen_x / screen_y * screen_ps;

            if (screen_aspect > image_aspect) {
                render_w = (float)screen_x * ry2 / screen_y * screen_ps;
                render_h = ry2;
            } else {
                render_w = (float)rx2 * screen_ps;
                render_h = (float)screen_y * rx2 / screen_x;
            }
        } else {
            render_w = screen_x * screen_ss * screen_ps;
            render_h = screen_y * screen_ss;
        }

        QImage copy = surface->copy();
        QPixmap pm = QPixmap::fromImage(copy.scaled(
            render_w, render_h,
            Qt::IgnoreAspectRatio,
            (filtering==0)?Qt::FastTransformation:Qt::SmoothTransformation
            ));
        widget->setPixmap(pm);
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
