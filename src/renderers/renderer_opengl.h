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
        VideoRenderer::~VideoRenderer();
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


};
