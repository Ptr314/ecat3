#pragma once

#include <qdebug.h>
#include <qlogging.h>

#include <SDL.h>

#include "emulator/renderer.h"

class SDL2Renderer: public VideoRenderer
{
private:
    SDL_Window * SDLWindowRef = nullptr;
    SDL_Renderer * SDLRendererRef = nullptr;
    SDL_Texture * SDLTexture = nullptr;
    SDL_Surface * device_surface = nullptr;
    SDL_Texture * black_box = nullptr;
    SDL_Rect render_rect;

public:
    SDL2Renderer():
        VideoRenderer()
    {};

    std::string get_name() override
    {
        return "SDL2";
    }

    virtual ~SDL2Renderer() override
    {
        VideoRenderer::~VideoRenderer();
        if (black_box != nullptr) SDL_DestroyTexture(black_box);
        if (device_surface != nullptr) SDL_FreeSurface(device_surface);
    };

    void init_screen(void *p, int sx, int sy, double ss, double ps) override
    {
        VideoRenderer::init_screen(p, sx, sy, ss, ps);

        if (SDLWindowRef == nullptr)
            // SDLWindowRef = SDL_CreateWindow("Screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_SKIP_TASKBAR);
            SDLWindowRef = SDL_CreateWindowFrom(p);
        if (!SDLWindowRef) {
            qWarning() << "SDL error:" << SDL_GetError();
        }

        if (SDLRendererRef == nullptr)
            SDLRendererRef = SDL_CreateRenderer(SDLWindowRef, -1, SDL_RENDERER_ACCELERATED);

        black_box = SDL_CreateTexture(SDLRendererRef, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING, 100, 100);
        unsigned char* black_bytes = nullptr;
        int pitch = 0;
        SDL_LockTexture(black_box, nullptr, reinterpret_cast<void**>(&black_bytes), &pitch);
        memset(black_bytes, 0, 100*100*4);
        SDL_UnlockTexture(black_box);
        SDL_RenderCopy(SDLRendererRef, black_box, NULL, NULL);
        render_rect.w = sx * ss * ps;
        render_rect.h = sy * ss;

        device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_x, screen_y, 32, SDL_PIXELFORMAT_RGBA8888);
    }

    void stop() override
    {
        if (black_box != nullptr) SDL_DestroyTexture(black_box);
        if (device_surface != nullptr) SDL_FreeSurface(device_surface);

        black_box = nullptr;
        device_surface = nullptr;
    }

    void set_filtering(int value) override
    {
        std::string s = std::to_string(value);
        char const *pchar = s.c_str();
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, pchar);
    }

    uint8_t * get_buffer() override
    {
        return reinterpret_cast<uint8_t *>(device_surface->pixels);
    }

    int get_line_bytes() override
    {
        return screen_x * 4;
    }

    void fill(uint32_t c) override
    {
        SDL_FillRect(device_surface, NULL, c);
    }

    void resize(int sx, int sy, double ss, double ps) override
    {
        SDL_FreeSurface(device_surface);
        screen_x = sx;
        screen_y = sy;
        render_rect.w = screen_x * ss * ps;
        render_rect.h = screen_y * ss;
        device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_x, screen_y, 32, SDL_PIXELFORMAT_RGBA8888);
        SDL_RenderCopy(SDLRendererRef, black_box, NULL, NULL);
    }

    void render() override
    {
        int rx, ry;
        SDL_GetRendererOutputSize(SDLRendererRef, &rx, &ry);
        render_rect.x = (rx - render_rect.w) / 2;
        render_rect.y = (ry - render_rect.h) / 2;

        SDLTexture = SDL_CreateTextureFromSurface(SDLRendererRef, device_surface);

        SDL_RenderCopy(SDLRendererRef, SDLTexture, NULL, &render_rect);
        SDL_RenderPresent(SDLRendererRef);

        SDL_DestroyTexture(SDLTexture);
    }

    uint32_t MapRGB(uint8_t R, uint8_t G, uint8_t B) override
    {
        return SDL_MapRGB(device_surface->format, R, G, B);
    }


};
