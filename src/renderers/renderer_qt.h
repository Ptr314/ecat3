#pragma once

#include <QLabel>
#include "emulator/renderer.h"

class QtRenderer: public VideoRenderer
{
private:
    QImage * surface;
    QImage * black_box;
    QLabel * widget;
    int render_w;
    int render_h;


public:
    QtRenderer():
        VideoRenderer()
    {};

    virtual ~QtRenderer() override
    {
        VideoRenderer::~VideoRenderer();
        if (black_box != nullptr) delete black_box;
        if (surface != nullptr) delete surface;
    };

    // RENDER_INFO init_screen(void *p, int screen_x, int screen_y, double screen_scale, double pixel_scale) override
    void init_screen(void *p, int sx, int sy, double ss, double ps) override
    {
        VideoRenderer::init_screen(p, sx, sy, ss, ps);

        widget = reinterpret_cast<QLabel *>(p);

        render_w = sx * ss * ps;
        render_h = sy * ss;

        surface = new QImage(sx, sy, QImage::Format_RGB32);
        black_box = new QImage(100, 100, QImage::Format_RGB32);
        black_box->fill(0);
    }
    void stop() override
    {
        if (black_box != nullptr) delete black_box;
        if (surface != nullptr) delete surface;

        black_box = nullptr;
        surface = nullptr;
    }

    void set_filtering(int value) override
    {
        // std::string s = std::to_string(value);
        // char const *pchar = s.c_str();
        // SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, pchar);
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
        render_w = screen_x * ss * ps;
        render_h = screen_y * ps;
        surface = new QImage(sx, sy, QImage::Format_RGB32);
        surface->fill(Qt::black);
    }

    void render() override
    {
        int w = screen_x * screen_ss * screen_ps;
        int h = screen_y * screen_ss;
        QImage copy = surface->copy();
        QPixmap pm = QPixmap::fromImage(copy.scaled(
            w, h,
            Qt::IgnoreAspectRatio,
            Qt::FastTransformation
            ));
        widget->setPixmap(pm);
    }

    uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) override
    {
        return 0xFF000000 | (R << 16) | (G << 8) | B;
    }


};
